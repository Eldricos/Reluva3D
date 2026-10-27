// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sockets/socket_net.h"

#include <algorithm>
#include <map>
#include <mutex>

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
#include <netinet/tcp.h>
#include <sys/ioctl.h>
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


// The system's sockets by number (Sockets): all non-blocking.
class SystemSockets : public Sockets {
public:
    ~SystemSockets() override {
        for (const auto& [n, s] : sockets_)
            closeSocket(s);
    }

    int open(Type type) override {
        if (!startSockets())
            return -1;
        const SocketHandle s = socket(AF_INET, type == Stream ? SOCK_STREAM : SOCK_DGRAM,
                                      type == Stream ? IPPROTO_TCP : IPPROTO_UDP);
        if (s == kNoSocket)
            return -1;
        return add(s);
    }

    void close(int n) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sockets_.find(n);
        if (it == sockets_.end())
            return;
        closeSocket(it->second);
        sockets_.erase(it);
    }

    Result bind(int n, const Address& at) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        const sockaddr_in a = address(at);
        return ::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0 ? Ok : error();
    }

    Result listen(int n, int backlog) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        return ::listen(s, backlog) == 0 ? Ok : error();
    }

    Result accept(int n, int& accepted, Address& from) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        sockaddr_in a{};
        socklen_t length = sizeof a;
        const SocketHandle got = ::accept(s, reinterpret_cast<sockaddr*>(&a), &length);
        if (got == kNoSocket)
            return error();
        from = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
        accepted = add(got);
        return Ok;
    }

    Result connect(int n, const Address& to) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        const sockaddr_in a = address(to);
        if (::connect(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0)
            return Ok;
        const Result r = error();
        return r == WouldBlock ? InProgress : r;
    }

    Result send(int n, const uint8_t* data, size_t size, size_t& done) override {
        done = 0;
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        const auto sent = ::send(s, reinterpret_cast<const char*>(data), int(size), kNoSignal);
        if (sent < 0)
            return error();
        done = size_t(sent);
        return Ok;
    }

    Result receive(int n, uint8_t* data, size_t size, size_t& done, bool peek) override {
        done = 0;
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        const auto got = ::recv(s, reinterpret_cast<char*>(data), int(size), peek ? MSG_PEEK : 0);
        if (got < 0)
            return error();
        done = size_t(got);
        return Ok;
    }

    Result sendTo(int n, const Address& to, const uint8_t* data, size_t size) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        const sockaddr_in a = address(to);
        const auto sent = ::sendto(s, reinterpret_cast<const char*>(data), int(size), kNoSignal,
                                   reinterpret_cast<const sockaddr*>(&a), sizeof a);
        return sent >= 0 ? Ok : error();
    }

    Result receiveFrom(int n, uint8_t* data, size_t size, size_t& done, Address& from, bool peek) override {
        done = 0;
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        sockaddr_in a{};
        socklen_t length = sizeof a;
        const auto got = ::recvfrom(s, reinterpret_cast<char*>(data), int(size), peek ? MSG_PEEK : 0,
                                    reinterpret_cast<sockaddr*>(&a), &length);
        if (got < 0) {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEMSGSIZE) { // cut short, as Winsock does it
                done = size;
                from = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
                return Ok;
            }
#endif
            return error();
        }
        done = size_t(got);
        from = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
        return Ok;
    }

    Result setOption(int n, Option option, int value) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        int level = SOL_SOCKET, name = 0;
        switch (option) {
        case Broadcast: name = SO_BROADCAST; break;
        case NoDelay: level = IPPROTO_TCP, name = TCP_NODELAY; break;
        case ReuseAddress: name = SO_REUSEADDR; break;
        case KeepAlive: name = SO_KEEPALIVE; break;
        case ReceiveBuffer: name = SO_RCVBUF; break;
        case SendBuffer: name = SO_SNDBUF; break;
        case Linger: {
            linger l{};
            l.l_onoff = value >= 0;
            l.l_linger = decltype(l.l_linger)(std::max(value, 0));
            return setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&l), sizeof l) == 0 ? Ok : error();
        }
        }
        return setsockopt(s, level, name, reinterpret_cast<const char*>(&value), sizeof value) == 0 ? Ok : error();
    }

    Result shutdown(int n, int how) override {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        return ::shutdown(s, how) == 0 ? Ok : error();
    }

    Result localAddress(int n, Address& out) override { return name(n, out, false); }
    Result peerAddress(int n, Address& out) override { return name(n, out, true); }

    Result pending(int n, uint32_t& bytes) override {
        bytes = 0;
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
#ifdef _WIN32
        u_long count = 0;
        if (ioctlsocket(s, FIONREAD, &count) != 0)
            return error();
#else
        int count = 0;
        if (ioctl(s, FIONREAD, &count) != 0)
            return error();
#endif
        bytes = uint32_t(count);
        return Ok;
    }

    Ready ready(int n, int timeoutMs) override {
        Ready r;
        SocketHandle s;
        if (!find(n, s)) {
            r.failed = true;
            return r;
        }
        fd_set read, write, failed;
        FD_ZERO(&read);
        FD_ZERO(&write);
        FD_ZERO(&failed);
        FD_SET(s, &read);
        FD_SET(s, &write);
        FD_SET(s, &failed);
        timeval t{};
        t.tv_sec = timeoutMs / 1000;
        t.tv_usec = (timeoutMs % 1000) * 1000;
        if (::select(int(s) + 1, &read, &write, &failed, &t) <= 0)
            return r;
        r.read = FD_ISSET(s, &read);
        r.write = FD_ISSET(s, &write);
        r.failed = FD_ISSET(s, &failed);
        // A connect that failed shows as writable with an error waiting.
        int pendingError = 0;
        socklen_t length = sizeof pendingError;
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pendingError), &length) == 0 && pendingError) {
            r.failed = true;
            r.write = false;
        }
        return r;
    }

    std::string hostName() override {
        if (!startSockets())
            return "localhost";
        char name[256] = {};
        if (gethostname(name, sizeof name - 1) != 0)
            return "localhost";
        return name;
    }

    bool resolve(const std::string& host, uint32_t& ip) override { return resolveSystemHost(host, ip); }

private:
#ifdef _WIN32
    static constexpr int kNoSignal = 0;
#else
    static constexpr int kNoSignal = MSG_NOSIGNAL;
#endif
    std::mutex mutex_;
    std::map<int, SocketHandle> sockets_;
    int next_ = 1;

    int add(SocketHandle s) {
#ifdef _WIN32
        u_long nonBlocking = 1;
        ioctlsocket(s, FIONBIO, &nonBlocking);
#else
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[next_] = s;
        return next_++;
    }

    bool find(int n, SocketHandle& s) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sockets_.find(n);
        if (it == sockets_.end())
            return false;
        s = it->second;
        return true;
    }

    static sockaddr_in address(const Address& at) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(at.ip);
        a.sin_port = htons(at.port);
        return a;
    }

    Result name(int n, Address& out, bool peer) {
        SocketHandle s;
        if (!find(n, s))
            return Invalid;
        sockaddr_in a{};
        socklen_t length = sizeof a;
        const int r = peer ? getpeername(s, reinterpret_cast<sockaddr*>(&a), &length)
                           : getsockname(s, reinterpret_cast<sockaddr*>(&a), &length);
        if (r != 0)
            return error();
        out = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
        return Ok;
    }

    // The last call's error, as a Result.
    static Result error() {
#ifdef _WIN32
        switch (WSAGetLastError()) {
        case WSAEWOULDBLOCK: return WouldBlock;
        case WSAEINPROGRESS: case WSAEALREADY: return InProgress;
        case WSAECONNREFUSED: return Refused;
        case WSAECONNRESET: case WSAECONNABORTED: case WSAENETRESET: return Reset;
        case WSAENOTCONN: return NotConnected;
        case WSAEADDRINUSE: return AddressInUse;
        case WSAEADDRNOTAVAIL: return AddressNotAvailable;
        case WSAENETUNREACH: case WSAEHOSTUNREACH: return Unreachable;
        case WSAEINVAL: case WSAENOTSOCK: case WSAEISCONN: return Invalid;
        default: return Failed;
        }
#else
        switch (errno) {
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return WouldBlock;
        case EINPROGRESS: case EALREADY: return InProgress;
        case ECONNREFUSED: return Refused;
        case ECONNRESET: case ECONNABORTED: case EPIPE: return Reset;
        case ENOTCONN: return NotConnected;
        case EADDRINUSE: return AddressInUse;
        case EADDRNOTAVAIL: return AddressNotAvailable;
        case ENETUNREACH: case EHOSTUNREACH: return Unreachable;
        case EINVAL: case ENOTSOCK: case EISCONN: case EBADF: return Invalid;
        default: return Failed;
        }
#endif
    }
};

} // namespace

std::shared_ptr<Sockets> systemSockets() { return std::make_shared<SystemSockets>(); }

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
