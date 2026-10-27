// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WSOCK32, Windows Sockets 1.1, as nfs3.exe imports it (by ordinal), on
// the network the host is given (Options::sockets, net::Sockets). From
// the Windows Sockets 1.1 specification: its structures (sockaddr_in,
// fd_set, hostent, WSADATA) in the game's memory, its error numbers, and
// its blocking: a socket blocks unless the game makes it non-blocking
// (FIONBIO), and a blocked call lets the game's other threads run while it
// waits. Without a network, WSAStartup fails as before and the game offers
// no network play.

#include "engines/nfs/host/game_host.h"

#include <algorithm>
#include <cstdio>

namespace reluva::nfs {

namespace {

using x86::Cpu;
using Result = net::Sockets::Result;

constexpr uint32_t kSocketError = 0xffffffffu, kInfinite = 0xffffffffu;
// Winsock's error numbers.
constexpr uint32_t kEINTR = 10004, kEFAULT = 10014, kEINVAL = 10022, kEMFILE = 10024, kEWOULDBLOCK = 10035,
                   kENOTSOCK = 10038, kEPROTONOSUPPORT = 10043, kEAFNOSUPPORT = 10047, kEADDRINUSE = 10048,
                   kEADDRNOTAVAIL = 10049, kENETUNREACH = 10051, kECONNRESET = 10054, kENOTCONN = 10057,
                   kECONNREFUSED = 10061, kNOTINITIALISED = 10093, kSYSNOTREADY = 10091, kHOST_NOT_FOUND = 11001;

uint32_t wsError(Result r) {
    switch (r) {
    case Result::Ok: return 0;
    case Result::WouldBlock: return kEWOULDBLOCK;
    case Result::InProgress: return kEWOULDBLOCK; // Winsock 1.1's for a non-blocking connect
    case Result::Refused: return kECONNREFUSED;
    case Result::Reset: return kECONNRESET;
    case Result::NotConnected: return kENOTCONN;
    case Result::AddressInUse: return kEADDRINUSE;
    case Result::AddressNotAvailable: return kEADDRNOTAVAIL;
    case Result::Unreachable: return kENETUNREACH;
    case Result::Invalid: return kEINVAL;
    case Result::Failed: return kEFAULT;
    }
    return kEFAULT;
}

// sockaddr_in: family (2), port and address in network order.
bool readAddress(Cpu& c, uint32_t at, uint32_t length, net::Address& out) {
    if (!at || length < 8 || c.read16(at) != 2) // AF_INET
        return false;
    out.port = uint16_t(c.read8(at + 2) << 8 | c.read8(at + 3));
    out.ip = uint32_t(c.read8(at + 4)) << 24 | uint32_t(c.read8(at + 5)) << 16 | uint32_t(c.read8(at + 6)) << 8 |
             c.read8(at + 7);
    return true;
}

void writeAddress(Cpu& c, uint32_t at, uint32_t lengthAt, const net::Address& a) {
    if (!at)
        return;
    if (lengthAt && c.read32(lengthAt) < 16)
        return;
    c.write16(at, 2);
    c.write8(at + 2, uint8_t(a.port >> 8));
    c.write8(at + 3, uint8_t(a.port));
    c.write8(at + 4, uint8_t(a.ip >> 24));
    c.write8(at + 5, uint8_t(a.ip >> 16));
    c.write8(at + 6, uint8_t(a.ip >> 8));
    c.write8(at + 7, uint8_t(a.ip));
    for (uint32_t i = 8; i < 16; ++i)
        c.write8(at + i, 0);
    if (lengthAt)
        c.write32(lengthAt, 16);
}

} // namespace

void GameHost::addNetwork() {
    const char* w = "WSOCK32.dll";
    net::Sockets* net = options_.sockets.get();
    auto self = [this] { return process_->currentThread() ? process_->currentThread()->id : 0u; };
    auto fail = [this, self](uint32_t error) {
        wsLastError_[self()] = error;
        return kSocketError;
    };
    auto find = [this](uint32_t handle) -> WsSocket* {
        auto it = wsSockets_.find(handle);
        return it == wsSockets_.end() ? nullptr : &it->second;
    };
    // The address the game gives, with "any" as Options::netAddress.
    auto ours = [this](net::Address a) {
        if (!a.ip && options_.netAddress)
            a.ip = options_.netAddress;
        return a;
    };
    // A socket not yet bound, given Options::netAddress before it's used,
    // so what it sends comes from there.
    auto bindIfNeeded = [this, net](WsSocket& s) {
        if (s.bound || !options_.netAddress)
            return;
        net->bind(s.socket, {options_.netAddress, 0});
        s.bound = true;
    };
    // Calls again while it would block, if the socket blocks: waiting for
    // it to be readable (or writable), the game's other threads running.
    // (By the socket's number, in case another thread closes it meanwhile.)
    auto blocking = [this, net](const WsSocket& s, bool forWrite, const std::function<Result()>& call) {
        const int socket = s.socket;
        const bool nonBlocking = s.nonBlocking;
        Result r = call();
        while (!nonBlocking && (r == Result::WouldBlock || r == Result::InProgress)) {
            process_->wait(
                [net, socket, forWrite] {
                    const net::Sockets::Ready ready = net->ready(socket);
                    return ready.failed || (forWrite ? ready.write : ready.read);
                },
                kInfinite);
            if (process_->ending())
                return Result::Failed;
            r = call();
        }
        return r;
    };
    auto log = [this](const std::string& line) {
        if (options_.callLog)
            say("  winsock: " + line);
    };

    // WSAStartup(version, WSADATA*): version 1.1, if there's a network.
    provide(w, "#115", 2, [this, net](Cpu& c) {
        if (!net)
            return kSYSNOTREADY;
        const uint32_t d = c.arg(1);
        if (d) {
            for (uint32_t i = 0; i < 400; i += 4)
                c.write32(d + i, 0);
            c.write16(d, 0x0101);
            c.write16(d + 2, 0x0101);
            c.writeString(d + 4, "Reluva Windows Sockets", 257);
            c.writeString(d + 261, "Running", 129);
            c.write16(d + 390, 256);   // iMaxSockets
            c.write16(d + 392, 65467); // iMaxUdpDg
        }
        wsStarted_ = true;
        return 0u;
    });
    provide(w, "#116", 0, [this, net](Cpu&) { // WSACleanup: the sockets left closed
        for (const auto& [handle, s] : wsSockets_)
            net->close(s.socket);
        wsSockets_.clear();
        wsStarted_ = false;
        return 0u;
    });
    provide(w, "#111", 0, [this, net, self](Cpu&) { // WSAGetLastError
        return net ? wsLastError_[self()] : kSYSNOTREADY;
    });
    provide(w, "#112", 1, [this, self](Cpu& c) { // WSASetLastError
        wsLastError_[self()] = c.arg(0);
        return 0u;
    });

    // socket(af, type, protocol): TCP and UDP over IPv4; not IPX (AF_IPX 6).
    provide(w, "#23", 3, [this, net, fail, log](Cpu& c) {
        if (!wsStarted_)
            return fail(kNOTINITIALISED);
        if (c.arg(0) != 2)
            return fail(kEAFNOSUPPORT);
        if (c.arg(1) != 1 && c.arg(1) != 2)
            return fail(kEPROTONOSUPPORT);
        WsSocket s;
        s.stream = c.arg(1) == 1;
        s.socket = net->open(s.stream ? net::Sockets::Stream : net::Sockets::Datagram);
        if (s.socket < 0)
            return fail(kEMFILE);
        const uint32_t handle = nextWsSocket_++;
        wsSockets_[handle] = s;
        log("socket " + std::to_string(handle) + (s.stream ? " (TCP)" : " (UDP)"));
        return handle;
    });
    provide(w, "#3", 1, [this, net, find, fail](Cpu& c) { // closesocket
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        net->close(s->socket);
        wsSockets_.erase(c.arg(0));
        return 0u;
    });
    provide(w, "#2", 3, [net, find, fail, ours, log](Cpu& c) { // bind(s, name, namelen)
        WsSocket* s = find(c.arg(0));
        net::Address a;
        if (!s)
            return fail(kENOTSOCK);
        if (!readAddress(c, c.arg(1), c.arg(2), a))
            return fail(kEFAULT);
        a = ours(a);
        const Result r = net->bind(s->socket, a);
        log("bind " + std::to_string(c.arg(0)) + " to " + a.toString() + (r == Result::Ok ? "" : ": failed"));
        if (r != Result::Ok)
            return fail(wsError(r));
        s->bound = true;
        return 0u;
    });
    provide(w, "#13", 2, [net, find, fail](Cpu& c) { // listen(s, backlog)
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const Result r = net->listen(s->socket, int(c.arg(1)));
        return r == Result::Ok ? 0u : fail(wsError(r));
    });
    provide(w, "#1", 3, [this, net, find, fail, blocking, log](Cpu& c) { // accept(s, addr, addrlen)
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        int accepted = -1;
        net::Address from;
        const int sock = s->socket;
        const bool nonBlocking = s->nonBlocking;
        const Result r = blocking(*s, false, [&] { return net->accept(sock, accepted, from); });
        if (r != Result::Ok)
            return fail(wsError(r));
        WsSocket a;
        a.stream = true;
        a.bound = true;
        a.socket = accepted;
        a.nonBlocking = nonBlocking;
        const uint32_t handle = nextWsSocket_++;
        wsSockets_[handle] = a;
        writeAddress(c, c.arg(1), c.arg(2), from);
        log("accept on " + std::to_string(c.arg(0)) + ": " + std::to_string(handle) + " from " + from.toString());
        return handle;
    });
    // connect(s, name, namelen): a non-blocking one is under way
    // (WSAEWOULDBLOCK) until select says it's writable.
    provide(w, "#4", 3, [net, find, fail, bindIfNeeded, blocking, log](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        net::Address to;
        if (!s)
            return fail(kENOTSOCK);
        if (!readAddress(c, c.arg(1), c.arg(2), to))
            return fail(kEFAULT);
        bindIfNeeded(*s);
        const int sock = s->socket;
        Result r = net->connect(sock, to);
        if (r == Result::InProgress && !s->nonBlocking) {
            r = blocking(*s, true, [&] {
                const net::Sockets::Ready ready = net->ready(sock);
                return ready.failed ? Result::Refused : ready.write ? Result::Ok : Result::WouldBlock;
            });
        }
        log("connect " + std::to_string(c.arg(0)) + " to " + to.toString() +
            (r == Result::Ok ? "" : r == Result::InProgress ? ": under way" : ": failed"));
        return r == Result::Ok ? 0u : fail(wsError(r));
    });
    provide(w, "#22", 2, [net, find, fail](Cpu& c) { // shutdown(s, how)
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const Result r = net->shutdown(s->socket, int(c.arg(1)));
        return r == Result::Ok || r == Result::NotConnected ? 0u : fail(wsError(r));
    });
    for (const bool peer : {true, false}) // getpeername (5), getsockname (6)
        provide(w, peer ? "#5" : "#6", 3, [net, find, fail, peer](Cpu& c) {
            WsSocket* s = find(c.arg(0));
            if (!s)
                return fail(kENOTSOCK);
            net::Address a;
            const Result r = peer ? net->peerAddress(s->socket, a) : net->localAddress(s->socket, a);
            if (r != Result::Ok)
                return fail(wsError(r));
            writeAddress(c, c.arg(1), c.arg(2), a);
            return 0u;
        });

    // send(s, buf, len, flags), recv(s, buf, len, flags): MSG_PEEK (2).
    provide(w, "#19", 4, [net, find, fail, blocking](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        std::vector<uint8_t> data(c.arg(2));
        for (uint32_t i = 0; i < data.size(); ++i)
            data[i] = c.read8(c.arg(1) + i);
        size_t done = 0;
        const int sock = s->socket;
        const Result r = blocking(*s, true, [&] { return net->send(sock, data.data(), data.size(), done); });
        return r == Result::Ok ? uint32_t(done) : fail(wsError(r));
    });
    provide(w, "#16", 4, [net, find, fail, blocking](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        std::vector<uint8_t> data(c.arg(2));
        size_t done = 0;
        const int sock = s->socket;
        const bool peek = c.arg(3) & 2;
        const Result r = blocking(*s, false, [&] { return net->receive(sock, data.data(), data.size(), done, peek); });
        if (r != Result::Ok)
            return fail(wsError(r));
        for (size_t i = 0; i < done; ++i)
            c.write8(c.arg(1) + uint32_t(i), data[i]);
        return uint32_t(done);
    });
    // sendto(s, buf, len, flags, to, tolen): a broadcast to each of
    // Options::broadcastTo if given.
    provide(w, "#20", 6, [this, net, find, fail, bindIfNeeded](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        net::Address to;
        if (!s)
            return fail(kENOTSOCK);
        if (!s->stream && !readAddress(c, c.arg(4), c.arg(5), to)) // a stream's is ignored
            return fail(kEFAULT);
        std::vector<uint8_t> data(c.arg(2));
        for (uint32_t i = 0; i < data.size(); ++i)
            data[i] = c.read8(c.arg(1) + i);
        bindIfNeeded(*s);
        if (s->stream) {
            size_t done = 0;
            const Result r = net->send(s->socket, data.data(), data.size(), done);
            return r == Result::Ok ? uint32_t(done) : fail(wsError(r));
        }
        std::vector<net::Address> targets{to};
        if (to.ip == 0xffffffffu && !options_.broadcastTo.empty()) {
            targets.clear();
            for (uint32_t ip : options_.broadcastTo)
                targets.push_back({ip, to.port});
        }
        Result r = Result::Ok;
        for (const net::Address& t : targets)
            if (!(options_.netAddress && t.ip == options_.netAddress)) // not to itself
                r = net->sendTo(s->socket, t, data.data(), data.size());
        return r == Result::Ok ? c.arg(2) : fail(wsError(r));
    });
    // recvfrom(s, buf, len, flags, from, fromlen).
    provide(w, "#17", 6, [net, find, fail, blocking](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        std::vector<uint8_t> data(c.arg(2));
        size_t done = 0;
        net::Address from;
        const int sock = s->socket;
        const bool peek = c.arg(3) & 2;
        const Result r =
            blocking(*s, false, [&] { return net->receiveFrom(sock, data.data(), data.size(), done, from, peek); });
        if (r != Result::Ok)
            return fail(wsError(r));
        for (size_t i = 0; i < done; ++i)
            c.write8(c.arg(1) + uint32_t(i), data[i]);
        writeAddress(c, c.arg(4), c.arg(5), from);
        return uint32_t(done);
    });

    // ioctlsocket(s, cmd, argp): FIONBIO, FIONREAD, SIOCATMARK.
    provide(w, "#12", 3, [net, find, fail](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        switch (c.arg(1)) {
        case 0x8004667e: // FIONBIO
            s->nonBlocking = c.read32(c.arg(2)) != 0;
            return 0u;
        case 0x4004667f: { // FIONREAD
            uint32_t bytes = 0;
            net->pending(s->socket, bytes);
            c.write32(c.arg(2), bytes);
            return 0u;
        }
        case 0x40047307: // SIOCATMARK: no out-of-band data
            c.write32(c.arg(2), 1);
            return 0u;
        default:
            return fail(kEINVAL);
        }
    });
    // setsockopt(s, level, name, value, length), getsockopt(...).
    provide(w, "#21", 5, [net, find, fail, log](Cpu& c) {
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const uint32_t level = c.arg(1), name = c.arg(2);
        const int value = c.arg(3) && c.arg(4) >= 4 ? int(c.read32(c.arg(3))) : c.arg(3) && c.arg(4) ? c.read8(c.arg(3)) : 0;
        using O = net::Sockets::Option;
        O option;
        int v = value;
        if (level == 0xffff && name == 0x20)
            option = O::Broadcast;
        else if (level == 0xffff && name == 0x4)
            option = O::ReuseAddress;
        else if (level == 0xffff && name == 0x8)
            option = O::KeepAlive;
        else if (level == 0xffff && name == 0x1001)
            option = O::SendBuffer;
        else if (level == 0xffff && name == 0x1002)
            option = O::ReceiveBuffer;
        else if (level == 0xffff && (name == 0x80 || name == 0xff7f)) { // SO_LINGER, SO_DONTLINGER
            option = O::Linger;
            v = name == 0xff7f ? -1 : c.read16(c.arg(3)) ? c.read16(c.arg(3) + 2) : -1;
        } else if (level == 6 && name == 1)
            option = O::NoDelay;
        else {
            log("setsockopt " + std::to_string(level) + "/" + std::to_string(name) + " taken and ignored");
            return 0u;
        }
        net->setOption(s->socket, option, v);
        return 0u;
    });
    provide(w, "#7", 5, [find, fail](Cpu& c) { // getsockopt: zeros (SO_ERROR none, SO_TYPE from the socket)
        WsSocket* s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        if (!c.arg(3) || !c.arg(4))
            return fail(kEFAULT);
        const uint32_t value = c.arg(1) == 0xffff && c.arg(2) == 0x1008 ? (s->stream ? 1u : 2u) : 0u; // SO_TYPE
        c.write32(c.arg(3), value);
        c.write32(c.arg(4), 4);
        return 0u;
    });

    // select(nfds, readfds, writefds, exceptfds, timeout): fd_set is a count
    // and up to 64 sockets; each set is left with those ready. A null
    // timeout waits for ever.
    provide(w, "#18", 5, [this, net, find, fail](Cpu& c) {
        struct Entry {
            uint32_t handle;
            int socket;
            int set; // 0 read, 1 write, 2 except
        };
        std::vector<Entry> entries;
        for (int set = 0; set < 3; ++set) {
            const uint32_t at = c.arg(1 + set);
            if (!at)
                continue;
            const uint32_t count = std::min<uint32_t>(c.read32(at), 64);
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t handle = c.read32(at + 4 + 4 * i);
                WsSocket* s = find(handle);
                if (!s)
                    return fail(kENOTSOCK);
                entries.push_back({handle, s->socket, set});
            }
        }
        std::vector<bool> isReady(entries.size());
        auto check = [&] {
            bool any = false;
            for (size_t i = 0; i < entries.size(); ++i) {
                const net::Sockets::Ready r = net->ready(entries[i].socket);
                isReady[i] = entries[i].set == 0 ? r.read : entries[i].set == 1 ? r.write : r.failed;
                any = any || isReady[i];
            }
            return any;
        };
        uint32_t timeoutMs = kInfinite;
        if (c.arg(4))
            timeoutMs = c.read32(c.arg(4)) * 1000 + c.read32(c.arg(4) + 4) / 1000;
        if (!check() && timeoutMs != 0) {
            process_->wait(check, timeoutMs);
            if (process_->ending())
                return fail(kEINTR);
            check();
        }
        uint32_t total = 0;
        for (int set = 0; set < 3; ++set) {
            const uint32_t at = c.arg(1 + set);
            if (!at)
                continue;
            uint32_t n = 0;
            for (size_t i = 0; i < entries.size(); ++i)
                if (entries[i].set == set && isReady[i])
                    c.write32(at + 4 + 4 * n++, entries[i].handle);
            c.write32(at, n);
            total += n;
        }
        return total;
    });
    provide(w, "#151", 2, [](Cpu& c) { // __WSAFDIsSet(s, fd_set)
        const uint32_t at = c.arg(1), count = std::min<uint32_t>(c.read32(at), 64);
        for (uint32_t i = 0; i < count; ++i)
            if (c.read32(at + 4 + 4 * i) == c.arg(0))
                return 1u;
        return 0u;
    });

    // Byte order and dotted addresses.
    auto swap16 = [](uint32_t v) { return ((v & 0xff) << 8) | ((v >> 8) & 0xff); };
    auto swap32 = [](uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24); };
    provide(w, "#9", 1, [swap16](Cpu& c) { return swap16(c.arg(0)); });  // htons
    provide(w, "#15", 1, [swap16](Cpu& c) { return swap16(c.arg(0)); }); // ntohs
    provide(w, "#8", 1, [swap32](Cpu& c) { return swap32(c.arg(0)); });  // htonl
    provide(w, "#14", 1, [swap32](Cpu& c) { return swap32(c.arg(0)); }); // ntohl
    provide(w, "#10", 1, [](Cpu& c) {                                     // inet_addr
        unsigned a = 0, b = 0, d = 0, e = 0;
        char extra = 0;
        if (std::sscanf(c.string(c.arg(0), 32).c_str(), "%u.%u.%u.%u%c", &a, &b, &d, &e, &extra) != 4 || a > 255 ||
            b > 255 || d > 255 || e > 255)
            return 0xffffffffu; // INADDR_NONE
        return a | b << 8 | d << 16 | e << 24;
    });
    // Results in a buffer of Winsock's own, as the specification allows:
    // inet_ntoa's text at +0, gethostbyname's hostent at +32 (name at +64,
    // its address list at +48, the address at +56).
    auto buffer = [this] {
        if (!wsBuffer_)
            wsBuffer_ = process_->memory().alloc(384);
        return wsBuffer_;
    };
    provide(w, "#11", 1, [buffer](Cpu& c) { // inet_ntoa
        const uint32_t v = c.arg(0), at = buffer();
        char text[16];
        std::snprintf(text, sizeof text, "%u.%u.%u.%u", v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, v >> 24);
        c.writeString(at, text, 16);
        return at;
    });
    // gethostname(name, length): the machine's (or, with Options::netAddress,
    // that address, so each copy's is its own).
    auto ourName = [this, net] {
        if (options_.netAddress) {
            const uint32_t ip = options_.netAddress;
            return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xff) + "." +
                   std::to_string((ip >> 8) & 0xff) + "." + std::to_string(ip & 0xff);
        }
        return net ? net->hostName() : std::string("localhost");
    };
    provide(w, "#57", 2, [this, ourName, fail](Cpu& c) {
        if (!wsStarted_)
            return fail(kNOTINITIALISED);
        const std::string name = ourName();
        if (name.size() + 1 > c.arg(1))
            return fail(kEFAULT);
        c.writeString(c.arg(0), name, c.arg(1));
        return 0u;
    });
    // gethostbyname(name): one IPv4 address; this machine's own name as
    // Options::netAddress if set.
    provide(w, "#52", 1, [this, net, buffer, ourName, self, log](Cpu& c) -> uint32_t {
        if (!wsStarted_) {
            wsLastError_[self()] = kNOTINITIALISED;
            return 0;
        }
        const std::string name = c.string(c.arg(0), 256);
        uint32_t ip = 0;
        if (options_.netAddress && name == ourName())
            ip = options_.netAddress;
        else if (!net->resolve(name, ip)) {
            wsLastError_[self()] = kHOST_NOT_FOUND;
            log("gethostbyname " + name + ": not found");
            return 0;
        }
        const uint32_t at = buffer(), h = at + 32;
        c.writeString(at + 64, name, 256);
        c.write32(h, at + 64);       // h_name
        c.write32(h + 4, at + 60);   // h_aliases: none
        c.write16(h + 8, 2);         // AF_INET
        c.write16(h + 10, 4);
        c.write32(h + 12, at + 48);  // h_addr_list
        c.write32(at + 48, at + 56);
        c.write32(at + 52, 0);
        c.write8(at + 56, uint8_t(ip >> 24));
        c.write8(at + 57, uint8_t(ip >> 16));
        c.write8(at + 58, uint8_t(ip >> 8));
        c.write8(at + 59, uint8_t(ip));
        c.write32(at + 60, 0);
        log("gethostbyname " + name + ": " + net::Address{ip, 0}.toString());
        return h;
    });
}

} // namespace reluva::nfs
