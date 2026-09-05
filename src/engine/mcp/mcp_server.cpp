//
// Created by William on 2026-09-04.
//

#if WILL_EDITOR

#include "mcp_server.h"
#include "mcp_call_internal.h"
#include "mcp_json.h"

#include <cctype>
#include <chrono>
#include <semaphore>
#include <string_view>
#include <tracy/Tracy.hpp>

#include "core/memory/concurrent_queue_traits.h"
#include "core/memory/memory_manager.h"
#include "core/time/frame_stamp.h"
#include "engine/engine_api.h"
#include "engine/logging/engine_log.h"
#include "platform/tcp_socket.h"
#include "platform/thread_utils.h"

namespace Engine::MCP
{
static constexpr const char* PROTOCOL_VERSION = "2025-06-18";
static constexpr const char* FALLBACK_PROTOCOL_VERSION = "2025-03-26";
static constexpr const char* SERVER_NAME = "will-engine";
static constexpr const char* SERVER_VERSION = "0.2.0";
static constexpr const char* ENDPOINT = "/mcp";
static constexpr const char* DEFAULT_SCHEMA = R"({"type":"object"})";
static constexpr uint32_t SOCKET_TIMEOUT_MS = 5000;
static constexpr int LISTEN_BACKLOG = 8;
static constexpr size_t RECV_CHUNK = 4096;
static constexpr size_t MAX_HEADER_BYTES = 8 * 1024;
static constexpr size_t MAX_BODY_BYTES = 1024 * 1024;
static constexpr size_t RESPONSE_RESERVE = 4096;
static constexpr size_t ARGS_LOG_RESERVE = 256;
static constexpr std::chrono::seconds DRAIN_WAIT_TIMEOUT{5};

static constexpr int HTTP_OK = 200;
static constexpr int HTTP_ACCEPTED = 202;
static constexpr int HTTP_BAD_REQUEST = 400;
static constexpr int HTTP_NOT_FOUND = 404;
static constexpr int HTTP_METHOD_NOT_ALLOWED = 405;
static constexpr int HTTP_LENGTH_REQUIRED = 411;
static constexpr int HTTP_PAYLOAD_TOO_LARGE = 413;

static constexpr int ERROR_PARSE = -32700;
static constexpr int ERROR_INVALID_REQUEST = -32600;
static constexpr int ERROR_METHOD_NOT_FOUND = -32601;
static constexpr int ERROR_INVALID_PARAMS = -32602;
static constexpr int ERROR_INTERNAL = -32603;

struct RpcId
{
    enum class Kind : uint8_t
    {
        Null,
        Number,
        String,
    };

    Kind kind{Kind::Null};
    const char* text{};
    uint32_t length{0};
};

struct PendingCall
{
    explicit PendingCall(Core::TlsfAllocator* allocator) : call(allocator) {}

    StringID toolId{};
    Core::Vector<char> request{};
    JsonReader reader{};
    Call::Impl call;
    std::binary_semaphore done{0};
    std::atomic<uint32_t> refs{2};
    std::atomic<bool> bAbandoned{false};
    uint64_t callId{0};
    uint64_t frame{0};
    ToolResult outcome{ToolResult::Error};
};

struct ServerImpl
{
    EngineContext* ctx{};
    EngineState* state{};
    Core::TlsfAllocator* allocator{};
    Core::ConcurrentQueue<PendingCall*> queue;
    uint64_t nextCallId{1};
    std::atomic<Platform::TcpSocket> listenSocket{Platform::INVALID_TCP_SOCKET};
};

struct HttpRequest
{
    const char* method{};
    const char* path{};
    size_t contentLength{0};
    bool bChunked{false};
    bool bExpectContinue{false};
};

static void ReleasePendingCall(ServerImpl& impl, PendingCall* pending)
{
    if (pending->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        pending->~PendingCall();
        impl.allocator->Free(pending);
    }
}

static const char* StatusText(const int status)
{
    switch (status) {
        case HTTP_OK: return "OK";
        case HTTP_ACCEPTED: return "Accepted";
        case HTTP_BAD_REQUEST: return "Bad Request";
        case HTTP_NOT_FOUND: return "Not Found";
        case HTTP_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_LENGTH_REQUIRED: return "Length Required";
        case HTTP_PAYLOAD_TOO_LARGE: return "Payload Too Large";
        default: return "Internal Server Error";
    }
}

static void SendResponse(const Platform::TcpSocket socket, const int status, const char* body, const size_t bodyLength)
{
    const auto header = Core::InlineString<256>::Format("HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", status, StatusText(status), bodyLength);
    if (Platform::TcpSendAll(socket, header.c_str(), header.Size()) && bodyLength > 0) {
        Platform::TcpSendAll(socket, body, bodyLength);
    }
}

static void WriteId(JsonWriter& w, const RpcId& id)
{
    switch (id.kind) {
        case RpcId::Kind::Number: w.Raw(id.text, id.length); break;
        case RpcId::Kind::String: w.String(id.text, id.length); break;
        case RpcId::Kind::Null: w.Null(); break;
    }
}

static void WriteRpcError(JsonWriter& w, const RpcId& id, const int code, const char* message)
{
    w.BeginObject();
    w.Key("jsonrpc");
    w.String("2.0");
    w.Key("id");
    WriteId(w, id);
    w.Key("error");
    w.BeginObject();
    w.Key("code");
    w.Int(code);
    w.Key("message");
    w.String(message);
    w.End();
    w.End();
}

static void BeginRpcResult(JsonWriter& w, const RpcId& id)
{
    w.BeginObject();
    w.Key("jsonrpc");
    w.String("2.0");
    w.Key("id");
    WriteId(w, id);
    w.Key("result");
}

static void SendRpcError(ServerImpl& impl, const Platform::TcpSocket socket, const int status, const int code, const char* message)
{
    JsonWriter w(impl.allocator, RESPONSE_RESERVE);
    WriteRpcError(w, RpcId{}, code, message);
    SendResponse(socket, status, w.Data(), w.Size());
}

static void WriteToolResponse(JsonWriter& w, const RpcId& id, const Call::Impl& call, const ToolResult outcome)
{
    const bool bError = outcome == ToolResult::Error || call.bError;
    BeginRpcResult(w, id);
    w.BeginObject();
    w.Key("content");
    w.BeginArray();
    w.BeginObject();
    w.Key("type");
    w.String("text");
    w.Key("text");
    if (bError) {
        w.String(call.errorMessage.IsEmpty() ? "Tool failed" : call.errorMessage.c_str());
    }
    else {
        w.String(call.result.Data(), call.result.Size());
    }
    w.End();
    w.End();
    if (!bError) {
        w.Key("structuredContent");
        w.Raw(call.result.Data(), call.result.Size());
    }
    w.Key("isError");
    w.Bool(bError);
    w.End();
    w.End();
}

static void HandleInitialize(const JsonReader& reader, const int32_t params, JsonWriter& w)
{
    const char* requested = reader.GetString(reader.Find(params, "protocolVersion"), "");
    const char* negotiated = strcmp(requested, FALLBACK_PROTOCOL_VERSION) == 0 ? FALLBACK_PROTOCOL_VERSION : PROTOCOL_VERSION;

    w.BeginObject();
    w.Key("protocolVersion");
    w.String(negotiated);
    w.Key("capabilities");
    w.BeginObject();
    w.Key("tools");
    w.BeginObject();
    w.Key("listChanged");
    w.Bool(false);
    w.End();
    w.End();
    w.Key("serverInfo");
    w.BeginObject();
    w.Key("name");
    w.String(SERVER_NAME);
    w.Key("version");
    w.String(SERVER_VERSION);
    w.End();
    w.End();
}

static void HandleToolsList(ServerImpl& impl, JsonWriter& w)
{
    w.BeginObject();
    w.Key("tools");
    w.BeginArray();

    ToolRegistry& r = impl.state->mcpTools;
    std::lock_guard lock(r.mutex);
    for (size_t i = 0; i < r.tools.Size(); ++i) {
        const ToolEntry& e = r.tools[i];
        const char* schema = e.inputSchemaJson ? e.inputSchemaJson : DEFAULT_SCHEMA;
        w.BeginObject();
        w.Key("name");
        w.String(e.name);
        w.Key("description");
        w.String(e.description ? e.description : "");
        w.Key("inputSchema");
        w.Raw(schema, strlen(schema));
        w.End();
    }

    w.End();
    w.End();
}

static void HandleDrainedCall(ServerImpl& impl, const RpcId& id, const StringID toolId, Core::Vector<char>& request, JsonReader& reader, const int32_t argsToken, JsonWriter& w)
{
    auto pending = new(impl.allocator->Alloc(sizeof(PendingCall), Core::AllocTag::MCPServer)) PendingCall(impl.allocator);
    pending->toolId = toolId;
    pending->request = std::move(request);
    pending->reader = std::move(reader);
    pending->call.args = &pending->reader;
    pending->call.argsToken = argsToken;
    pending->callId = impl.nextCallId++;

    impl.queue.enqueue(pending);

    bool bFinished = pending->done.try_acquire_for(DRAIN_WAIT_TIMEOUT);
    if (!bFinished) {
        pending->bAbandoned.store(true, std::memory_order_release);
        bFinished = pending->done.try_acquire();
    }
    if (!bFinished) {
        WriteRpcError(w, id, ERROR_INTERNAL, "Engine thread did not service the call in time; the engine is paused, stalled or shutting down");
    }
    else {
        WriteToolResponse(w, id, pending->call, pending->outcome);
    }
    ReleasePendingCall(impl, pending);
}

static void HandleToolsCall(ServerImpl& impl, const RpcId& id, Core::Vector<char>& request, JsonReader& reader, const int32_t params, JsonWriter& w)
{
    const char* name = reader.GetString(reader.Find(params, "name"), nullptr);
    if (!name) {
        WriteRpcError(w, id, ERROR_INVALID_PARAMS, "Missing tool name");
        return;
    }
    const StringID toolId(Hash(name, strlen(name)));

    ToolEntry entry{};
    {
        ToolRegistry& r = impl.state->mcpTools;
        std::lock_guard lock(r.mutex);
        const size_t* index = r.mapping.Find(toolId);
        if (!index) {
            WriteRpcError(w, id, ERROR_INVALID_PARAMS, "Unknown tool");
            return;
        }
        entry = r.tools[*index];
    }

    const int32_t argsField = reader.Find(params, "arguments");
    const int32_t argsToken = reader.TypeOf(argsField) == JsonType::Object ? argsField : -1;

    if (entry.bNeedsDrain) {
        HandleDrainedCall(impl, id, toolId, request, reader, argsToken, w);
        return;
    }

    Call::Impl callImpl(impl.allocator);
    callImpl.args = &reader;
    callImpl.argsToken = argsToken;
    callImpl.result.BeginObject();
    Call call(&callImpl);
    const ToolResult outcome = entry.invoke(impl.ctx, impl.state, call);
    callImpl.result.End();
    LOG_INFO(MCP, "mcp/{} tool={} result={}", impl.nextCallId++, entry.name, outcome == ToolResult::Complete && !callImpl.bError ? "complete" : "error");
    WriteToolResponse(w, id, callImpl, outcome);
}

/** @return the HTTP status to send; HTTP_ACCEPTED means a notification with no response body. */
static int HandleRpc(ServerImpl& impl, Core::Vector<char>& request, const size_t bodyStart, const size_t bodyLength, JsonWriter& w)
{
    JsonReader reader(impl.allocator);
    if (!reader.Parse(request.Data() + bodyStart, bodyLength)) {
        WriteRpcError(w, RpcId{}, ERROR_PARSE, "Parse error");
        return HTTP_BAD_REQUEST;
    }
    const int32_t root = reader.Root();
    if (reader.TypeOf(root) != JsonType::Object) {
        WriteRpcError(w, RpcId{}, ERROR_INVALID_REQUEST, "Invalid Request");
        return HTTP_BAD_REQUEST;
    }
    const char* method = reader.GetString(reader.Find(root, "method"), nullptr);
    if (!method) {
        WriteRpcError(w, RpcId{}, ERROR_INVALID_REQUEST, "Missing method");
        return HTTP_BAD_REQUEST;
    }

    const JsonToken* idToken = reader.Get(reader.Find(root, "id"));
    RpcId id{};
    if (!idToken || idToken->type == JsonType::Null) {
        return HTTP_ACCEPTED;
    }
    if (idToken->type == JsonType::Number) {
        id = {RpcId::Kind::Number, idToken->text, idToken->length};
    }
    else if (idToken->type == JsonType::String) {
        id = {RpcId::Kind::String, idToken->text, idToken->length};
    }
    else {
        WriteRpcError(w, RpcId{}, ERROR_INVALID_REQUEST, "id must be a string or number");
        return HTTP_BAD_REQUEST;
    }

    const int32_t params = reader.Find(root, "params");

    if (strcmp(method, "initialize") == 0) {
        BeginRpcResult(w, id);
        HandleInitialize(reader, params, w);
        w.End();
        return HTTP_OK;
    }
    if (strcmp(method, "ping") == 0) {
        BeginRpcResult(w, id);
        w.BeginObject();
        w.End();
        w.End();
        return HTTP_OK;
    }
    if (strcmp(method, "tools/list") == 0) {
        BeginRpcResult(w, id);
        HandleToolsList(impl, w);
        w.End();
        return HTTP_OK;
    }
    if (strcmp(method, "tools/call") == 0) {
        if (reader.TypeOf(params) != JsonType::Object) {
            WriteRpcError(w, id, ERROR_INVALID_PARAMS, "Missing params");
            return HTTP_OK;
        }
        HandleToolsCall(impl, id, request, reader, params, w);
        return HTTP_OK;
    }

    WriteRpcError(w, id, ERROR_METHOD_NOT_FOUND, "Method not found");
    return HTTP_OK;
}

static bool FindHeaderEnd(const Core::Vector<char>& buffer, const size_t scanFrom, size_t& outHeaderEnd)
{
    const char* data = buffer.Data();
    for (size_t i = scanFrom; i + 3 < buffer.Size(); ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            outHeaderEnd = i;
            return true;
        }
    }
    return false;
}

static bool RecvChunk(const Platform::TcpSocket socket, Core::Vector<char>& buffer)
{
    const size_t old = buffer.Size();
    buffer.Resize(old + RECV_CHUNK);
    const int32_t received = Platform::TcpRecv(socket, buffer.Data() + old, RECV_CHUNK);
    if (received <= 0) {
        buffer.Resize(old);
        return false;
    }
    buffer.Resize(old + static_cast<size_t>(received));
    return true;
}

static bool RecvHeader(const Platform::TcpSocket socket, Core::Vector<char>& buffer, size_t& outHeaderEnd)
{
    while (true) {
        const size_t scanFrom = buffer.Size() >= 3 ? buffer.Size() - 3 : 0;
        if (!RecvChunk(socket, buffer)) { return false; }
        if (FindHeaderEnd(buffer, scanFrom, outHeaderEnd)) { return true; }
        if (buffer.Size() > MAX_HEADER_BYTES) { return false; }
    }
}

static bool RecvBody(const Platform::TcpSocket socket, Core::Vector<char>& buffer, const size_t bodyStart, const size_t contentLength)
{
    while (buffer.Size() - bodyStart < contentLength) {
        if (!RecvChunk(socket, buffer)) { return false; }
    }
    return true;
}

static bool HeaderIs(const char* name, const char* expected)
{
    for (;; ++name, ++expected) {
        const int a = tolower(static_cast<unsigned char>(*name));
        const int b = tolower(static_cast<unsigned char>(*expected));
        if (a != b) { return false; }
        if (a == 0) { return true; }
    }
}

static char* NextLine(char*& cursor)
{
    char* line = cursor;
    char* end = strstr(cursor, "\r\n");
    if (!end) {
        cursor = nullptr;
        return line;
    }
    *end = '\0';
    cursor = end + 2;
    return line;
}

static bool ParseHttpHeader(char* text, const size_t headerEnd, HttpRequest& out)
{
    text[headerEnd] = '\0';
    char* cursor = text;
    char* requestLine = NextLine(cursor);

    out.method = requestLine;
    char* space = strchr(requestLine, ' ');
    if (!space) { return false; }
    *space = '\0';
    char* path = space + 1;
    space = strchr(path, ' ');
    if (!space) { return false; }
    *space = '\0';
    if (char* query = strchr(path, '?')) { *query = '\0'; }
    out.path = path;

    while (cursor && *cursor) {
        char* line = NextLine(cursor);
        char* colon = strchr(line, ':');
        if (!colon) { continue; }
        *colon = '\0';
        char* value = colon + 1;
        while (*value == ' ' || *value == '\t') { ++value; }
        if (HeaderIs(line, "Content-Length")) {
            out.contentLength = strtoull(value, nullptr, 10);
        }
        else if (HeaderIs(line, "Transfer-Encoding")) {
            out.bChunked = strstr(value, "chunked") != nullptr;
        }
        else if (HeaderIs(line, "Expect")) {
            out.bExpectContinue = HeaderIs(value, "100-continue");
        }
    }
    return true;
}

static void ServeConnection(ServerImpl& impl, const Platform::TcpSocket socket)
{
    ZoneScoped;
    Core::Vector<char> buffer(impl.allocator, Core::AllocTag::MCPServer, RECV_CHUNK);

    size_t headerEnd = 0;
    if (!RecvHeader(socket, buffer, headerEnd)) {
        SendRpcError(impl, socket, HTTP_BAD_REQUEST, ERROR_INVALID_REQUEST, "Malformed or oversized HTTP header");
        return;
    }

    HttpRequest request{};
    if (!ParseHttpHeader(buffer.Data(), headerEnd, request)) {
        SendRpcError(impl, socket, HTTP_BAD_REQUEST, ERROR_INVALID_REQUEST, "Malformed request line");
        return;
    }
    if (strcmp(request.path, ENDPOINT) != 0) {
        SendRpcError(impl, socket, HTTP_NOT_FOUND, ERROR_INVALID_REQUEST, "Not found");
        return;
    }
    if (strcmp(request.method, "POST") != 0) {
        SendRpcError(impl, socket, HTTP_METHOD_NOT_ALLOWED, ERROR_METHOD_NOT_FOUND, "Only POST is supported");
        return;
    }
    if (request.bChunked) {
        SendRpcError(impl, socket, HTTP_LENGTH_REQUIRED, ERROR_INVALID_REQUEST, "Chunked bodies are not supported; send Content-Length");
        return;
    }
    if (request.contentLength > MAX_BODY_BYTES) {
        SendRpcError(impl, socket, HTTP_PAYLOAD_TOO_LARGE, ERROR_INVALID_REQUEST, "Body too large");
        return;
    }
    if (request.bExpectContinue) {
        static constexpr const char* CONTINUE = "HTTP/1.1 100 Continue\r\n\r\n";
        Platform::TcpSendAll(socket, CONTINUE, strlen(CONTINUE));
    }

    const size_t bodyStart = headerEnd + 4;
    if (!RecvBody(socket, buffer, bodyStart, request.contentLength)) {
        SendRpcError(impl, socket, HTTP_BAD_REQUEST, ERROR_INVALID_REQUEST, "Body shorter than Content-Length");
        return;
    }

    JsonWriter response(impl.allocator, RESPONSE_RESERVE);
    const int status = HandleRpc(impl, buffer, bodyStart, request.contentLength, response);
    SendResponse(socket, status, response.Data(), status == HTTP_ACCEPTED ? 0 : response.Size());
}

MCPServer::MCPServer(Core::MemoryManager& memoryManager_)
    : memoryManager(memoryManager_)
{
    Platform::TcpStartup();

    impl = new(memoryManager.PersistentAllocRaw(sizeof(ServerImpl), Core::AllocTag::MCPServer)) ServerImpl();
    impl->allocator = &memoryManager.General();
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
    Platform::TcpCleanup();
}

void MCPServer::Start(const int32_t port, EngineContext* ctx, EngineState* state)
{
    impl->ctx = ctx;
    impl->state = state;
    bShouldExit.store(false, std::memory_order_release);
    boundPort = port;
    thisThread = std::jthread([this, port] { ThreadMain(port); });
}

void MCPServer::Drain(EngineContext* ctx, EngineState* state) const
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
        pending->call.result.BeginObject();
        pending->call.result.Key("callId");
        pending->call.result.Int(static_cast<int64_t>(pending->callId));
        pending->call.result.Key("frame");
        pending->call.result.Int(static_cast<int64_t>(pending->frame));
        if (!bFound) {
            call.SetError("Tool is no longer registered; the game DLL was reloaded between dispatch and drain");
            pending->outcome = ToolResult::Error;
        }
        else {
            if (pending->call.argsToken >= 0) {
                JsonWriter args(impl->allocator, ARGS_LOG_RESERVE);
                pending->reader.Write(pending->call.argsToken, args);
                LOG_INFO(MCP, "mcp/{} begin frame={} tool={} args={}", pending->callId, pending->frame, entry.name, std::string_view(args.Data(), args.Size()));
            }
            else {
                LOG_INFO(MCP, "mcp/{} begin frame={} tool={}", pending->callId, pending->frame, entry.name);
            }
            pending->outcome = entry.invoke(ctx, state, call);
            LOG_INFO(MCP, "mcp/{} end frame={} result={}", pending->callId, pending->frame, pending->outcome == ToolResult::Complete && !pending->call.bError ? "complete" : "error");
        }
        pending->call.result.End();

        pending->done.release();
        ReleasePendingCall(*impl, pending);
    }
}

static void CloseListenSocket(ServerImpl& impl)
{
    Platform::TcpClose(impl.listenSocket.exchange(Platform::INVALID_TCP_SOCKET));
}

static void FailQueuedCalls(ServerImpl& impl)
{
    PendingCall* pending{};
    while (impl.queue.try_dequeue(pending)) {
        Call call(&pending->call);
        pending->call.result.BeginObject();
        call.SetError("Engine is shutting down");
        pending->call.result.End();
        pending->outcome = ToolResult::Error;
        pending->done.release();
        ReleasePendingCall(impl, pending);
    }
}

void MCPServer::RequestShutdown()
{
    bShouldExit.store(true, std::memory_order_release);
    if (impl) {
        CloseListenSocket(*impl);
        FailQueuedCalls(*impl);
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

    int error = 0;
    const Platform::TcpSocket listenSocket = Platform::TcpListenLoopback(static_cast<uint16_t>(port), LISTEN_BACKLOG, error);
    if (listenSocket == Platform::INVALID_TCP_SOCKET) {
        LOG_ERROR(MCP, "Failed to listen on 127.0.0.1:{} (error {}), server disabled for this run", port, error);
        return;
    }

    impl->listenSocket.store(listenSocket);
    if (bShouldExit.load(std::memory_order_acquire)) {
        CloseListenSocket(*impl);
        return;
    }

    LOG_INFO(MCP, "Listening on http://127.0.0.1:{}{}", port, ENDPOINT);
    bListening.store(true, std::memory_order_release);

    while (!bShouldExit.load(std::memory_order_acquire)) {
        Platform::TcpSocket client = Platform::INVALID_TCP_SOCKET;
        const Platform::TcpAcceptResult accepted = Platform::TcpAccept(listenSocket, SOCKET_TIMEOUT_MS, client);
        if (accepted == Platform::TcpAcceptResult::Closed || bShouldExit.load(std::memory_order_acquire)) {
            Platform::TcpClose(client);
            break;
        }
        if (accepted == Platform::TcpAcceptResult::Retry) {
            continue;
        }

        ServeConnection(*impl, client);
        Platform::TcpClose(client);
    }

    bListening.store(false, std::memory_order_release);
    CloseListenSocket(*impl);
    LOG_INFO(MCP, "Stopped");
}
} // Engine::MCP

#endif // WILL_EDITOR
