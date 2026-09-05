//
// Created by William on 2026-09-04.
//

#if WILL_EDITOR

// httplib pulls winsock2.h and must precede anything that may reach windows.h.
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include "mcp_server.h"
#include "mcp_call_internal.h"

#include <chrono>
#include <semaphore>
#include <tracy/Tracy.hpp>

#include "core/memory/concurrent_queue_traits.h"
#include "core/memory/memory_manager.h"
#include "core/time/frame_stamp.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "platform/thread_utils.h"

namespace Engine::MCP
{
static constexpr const char* PROTOCOL_VERSION = "2025-06-18";
static constexpr const char* FALLBACK_PROTOCOL_VERSION = "2025-03-26";
static constexpr const char* SERVER_NAME = "will-engine";
static constexpr const char* SERVER_VERSION = "0.1.0";
static constexpr const char* ENDPOINT = "/mcp";
static constexpr const char* JSON_MIME = "application/json";
static constexpr time_t SOCKET_TIMEOUT_SECONDS = 5;
static constexpr size_t MAX_CONNECTION_THREADS = 4;
static constexpr std::chrono::milliseconds DRAIN_POLL_INTERVAL{50};
static constexpr std::chrono::seconds DRAIN_WAIT_TIMEOUT{5};

static constexpr int ERROR_PARSE = -32700;
static constexpr int ERROR_INVALID_REQUEST = -32600;
static constexpr int ERROR_METHOD_NOT_FOUND = -32601;
static constexpr int ERROR_INVALID_PARAMS = -32602;
static constexpr int ERROR_INTERNAL = -32603;

struct PendingCall
{
    StringID toolId{};
    nlohmann::json args = nlohmann::json::object();
    Call::Impl call{};
    std::binary_semaphore done{0};
    std::atomic<uint32_t> refs{2};
    std::atomic<bool> bAbandoned{false};
    uint64_t callId{0};
    uint64_t frame{0};
    ToolResult outcome{ToolResult::Error};
};

struct ServerImpl
{
    httplib::Server server;
    EngineContext* ctx{};
    EngineState* state{};
    Core::MemoryManager* memoryManager{};
    Core::ConcurrentQueue<PendingCall*> queue;
    std::atomic<uint64_t> nextCallId{1};
    std::atomic<bool>* bShouldExit{};
};

static void ReleasePendingCall(ServerImpl& impl, PendingCall* pending)
{
    if (pending->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        pending->~PendingCall();
        impl.memoryManager->GeneralFree(pending);
    }
}

static nlohmann::json MakeError(const nlohmann::json& id, const int code, const char* message)
{
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}},
    };
}

static nlohmann::json MakeResult(const nlohmann::json& id, nlohmann::json result)
{
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
    };
}

static nlohmann::json MakeToolResponse(const nlohmann::json& id, Call::Impl& call, const ToolResult outcome)
{
    nlohmann::json content = nlohmann::json::array();
    if (outcome == ToolResult::Error || call.bError) {
        content.push_back({{"type", "text"}, {"text", call.errorMessage.IsEmpty() ? "Tool failed" : call.errorMessage.c_str()}});
        return MakeResult(id, {{"content", std::move(content)}, {"isError", true}});
    }

    content.push_back({{"type", "text"}, {"text", call.result.dump()}});
    return MakeResult(id, {{"content", std::move(content)}, {"structuredContent", std::move(call.result)}, {"isError", false}});
}

static nlohmann::json HandleInitialize(const nlohmann::json& request)
{
    std::string negotiated = PROTOCOL_VERSION;
    const auto params = request.find("params");
    if (params != request.end() && params->is_object()) {
        const auto requested = params->find("protocolVersion");
        if (requested != params->end() && requested->is_string()) {
            const std::string& v = requested->get_ref<const std::string&>();
            if (v == FALLBACK_PROTOCOL_VERSION) {
                negotiated = v;
            }
        }
    }

    return {
        {"protocolVersion", negotiated},
        {"capabilities", {{"tools", {{"listChanged", false}}}}},
        {"serverInfo", {{"name", SERVER_NAME}, {"version", SERVER_VERSION}}},
    };
}

static nlohmann::json HandleToolsList(ServerImpl& impl)
{
    nlohmann::json tools = nlohmann::json::array();

    ToolRegistry& r = impl.state->mcpTools;
    std::lock_guard lock(r.mutex);
    for (size_t i = 0; i < r.tools.Size(); ++i) {
        const ToolEntry& e = r.tools[i];
        nlohmann::json schema = e.inputSchemaJson ? nlohmann::json::parse(e.inputSchemaJson, nullptr, false) : nlohmann::json();
        if (!schema.is_object()) {
            schema = {{"type", "object"}};
        }
        tools.push_back({
            {"name", e.name},
            {"description", e.description ? e.description : ""},
            {"inputSchema", std::move(schema)},
        });
    }

    return {{"tools", std::move(tools)}};
}

static nlohmann::json HandleDrainedCall(ServerImpl& impl, const nlohmann::json& id, const StringID toolId, const nlohmann::json& args)
{
    auto pending = new(impl.memoryManager->GeneralAllocRaw(sizeof(PendingCall), Core::AllocTag::MCPServer)) PendingCall();
    pending->toolId = toolId;
    pending->args = args;
    pending->call.args = &pending->args;
    pending->callId = impl.nextCallId.fetch_add(1, std::memory_order_relaxed);

    impl.queue.enqueue(pending);

    const auto deadline = std::chrono::steady_clock::now() + DRAIN_WAIT_TIMEOUT;
    bool bFinished = false;
    while (!bFinished) {
        bFinished = pending->done.try_acquire_for(DRAIN_POLL_INTERVAL);
        if (!bFinished && (impl.bShouldExit->load(std::memory_order_acquire) || std::chrono::steady_clock::now() >= deadline)) {
            break;
        }
    }

    if (!bFinished) {
        pending->bAbandoned.store(true, std::memory_order_release);
        bFinished = pending->done.try_acquire();
    }
    if (!bFinished) {
        ReleasePendingCall(impl, pending);
        return MakeError(id, ERROR_INTERNAL, "Engine thread did not service the call in time; the engine is paused, stalled or shutting down");
    }

    if (pending->call.result.is_object()) {
        pending->call.result["callId"] = pending->callId;
        pending->call.result["frame"] = pending->frame;
    }
    nlohmann::json response = MakeToolResponse(id, pending->call, pending->outcome);
    ReleasePendingCall(impl, pending);
    return response;
}

static nlohmann::json HandleToolsCall(ServerImpl& impl, const nlohmann::json& id, const nlohmann::json& request)
{
    const auto params = request.find("params");
    if (params == request.end() || !params->is_object()) {
        return MakeError(id, ERROR_INVALID_PARAMS, "Missing params");
    }
    const auto nameField = params->find("name");
    if (nameField == params->end() || !nameField->is_string()) {
        return MakeError(id, ERROR_INVALID_PARAMS, "Missing tool name");
    }

    const std::string& name = nameField->get_ref<const std::string&>();
    const StringID toolId(Hash(name.c_str(), name.size()));

    ToolEntry entry{};
    {
        ToolRegistry& r = impl.state->mcpTools;
        std::lock_guard lock(r.mutex);
        const size_t* index = r.mapping.Find(toolId);
        if (!index) {
            return MakeError(id, ERROR_INVALID_PARAMS, "Unknown tool");
        }
        entry = r.tools[*index];
    }

    const auto argsField = params->find("arguments");
    const nlohmann::json args = argsField != params->end() && argsField->is_object() ? *argsField : nlohmann::json::object();

    if (entry.bNeedsDrain) {
        return HandleDrainedCall(impl, id, toolId, args);
    }

    Call::Impl callImpl{};
    callImpl.args = &args;
    Call call(&callImpl);
    const ToolResult outcome = entry.invoke(impl.ctx, impl.state, call);
    LOG_INFO(MCP, "mcp/{} tool={} result={}", impl.nextCallId.fetch_add(1, std::memory_order_relaxed), entry.name, outcome == ToolResult::Complete && !callImpl.bError ? "complete" : "error");
    return MakeToolResponse(id, callImpl, outcome);
}

/** @return false for notifications, which carry no id and must not produce a response body. */
static bool HandleRequest(ServerImpl& impl, const nlohmann::json& request, nlohmann::json& outResponse)
{
    const auto methodField = request.find("method");
    if (methodField == request.end() || !methodField->is_string()) {
        outResponse = MakeError(nullptr, ERROR_INVALID_REQUEST, "Missing method");
        return true;
    }

    const auto idField = request.find("id");
    if (idField == request.end() || idField->is_null()) {
        return false;
    }

    const std::string& method = methodField->get_ref<const std::string&>();
    const nlohmann::json& id = *idField;

    if (method == "initialize") {
        outResponse = MakeResult(id, HandleInitialize(request));
        return true;
    }
    if (method == "ping") {
        outResponse = MakeResult(id, nlohmann::json::object());
        return true;
    }
    if (method == "tools/list") {
        outResponse = MakeResult(id, HandleToolsList(impl));
        return true;
    }
    if (method == "tools/call") {
        outResponse = HandleToolsCall(impl, id, request);
        return true;
    }

    outResponse = MakeError(id, ERROR_METHOD_NOT_FOUND, "Method not found");
    return true;
}

MCPServer::MCPServer(Core::MemoryManager& memoryManager_)
    : memoryManager(memoryManager_)
{
    impl = new(memoryManager.PersistentAllocRaw(sizeof(ServerImpl), Core::AllocTag::MCPServer)) ServerImpl();
    impl->memoryManager = &memoryManager;
    impl->bShouldExit = &bShouldExit;

    impl->server.new_task_queue = [] { return new httplib::ThreadPool(MAX_CONNECTION_THREADS); };
    impl->server.set_read_timeout(SOCKET_TIMEOUT_SECONDS, 0);
    impl->server.set_write_timeout(SOCKET_TIMEOUT_SECONDS, 0);

    ServerImpl* serverImpl = impl;
    impl->server.Post(ENDPOINT, [serverImpl](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");

        const nlohmann::json body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) {
            res.status = 400;
            res.set_content(MakeError(nullptr, ERROR_PARSE, "Parse error").dump(), JSON_MIME);
            return;
        }
        if (!body.is_object()) {
            res.status = 400;
            res.set_content(MakeError(nullptr, ERROR_INVALID_REQUEST, "Invalid Request").dump(), JSON_MIME);
            return;
        }

        nlohmann::json response;
        if (!HandleRequest(*serverImpl, body, response)) {
            res.status = 202;
            return;
        }

        res.status = 200;
        res.set_content(response.dump(), JSON_MIME);
    });

    auto rejectMethod = [](const httplib::Request&, httplib::Response& res) {
        res.status = 405;
        res.set_content(MakeError(nullptr, ERROR_METHOD_NOT_FOUND, "Only POST is supported").dump(), JSON_MIME);
    };
    impl->server.Get(ENDPOINT, rejectMethod);
    impl->server.Delete(ENDPOINT, rejectMethod);
}

MCPServer::~MCPServer()
{
    RequestShutdown();
    Join();

    if (impl) {
        PendingCall* pending{};
        while (impl->queue.try_dequeue(pending)) {
            ReleasePendingCall(*impl, pending);
        }
        impl->~ServerImpl();
        memoryManager.PersistentFree(impl);
        impl = nullptr;
    }
}

void MCPServer::Start(const int32_t port, EngineContext* ctx, EngineState* state)
{
    impl->ctx = ctx;
    impl->state = state;
    bShouldExit.store(false, std::memory_order_release);
    boundPort = port;
    thisThread = std::jthread([this, port] { ThreadMain(port); });
}

void MCPServer::Drain(EngineContext* ctx, EngineState* state)
{
    ZoneScoped;
    PendingCall* pending{};
    while (impl->queue.try_dequeue(pending)) {
        if (pending->bAbandoned.load(std::memory_order_acquire)) {
            ReleasePendingCall(*impl, pending);
            continue;
        }

        ToolEntry entry{};
        bool bFound = false;
        {
            ToolRegistry& r = state->mcpTools;
            std::lock_guard lock(r.mutex);
            if (const size_t* index = r.mapping.Find(pending->toolId)) {
                entry = r.tools[*index];
                bFound = true;
            }
        }

        pending->frame = Core::gGameFrame.load(std::memory_order_relaxed);
        Call call(&pending->call);
        if (!bFound) {
            call.SetError("Tool is no longer registered; the game DLL was reloaded between dispatch and drain");
            pending->outcome = ToolResult::Error;
        }
        else {
            LOG_INFO(MCP, "mcp/{} begin frame={} tool={}", pending->callId, pending->frame, entry.name);
            pending->outcome = entry.invoke(ctx, state, call);
            LOG_INFO(MCP, "mcp/{} end frame={} result={}", pending->callId, pending->frame, pending->outcome == ToolResult::Complete && !pending->call.bError ? "complete" : "error");
        }

        pending->done.release();
        ReleasePendingCall(*impl, pending);
    }
}

void MCPServer::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    if (impl) {
        impl->server.stop();
    }
}

void MCPServer::Join()
{
    if (thisThread.joinable()) {
        thisThread.join();
    }
}

void MCPServer::ThreadMain(const int32_t port)
{
    ZoneScoped;
    tracy::SetThreadName("MCPServer");
    Platform::SetThreadName("MCPServer");

    if (!impl->server.bind_to_port("127.0.0.1", port)) {
        LOG_ERROR(MCP, "Failed to bind 127.0.0.1:{}, server disabled for this run", port);
        return;
    }

    LOG_INFO(MCP, "Listening on http://127.0.0.1:{}{}", port, ENDPOINT);
    bListening.store(true, std::memory_order_release);

    impl->server.listen_after_bind();

    bListening.store(false, std::memory_order_release);
    LOG_INFO(MCP, "Stopped");
}
} // Engine::MCP

#endif // WILL_EDITOR
