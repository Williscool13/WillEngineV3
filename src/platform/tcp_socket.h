//
// Created by William on 2026-09-05.
//

#ifndef WILL_ENGINE_TCP_SOCKET_H
#define WILL_ENGINE_TCP_SOCKET_H

#include <cstddef>
#include <cstdint>

namespace Platform
{
using TcpSocket = uintptr_t;
inline constexpr TcpSocket INVALID_TCP_SOCKET = ~static_cast<uintptr_t>(0);

enum class TcpAcceptResult : uint8_t
{
    Accepted,
    Retry,
    Closed,
};

bool TcpStartup();

void TcpCleanup();

TcpSocket TcpListenLoopback(uint16_t port, int backlog, int& outError);

TcpAcceptResult TcpAccept(TcpSocket listenSocket, uint32_t timeoutMs, TcpSocket& outClient);

int32_t TcpRecv(TcpSocket socket, char* buffer, size_t capacity);

bool TcpSendAll(TcpSocket socket, const char* data, size_t length);

void TcpClose(TcpSocket socket);
} // Platform

#endif //WILL_ENGINE_TCP_SOCKET_H
