// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sockets/socket_net.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kNoSocket = -1;
#endif

namespace reluva::net {

namespace {

#ifdef _WIN32
// Winsock is started once, the first time a socket is wanted.
bool startSockets() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}
void closeSocket(SocketHandle s) { closesocket(s); }
std::string lastError() { return "Winsock error " + std::to_string(WSAGetLastError()); }
#else
bool startSockets() { return true; }
void closeSocket(SocketHandle s) { close(s); }
std::string lastError() { return std::strerror(errno); }
#endif

class SystemSocket : public UdpSocket {
public:
    SystemSocket(SocketHandle s, uint16_t port) : s_(s), port_(port) {}
    ~SystemSocket() override { closeSocket(s_); }
    SystemSocket(const SystemSocket&) = delete;
    SystemSocket& operator=(const SystemSocket&) = delete;

    uint16_t port() const override { return port_; }

    bool send(const Address& to, const std::vector<uint8_t>& data) override {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(to.ip);
        a.sin_port = htons(to.port);
        const auto sent = sendto(s_, reinterpret_cast<const char*>(data.data()), int(data.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&a), sizeof a);
        return sent >= 0 && size_t(sent) == data.size();
    }

    bool receive(Address& from, std::vector<uint8_t>& data) override {
        // Datagrams that can't be read (a port that refused an earlier
        // one, on Windows) are skipped.
        for (int tries = 0; tries < 16; ++tries) {
            sockaddr_in a{};
            socklen_t length = sizeof a;
            data.resize(65536);
            const auto got = recvfrom(s_, reinterpret_cast<char*>(data.data()), int(data.size()), 0,
                                      reinterpret_cast<sockaddr*>(&a), &length);
            if (got < 0) {
#ifdef _WIN32
                if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAEMSGSIZE)
                    continue;
#endif
                data.clear();
                return false;
            }
            data.resize(size_t(got));
            from = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
            return true;
        }
        data.clear();
        return false;
    }

private:
    SocketHandle s_;
    uint16_t port_;
};

} // namespace

std::unique_ptr<UdpSocket> openSystemUdp(uint16_t port, std::string& error) {
    if (!startSockets()) {
        error = "Cannot start the network";
        return nullptr;
    }
    const SocketHandle s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kNoSocket) {
        error = "Cannot open a socket: " + lastError();
        return nullptr;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    const int on = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
        error = "Cannot use port " + std::to_string(port) + ": " + lastError();
        closeSocket(s);
        return nullptr;
    }
    socklen_t length = sizeof a;
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &length);
    return std::make_unique<SystemSocket>(s, ntohs(a.sin_port));
}

bool resolveSystemHost(const std::string& host, uint32_t& ip) {
    Address parsed;
    if (parseAddress(host, 0, parsed)) {
        ip = parsed.ip;
        return true;
    }
    if (!startSockets())
        return false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || !found)
        return false;
    ip = ntohl(reinterpret_cast<const sockaddr_in*>(found->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(found);
    return true;
}

} // namespace reluva::net
