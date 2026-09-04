//
// Created by William on 2026-09-04.
//

#ifndef WILL_ENGINE_MCP_SERVER_H
#define WILL_ENGINE_MCP_SERVER_H

#if WILL_EDITOR

#include <atomic>
#include <cstdint>
#include <thread>

namespace Core
{
class MemoryManager;
}

namespace Engine::MCP
{
inline constexpr int32_t DEFAULT_PORT = 8787;

struct ServerImpl;

/**
 * Localhost-only JSON-RPC listener speaking the MCP wire protocol.
 * Owns a dedicated thread outside the enkiTS pool; never registered as an external task thread.
 */
class MCPServer
{
public:
    explicit MCPServer(Core::MemoryManager& memoryManager_);

    ~MCPServer();

    MCPServer(const MCPServer&) = delete;
    MCPServer& operator=(const MCPServer&) = delete;

    /** Binds 127.0.0.1 only. Bind failure is logged, not fatal; check IsListening. */
    void Start(int32_t port);

    void RequestShutdown();

    void Join();

    [[nodiscard]] bool IsListening() const { return bListening.load(std::memory_order_acquire); }

    [[nodiscard]] int32_t GetPort() const { return boundPort; }

private:
    void ThreadMain(int32_t port);

    Core::MemoryManager& memoryManager;
    ServerImpl* impl{};
    std::atomic<bool> bShouldExit{false};
    std::atomic<bool> bListening{false};
    int32_t boundPort{0};
    std::jthread thisThread;
};
} // Engine::MCP

#endif // WILL_EDITOR

#endif //WILL_ENGINE_MCP_SERVER_H
