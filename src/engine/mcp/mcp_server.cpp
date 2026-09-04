//
// Created by William on 2026-09-04.
//

#if WILL_EDITOR

// httplib pulls winsock2.h and must precede anything that may reach windows.h.
#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include "mcp_server.h"

#include <tracy/Tracy.hpp>

#include "core/memory/memory_manager.h"
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

static constexpr int ERROR_PARSE = -32700;
static constexpr int ERROR_INVALID_REQUEST = -32600;
static constexpr int ERROR_METHOD_NOT_FOUND = -32601;
static constexpr int ERROR_INVALID_PARAMS = -32602;

struct ServerImpl
{
    httplib::Server server;
};

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

/** @return false for notifications, which carry no id and must not produce a response body. */
static bool HandleRequest(const nlohmann::json& request, nlohmann::json& outResponse)
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
        outResponse = MakeResult(id, {{"tools", nlohmann::json::array()}});
        return true;
    }
    if (method == "tools/call") {
        outResponse = MakeError(id, ERROR_INVALID_PARAMS, "Unknown tool");
        return true;
    }

    outResponse = MakeError(id, ERROR_METHOD_NOT_FOUND, "Method not found");
    return true;
}

MCPServer::MCPServer(Core::MemoryManager& memoryManager_)
    : memoryManager(memoryManager_)
{
    impl = new(memoryManager.PersistentAllocRaw(sizeof(ServerImpl), Core::AllocTag::MCPServer)) ServerImpl();

    impl->server.new_task_queue = [] { return new httplib::ThreadPool(MAX_CONNECTION_THREADS); };
    impl->server.set_read_timeout(SOCKET_TIMEOUT_SECONDS, 0);
    impl->server.set_write_timeout(SOCKET_TIMEOUT_SECONDS, 0);

    impl->server.Post(ENDPOINT, [](const httplib::Request& req, httplib::Response& res) {
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
        if (!HandleRequest(body, response)) {
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
        impl->~ServerImpl();
        memoryManager.PersistentFree(impl);
        impl = nullptr;
    }
}

void MCPServer::Start(const int32_t port)
{
    bShouldExit.store(false, std::memory_order_release);
    boundPort = port;
    thisThread = std::jthread([this, port] { ThreadMain(port); });
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
