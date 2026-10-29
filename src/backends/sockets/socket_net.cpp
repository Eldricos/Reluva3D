// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sockets/socket_net.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
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

bool systemHttpExchange(const Address& to, const std::string& request, std::string& response, uint32_t& localIp,
                        int timeoutMs) {
    response.clear();
    if (!startSockets())
        return false;
    const SocketHandle s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kNoSocket)
        return false;
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(to.ip);
    a.sin_port = htons(to.port);
    connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a);
    const auto start = std::chrono::steady_clock::now();
    auto left = [&] {
        const auto spent =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        return std::max<long long>(0, timeoutMs - spent);
    };
    // Waits until it can write (connected) or read.
    auto wait = [&](bool writing) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval tv{};
        const long long ms = left();
        tv.tv_sec = long(ms / 1000);
        tv.tv_usec = long(ms % 1000 * 1000);
        return select(int(s) + 1, writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &tv) > 0;
    };
    bool ok = wait(true);
    if (ok) {
        int err = 0;
        socklen_t length = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &length);
        ok = err == 0;
    }
    for (size_t sent = 0; ok && sent < request.size();) {
        const auto n = ::send(s, request.data() + sent, int(request.size() - sent), 0);
        if (n > 0)
            sent += size_t(n);
        else if (!wait(true))
            ok = false;
    }
    if (ok) {
        sockaddr_in local{};
        socklen_t length = sizeof local;
        getsockname(s, reinterpret_cast<sockaddr*>(&local), &length);
        localIp = ntohl(local.sin_addr.s_addr);
    }
    char buffer[4096];
    while (ok && response.size() < (1u << 20) && left() > 0) {
        if (!wait(false))
            break;
        const auto n = recv(s, buffer, int(sizeof buffer), 0);
        if (n <= 0)
            break; // closed (or failed)
        response.append(buffer, size_t(n));
    }
    closeSocket(s);
    return ok && !response.empty();
}

std::vector<uint32_t> systemGateways() {
    std::vector<uint32_t> out;
#ifdef _WIN32
    if (!startSockets())
        return out;
    ULONG size = 16384;
    std::vector<uint8_t> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && result == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr,
                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR)
        return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next)
        for (auto* g = a->FirstGatewayAddress; g && a->OperStatus == IfOperStatusUp; g = g->Next)
            if (g->Address.lpSockaddr->sa_family == AF_INET)
                out.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(g->Address.lpSockaddr)->sin_addr.s_addr));
#elif defined(__linux__)
    // Each route: interface, destination, gateway (hex, in the machine's
    // byte order), flags...; the default route's destination is 0.
    if (FILE* f = std::fopen("/proc/net/route", "r")) {
        char line[256];
        bool heading = true;
        while (std::fgets(line, sizeof line, f)) {
            if (std::exchange(heading, false))
                continue;
            char name[64];
            unsigned destination = 0, gateway = 0, flags = 0;
            if (std::sscanf(line, "%63s %x %x %x", name, &destination, &gateway, &flags) == 4 && destination == 0 &&
                gateway && (flags & 2))
                out.push_back(ntohl(gateway));
        }
        std::fclose(f);
    }
#endif
    return out;
}

std::vector<uint32_t> systemBroadcastAddresses() {
    std::vector<uint32_t> out;
    auto add = [&](uint32_t b) {
        if (b && b != 0xffffffffu && std::find(out.begin(), out.end(), b) == out.end())
            out.push_back(b);
    };
#ifdef _WIN32
    if (!startSockets())
        return out;
    ULONG size = 16384;
    std::vector<uint8_t> buffer;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && result == ERROR_BUFFER_OVERFLOW; ++tries) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET,
                                      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR)
        return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET || u->OnLinkPrefixLength == 0 ||
                u->OnLinkPrefixLength >= 32)
                continue;
            const uint32_t ip = ntohl(reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
            const uint32_t mask = 0xffffffffu << (32 - u->OnLinkPrefixLength);
            add(ip | ~mask);
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0)
        return out;
    for (ifaddrs* i = list; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && (i->ifa_flags & IFF_BROADCAST) &&
            (i->ifa_flags & IFF_UP) && i->ifa_broadaddr)
            add(ntohl(reinterpret_cast<const sockaddr_in*>(i->ifa_broadaddr)->sin_addr.s_addr));
    freeifaddrs(list);
#endif
    return out;
}

} // namespace reluva::net
