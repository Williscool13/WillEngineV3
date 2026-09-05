//
// Created by William on 2026-09-05.
//

#include "tcp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

namespace Platform
{
static SOCKET ToNative(const TcpSocket socket)
{
    return static_cast<SOCKET>(socket);
}

bool TcpStartup()
{
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

void TcpCleanup()
{
    WSACleanup();
}

TcpSocket TcpListenLoopback(const uint16_t port, const int backlog, int& outError)
{
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        outError = WSAGetLastError();
        return INVALID_TCP_SOCKET;
    }

    const BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(s, backlog) != 0) {
        outError = WSAGetLastError();
        closesocket(s);
        return INVALID_TCP_SOCKET;
    }

    outError = 0;
    return static_cast<TcpSocket>(s);
}

TcpAcceptResult TcpAccept(const TcpSocket listenSocket, const uint32_t timeoutMs, TcpSocket& outClient)
{
    sockaddr_in peer{};
    int peerLength = sizeof(peer);
    const SOCKET client = accept(ToNative(listenSocket), reinterpret_cast<sockaddr*>(&peer), &peerLength);
    if (client == INVALID_SOCKET) {
        const int error = WSAGetLastError();
        outClient = INVALID_TCP_SOCKET;
        return error == WSAENOTSOCK || error == WSAEINTR || error == WSAEINVAL ? TcpAcceptResult::Closed : TcpAcceptResult::Retry;
    }

    const DWORD timeout = timeoutMs;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    outClient = static_cast<TcpSocket>(client);
    return TcpAcceptResult::Accepted;
}

int32_t TcpRecv(const TcpSocket socket, char* buffer, const size_t capacity)
{
    return recv(ToNative(socket), buffer, static_cast<int>(capacity), 0);
}

bool TcpSendAll(const TcpSocket socket, const char* data, size_t length)
{
    while (length > 0) {
        const int sent = send(ToNative(socket), data, static_cast<int>(length), 0);
        if (sent <= 0) { return false; }
        data += sent;
        length -= static_cast<size_t>(sent);
    }
    return true;
}

void TcpClose(const TcpSocket socket)
{
    if (socket == INVALID_TCP_SOCKET) { return; }
    shutdown(ToNative(socket), SD_SEND);
    closesocket(ToNative(socket));
}
} // Platform
