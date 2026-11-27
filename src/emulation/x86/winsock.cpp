// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WSOCK32, Windows Sockets 1.1, on the host's network (net::Sockets). IPX
// datagram sockets (AF_IPX) are carried over UDP: an IPX node is an IPv4
// address (its first four bytes), an IPX socket number a UDP port, and the
// broadcast node (all FF) a UDP broadcast. From the Windows Sockets 1.1
// specification: its structures (sockaddr_in, fd_set, hostent, WSADATA)
// in the program's memory, its error numbers, and its blocking: a socket
// blocks unless the program makes it non-blocking (FIONBIO), and a blocked
// call lets the program's other threads run while it waits.

#include "emulation/x86/winsock.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace reluva::x86 {

namespace {

using Result = net::Sockets::Result;

constexpr uint32_t kSocketError = 0xffffffffu;
constexpr uint32_t kInfinite = 0xffffffffu;
// Winsock's error numbers.
constexpr uint32_t kEINTR = 10004;
constexpr uint32_t kEFAULT = 10014;
constexpr uint32_t kEINVAL = 10022;
constexpr uint32_t kEMFILE = 10024;
constexpr uint32_t kEWOULDBLOCK = 10035;
constexpr uint32_t kENOTSOCK = 10038;
constexpr uint32_t kEPROTONOSUPPORT = 10043;
constexpr uint32_t kEAFNOSUPPORT = 10047;
constexpr uint32_t kEADDRINUSE = 10048;
constexpr uint32_t kEADDRNOTAVAIL = 10049;
constexpr uint32_t kENETUNREACH = 10051;
constexpr uint32_t kECONNRESET = 10054;
constexpr uint32_t kENOTCONN = 10057;
constexpr uint32_t kECONNREFUSED = 10061;
constexpr uint32_t kNOTINITIALISED = 10093;
constexpr uint32_t kSYSNOTREADY = 10091;
constexpr uint32_t kHostNotFound = 11001;

uint32_t wsError(Result r) {
    switch (r) {
    case Result::kOk:
        return 0;
    case Result::kWouldBlock:
        return kEWOULDBLOCK;
    case Result::kInProgress:
        return kEWOULDBLOCK; // Winsock 1.1's for a non-blocking connect
    case Result::kRefused:
        return kECONNREFUSED;
    case Result::kReset:
        return kECONNRESET;
    case Result::kNotConnected:
        return kENOTCONN;
    case Result::kAddressInUse:
        return kEADDRINUSE;
    case Result::kAddressNotAvailable:
        return kEADDRNOTAVAIL;
    case Result::kUnreachable:
        return kENETUNREACH;
    case Result::kInvalid:
        return kEINVAL;
    case Result::kFailed:
        return kEFAULT;
    }
    return kEFAULT;
}

// sockaddr_in: family (2), port and address in network order.
bool readAddress(Cpu &c, uint32_t at, uint32_t length, net::Address &out) {
    if (!at || length < 8 || c.read16(at) != 2) // AF_INET
        return false;
    out.port = uint16_t(c.read8(at + 2) << 8 | c.read8(at + 3));
    out.ip = uint32_t(c.read8(at + 4)) << 24 | uint32_t(c.read8(at + 5)) << 16 | uint32_t(c.read8(at + 6)) << 8 |
             c.read8(at + 7);
    return true;
}

// SOCKADDR_IPX: family (6), network (4 bytes), node (6), socket (in
// network order); 14 bytes. The node's first four bytes are the IPv4
// address carrying it (all FF: broadcast), the socket the UDP port.
bool readIpxAddress(Cpu &c, uint32_t at, uint32_t length, net::Address &out) {
    if (!at || length < 14 || c.read16(at) != 6) // AF_IPX
        return false;
    bool broadcast = true;
    for (uint32_t i = 0; i < 6; ++i)
        broadcast = broadcast && c.read8(at + 6 + i) == 0xff;
    out.ip = broadcast ? 0xffffffffu
                       : uint32_t(c.read8(at + 6)) << 24 | uint32_t(c.read8(at + 7)) << 16 |
                             uint32_t(c.read8(at + 8)) << 8 | c.read8(at + 9);
    out.port = uint16_t(c.read8(at + 12) << 8 | c.read8(at + 13));
    return true;
}

void writeIpxAddress(Cpu &c, uint32_t at, uint32_t lengthAt, const net::Address &a) {
    if (!at || (lengthAt && c.read32(lengthAt) < 14))
        return;
    c.write16(at, 6);
    for (uint32_t i = 2; i < 14; ++i)
        c.write8(at + i, 0);
    c.write8(at + 6, uint8_t(a.ip >> 24));
    c.write8(at + 7, uint8_t(a.ip >> 16));
    c.write8(at + 8, uint8_t(a.ip >> 8));
    c.write8(at + 9, uint8_t(a.ip));
    c.write8(at + 12, uint8_t(a.port >> 8));
    c.write8(at + 13, uint8_t(a.port));
    if (lengthAt)
        c.write32(lengthAt, 14);
}

void writeAddress(Cpu &c, uint32_t at, uint32_t lengthAt, const net::Address &a) {
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

Winsock::Winsock(Win32Process &process, Options options) : _process(&process), _options(std::move(options)) {
    const char *w = "WSOCK32.dll";
    // The sockets as they are when the program calls: they may be given
    // after its imports are bound (setSockets).
    const auto net = [this] { return _options.sockets.get(); };
    auto self = [this] { return _process->currentThread() ? _process->currentThread()->id : 0u; };
    auto fail = [this, self](uint32_t error) {
        _lastError[self()] = error;
        return kSocketError;
    };
    auto find = [this](uint32_t handle) -> Socket * {
        auto it = _handles.find(handle);
        return it == _handles.end() ? nullptr : &it->second;
    };
    // The address the game gives, with "any" as Options::netAddress.
    auto ours = [this](net::Address a) {
        if (!a.ip && _options.localAddress)
            a.ip = _options.localAddress;
        return a;
    };
    // A socket not yet bound, given Options::netAddress before it's used,
    // so what it sends comes from there.
    auto bindIfNeeded = [this, net](Socket &s) {
        if (s.bound || !_options.localAddress)
            return;
        net()->bind(s.socket, {_options.localAddress, 0});
        s.bound = true;
    };
    // Calls again while it would block, if the socket blocks: waiting for
    // it to be readable (or writable), the game's other threads running.
    // (By the socket's number, in case another thread closes it meanwhile.)
    auto blocking = [this, net](const Socket &s, bool forWrite, const std::function<Result()> &call) {
        const int socket = s.socket;
        const bool nonBlocking = s.nonBlocking;
        Result r = call();
        while (!nonBlocking && (r == Result::kWouldBlock || r == Result::kInProgress)) {
            _process->wait(
                [net, socket, forWrite] {
                    const net::Sockets::Ready ready = net()->ready(socket);
                    return ready.failed || (forWrite ? ready.write : ready.read);
                },
                kInfinite);
            if (_process->ending())
                return Result::kFailed;
            r = call();
        }
        return r;
    };
    auto log = [this](const std::string &line) {
        if (_options.log)
            _options.log(line);
    };

    // WSAStartup(version, WSADATA*): version 1.1, if there's a network.
    provide(w, "#115", 2, [this, net](Cpu &c) {
        if (!net())
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
        _started = true;
        return 0u;
    });
    provide(w, "#116", 0, [this, net](Cpu &) { // WSACleanup: the sockets left closed
        for (const auto &entry : _handles)     // (not a structured binding: Visual Studio wants it captured)
            net()->close(entry.second.socket);
        _handles.clear();
        _started = false;
        return 0u;
    });
    provide(w, "#111", 0, [this, net, self](Cpu &) { // WSAGetLastError
        return net() ? _lastError[self()] : kSYSNOTREADY;
    });
    provide(w, "#112", 1, [this, self](Cpu &c) { // WSASetLastError
        _lastError[self()] = c.arg(0);
        return 0u;
    });

    // socket(af, type, protocol): TCP and UDP over IPv4, and IPX datagrams
    // (AF_IPX 6, SOCK_DGRAM, NSPROTO_IPX 1000-1255) over UDP; not SPX.
    provide(w, "#23", 3, [this, net, fail, log](Cpu &c) {
        if (!_started)
            return fail(kNOTINITIALISED);
        const bool ipx = c.arg(0) == 6;
        if (c.arg(0) != 2 && !ipx)
            return fail(kEAFNOSUPPORT);
        if (ipx ? c.arg(1) != 2 || (c.arg(2) && (c.arg(2) < 1000 || c.arg(2) > 1255)) : c.arg(1) != 1 && c.arg(1) != 2)
            return fail(kEPROTONOSUPPORT);
        Socket s;
        s.ipx = ipx;
        s.stream = c.arg(1) == 1;
        s.socket = net()->open(s.stream ? net::Sockets::kStream : net::Sockets::kDatagram);
        if (s.socket < 0)
            return fail(kEMFILE);
        if (ipx) // IPX sockets may broadcast and hear broadcasts without asking
            net()->setOption(s.socket, net::Sockets::kBroadcast, 1);
        const uint32_t handle = _nextHandle++;
        _handles[handle] = s;
        log("socket " + std::to_string(handle) + (s.stream ? " (TCP)" : s.ipx ? " (IPX)" : " (UDP)"));
        return handle;
    });
    provide(w, "#3", 1, [this, net, find, fail](Cpu &c) { // closesocket
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        net()->close(s->socket);
        _handles.erase(c.arg(0));
        return 0u;
    });
    provide(w, "#2", 3, [net, find, fail, ours, log](Cpu &c) { // bind(s, name, namelen)
        Socket *s = find(c.arg(0));
        net::Address a;
        if (!s)
            return fail(kENOTSOCK);
        if (!(s->ipx ? readIpxAddress : readAddress)(c, c.arg(1), c.arg(2), a))
            return fail(kEFAULT);
        a = ours(a);
        const Result r = net()->bind(s->socket, a);
        log("bind " + std::to_string(c.arg(0)) + " to " + a.toString() + (r == Result::kOk ? "" : ": failed"));
        if (r != Result::kOk)
            return fail(wsError(r));
        s->bound = true;
        return 0u;
    });
    provide(w, "#13", 2, [net, find, fail](Cpu &c) { // listen(s, backlog)
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const Result r = net()->listen(s->socket, int(c.arg(1)));
        s->listening = r == Result::kOk;
        return r == Result::kOk ? 0u : fail(wsError(r));
    });
    provide(w, "#1", 3, [this, net, find, fail, blocking, log](Cpu &c) { // accept(s, addr, addrlen)
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        int accepted = -1;
        net::Address from;
        const int sock = s->socket;
        const bool nonBlocking = s->nonBlocking;
        const Result r = blocking(*s, false, [&] { return net()->accept(sock, accepted, from); });
        if (r != Result::kOk)
            return fail(wsError(r));
        Socket a;
        a.stream = true;
        a.bound = true;
        a.socket = accepted;
        a.nonBlocking = nonBlocking;
        const uint32_t handle = _nextHandle++;
        _handles[handle] = a;
        writeAddress(c, c.arg(1), c.arg(2), from);
        log("accept on " + std::to_string(c.arg(0)) + ": " + std::to_string(handle) + " from " + from.toString());
        return handle;
    });
    // connect(s, name, namelen): a non-blocking one is under way
    // (WSAEWOULDBLOCK) until select says it's writable.
    provide(w, "#4", 3, [net, find, fail, bindIfNeeded, blocking, log](Cpu &c) {
        Socket *s = find(c.arg(0));
        net::Address to;
        if (!s)
            return fail(kENOTSOCK);
        if (!readAddress(c, c.arg(1), c.arg(2), to))
            return fail(kEFAULT);
        bindIfNeeded(*s);
        const int sock = s->socket;
        Result r = net()->connect(sock, to);
        if (r == Result::kInProgress && !s->nonBlocking) {
            r = blocking(*s, true, [&] {
                const net::Sockets::Ready ready = net()->ready(sock);
                return ready.failed ? Result::kRefused : ready.write ? Result::kOk : Result::kWouldBlock;
            });
        }
        s->connecting = r == Result::kInProgress;
        s->writeArmed = s->writeArmed || r == Result::kOk;
        log("connect " + std::to_string(c.arg(0)) + " to " + to.toString() +
            (r == Result::kOk           ? ""
             : r == Result::kInProgress ? ": under way"
                                        : ": failed"));
        return r == Result::kOk ? 0u : fail(wsError(r));
    });
    provide(w, "#22", 2, [net, find, fail](Cpu &c) { // shutdown(s, how)
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const Result r = net()->shutdown(s->socket, int(c.arg(1)));
        return r == Result::kOk || r == Result::kNotConnected ? 0u : fail(wsError(r));
    });
    // This machine's address for an IPX node: Options::netAddress, or its
    // name's.
    auto ipxNode = [this, net] {
        uint32_t ip = _options.localAddress;
        if (!ip && !net()->resolve(net()->hostName(), ip))
            ip = 0x7f000001;
        return ip;
    };
    for (const bool peer : {true, false}) // getpeername (5), getsockname (6)
        provide(w, peer ? "#5" : "#6", 3, [net, find, fail, peer, ipxNode](Cpu &c) {
            Socket *s = find(c.arg(0));
            if (!s)
                return fail(kENOTSOCK);
            net::Address a;
            const Result r = peer ? net()->peerAddress(s->socket, a) : net()->localAddress(s->socket, a);
            if (r != Result::kOk)
                return fail(wsError(r));
            if (s->ipx) {
                if (!a.ip)
                    a.ip = ipxNode();
                writeIpxAddress(c, c.arg(1), c.arg(2), a);
                return 0u;
            }
            writeAddress(c, c.arg(1), c.arg(2), a);
            return 0u;
        });

    // send(s, buf, len, flags), recv(s, buf, len, flags): MSG_PEEK (2).
    provide(w, "#19", 4, [this, net, find, fail, blocking](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        std::vector<uint8_t> data(c.arg(2));
        for (uint32_t i = 0; i < data.size(); ++i)
            data[i] = c.read8(c.arg(1) + i);
        size_t done = 0;
        const int sock = s->socket;
        if (!data.empty() &&
            (_options.dropOutgoing && _options.dropOutgoing())) { // as if the socket's buffer were full
            net::Address peer;
            net()->peerAddress(sock, peer);
            traffic(true, c.arg(0), peer, data.data(), data.size(), true);
            return fail(wsError(Result::kWouldBlock));
        }
        const Result r = blocking(*s, true, [&] { return net()->send(sock, data.data(), data.size(), done); });
        if (r == Result::kWouldBlock)
            if (Socket *again = find(c.arg(0)))
                again->writeArmed = true;
        if (r == Result::kOk && _options.traffic) {
            net::Address peer;
            net()->peerAddress(sock, peer);
            traffic(true, c.arg(0), peer, data.data(), done);
        }
        return r == Result::kOk ? uint32_t(done) : fail(wsError(r));
    });
    provide(w, "#16", 4, [this, net, find, fail, blocking](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        std::vector<uint8_t> data(c.arg(2));
        size_t done = 0;
        const int sock = s->socket;
        const bool peek = c.arg(3) & 2;
        const Result r =
            blocking(*s, false, [&] { return net()->receive(sock, data.data(), data.size(), done, peek); });
        if (r != Result::kOk)
            return fail(wsError(r));
        for (size_t i = 0; i < done; ++i)
            c.write8(c.arg(1) + uint32_t(i), data[i]);
        if (!peek && _options.traffic) {
            net::Address peer;
            net()->peerAddress(sock, peer);
            traffic(false, c.arg(0), peer, data.data(), done);
        }
        return uint32_t(done);
    });
    // sendto(s, buf, len, flags, to, tolen): a broadcast to each of
    // Options::broadcastTo if given.
    provide(w, "#20", 6, [this, net, find, fail, bindIfNeeded](Cpu &c) {
        Socket *s = find(c.arg(0));
        net::Address to;
        if (!s)
            return fail(kENOTSOCK);
        if (!s->stream && !(s->ipx ? readIpxAddress : readAddress)(c, c.arg(4), c.arg(5), to)) // a stream's is ignored
            return fail(kEFAULT);
        std::vector<uint8_t> data(c.arg(2));
        for (uint32_t i = 0; i < data.size(); ++i)
            data[i] = c.read8(c.arg(1) + i);
        bindIfNeeded(*s);
        if (s->stream) {
            size_t done = 0;
            const Result r = net()->send(s->socket, data.data(), data.size(), done);
            return r == Result::kOk ? uint32_t(done) : fail(wsError(r));
        }
        std::vector<net::Address> targets{to};
        if (to.ip == 0xffffffffu && !_options.broadcastTo.empty()) {
            targets.clear();
            for (uint32_t ip : _options.broadcastTo)
                targets.push_back({ip, to.port});
        }
        if ((_options.dropOutgoing && _options.dropOutgoing())) { // lost on the way
            traffic(true, c.arg(0), to, data.data(), data.size(), true);
            return c.arg(2);
        }
        Result r = Result::kOk;
        for (const net::Address &t : targets)
            if (!(_options.localAddress && t.ip == _options.localAddress)) // not to itself
                r = net()->sendTo(s->socket, t, data.data(), data.size());
        traffic(true, c.arg(0), to, data.data(), data.size());
        return r == Result::kOk ? c.arg(2) : fail(wsError(r));
    });
    // recvfrom(s, buf, len, flags, from, fromlen).
    provide(w, "#17", 6, [this, net, find, fail, blocking](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        std::vector<uint8_t> data(c.arg(2));
        size_t done = 0;
        net::Address from;
        const int sock = s->socket;
        const bool peek = c.arg(3) & 2;
        const Result r =
            blocking(*s, false, [&] { return net()->receiveFrom(sock, data.data(), data.size(), done, from, peek); });
        if (r != Result::kOk)
            return fail(wsError(r));
        for (size_t i = 0; i < done; ++i)
            c.write8(c.arg(1) + uint32_t(i), data[i]);
        (s->ipx ? writeIpxAddress : writeAddress)(c, c.arg(4), c.arg(5), from);
        if (!peek)
            traffic(false, c.arg(0), from, data.data(), done);
        return uint32_t(done);
    });

    // ioctlsocket(s, cmd, argp): FIONBIO, FIONREAD, SIOCATMARK.
    provide(w, "#12", 3, [net, find, fail](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        switch (c.arg(1)) {
        case 0x8004667e: // FIONBIO
            s->nonBlocking = c.read32(c.arg(2)) != 0;
            return 0u;
        case 0x4004667f: { // FIONREAD
            uint32_t bytes = 0;
            net()->pending(s->socket, bytes);
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
    provide(w, "#21", 5, [net, find, fail, log](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const uint32_t level = c.arg(1);
        const uint32_t name = c.arg(2);
        const int value = c.arg(3) && c.arg(4) >= 4 ? int(c.read32(c.arg(3)))
                          : c.arg(3) && c.arg(4)    ? c.read8(c.arg(3))
                                                    : 0;
        using O = net::Sockets::Option;
        O option;
        int v = value;
        if (level == 0xffff && name == 0x20)
            option = O::kBroadcast;
        else if (level == 0xffff && name == 0x4)
            option = O::kReuseAddress;
        else if (level == 0xffff && name == 0x8)
            option = O::kKeepAlive;
        else if (level == 0xffff && name == 0x1001)
            option = O::kSendBuffer;
        else if (level == 0xffff && name == 0x1002)
            option = O::kReceiveBuffer;
        else if (level == 0xffff && (name == 0x80 || name == 0xff7f)) { // SO_LINGER, SO_DONTLINGER
            option = O::kLinger;
            v = name == 0xff7f ? -1 : c.read16(c.arg(3)) ? c.read16(c.arg(3) + 2) : -1;
        } else if (level == 6 && name == 1)
            option = O::kNoDelay;
        else {
            log("setsockopt " + std::to_string(level) + "/" + std::to_string(name) + " taken and ignored");
            return 0u;
        }
        net()->setOption(s->socket, option, v);
        return 0u;
    });
    // getsockopt: zeros (SO_ERROR none, SO_TYPE from the socket); for IPX
    // (NSPROTO_IPX), one adapter (IPX_MAX_ADAPTER_NUM) and its address
    // (IPX_ADDRESS: network 0, node this machine's address).
    provide(w, "#7", 5, [find, fail, ipxNode, log](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        if (!c.arg(3) || !c.arg(4))
            return fail(kEFAULT);
        if (c.arg(1) == 1000 && c.arg(2) == 0x400d) { // IPX_MAX_ADAPTER_NUM
            c.write32(c.arg(3), 1);
            c.write32(c.arg(4), 4);
            return 0u;
        }
        if (c.arg(1) == 1000 && c.arg(2) == 0x4007) { // IPX_ADDRESS: IPX_ADDRESS_DATA, 24 bytes
            const uint32_t at = c.arg(3);
            const uint32_t ip = ipxNode();
            if (c.read32(c.arg(4)) < 24)
                return fail(kEFAULT);
            for (uint32_t i = 4; i < 24; ++i) // adapter number (in) kept
                c.write8(at + i, 0);
            c.write8(at + 8, uint8_t(ip >> 24));
            c.write8(at + 9, uint8_t(ip >> 16));
            c.write8(at + 10, uint8_t(ip >> 8));
            c.write8(at + 11, uint8_t(ip));
            c.write8(at + 15, 1);       // status: up
            c.write32(at + 16, 1470);   // the largest packet: what a UDP datagram carries safely
            c.write32(at + 20, 100000); // link speed, in 100 bit/s units: 10 Mbit/s
            c.write32(c.arg(4), 24);
            return 0u;
        }
        if (c.arg(1) == 1000)
            log("getsockopt IPX " + std::to_string(c.arg(2)) + ": zero");
        const uint32_t value = c.arg(1) == 0xffff && c.arg(2) == 0x1008 ? (s->stream ? 1u : 2u) : 0u; // SO_TYPE
        c.write32(c.arg(3), value);
        c.write32(c.arg(4), 4);
        return 0u;
    });

    // select(nfds, readfds, writefds, exceptfds, timeout): fd_set is a count
    // and up to 64 sockets; each set is left with those ready. A null
    // timeout waits for ever.
    provide(w, "#18", 5, [this, net, find, fail](Cpu &c) {
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
                Socket *s = find(handle);
                if (!s)
                    return fail(kENOTSOCK);
                entries.push_back({handle, s->socket, set});
            }
        }
        std::vector<bool> isReady(entries.size());
        auto check = [&] {
            bool any = false;
            for (size_t i = 0; i < entries.size(); ++i) {
                const net::Sockets::Ready r = net()->ready(entries[i].socket);
                isReady[i] = entries[i].set == 0 ? r.read : entries[i].set == 1 ? r.write : r.failed;
                any = any || isReady[i];
            }
            return any;
        };
        uint32_t timeoutMs = kInfinite;
        if (c.arg(4))
            timeoutMs = c.read32(c.arg(4)) * 1000 + c.read32(c.arg(4) + 4) / 1000;
        if (!check() && timeoutMs != 0) {
            _process->wait(check, timeoutMs);
            if (_process->ending())
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
    provide(w, "#151", 2, [](Cpu &c) { // __WSAFDIsSet(s, fd_set)
        const uint32_t at = c.arg(1);
        const uint32_t count = std::min<uint32_t>(c.read32(at), 64);
        for (uint32_t i = 0; i < count; ++i)
            if (c.read32(at + 4 + 4 * i) == c.arg(0))
                return 1u;
        return 0u;
    });

    // Byte order and dotted addresses.
    auto swap16 = [](uint32_t v) { return ((v & 0xff) << 8) | ((v >> 8) & 0xff); };
    auto swap32 = [](uint32_t v) { return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24); };
    provide(w, "#9", 1, [swap16](Cpu &c) { return swap16(c.arg(0)); });  // htons
    provide(w, "#15", 1, [swap16](Cpu &c) { return swap16(c.arg(0)); }); // ntohs
    provide(w, "#8", 1, [swap32](Cpu &c) { return swap32(c.arg(0)); });  // htonl
    provide(w, "#14", 1, [swap32](Cpu &c) { return swap32(c.arg(0)); }); // ntohl
    provide(w, "#10", 1, [](Cpu &c) {                                    // inet_addr
        unsigned a = 0;
        unsigned b = 0;
        unsigned d = 0;
        unsigned e = 0;
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
        if (!_buffer)
            _buffer = _process->memory().alloc(384);
        return _buffer;
    };
    provide(w, "#11", 1, [buffer](Cpu &c) { // inet_ntoa
        const uint32_t v = c.arg(0);
        const uint32_t at = buffer();
        char text[16];
        std::snprintf(text, sizeof text, "%u.%u.%u.%u", v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, v >> 24);
        c.writeString(at, text, 16);
        return at;
    });
    // gethostname(name, length): the machine's (or, with Options::netAddress,
    // that address, so each copy's is its own).
    auto ourName = [this, net] {
        if (_options.localAddress) {
            const uint32_t ip = _options.localAddress;
            return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xff) + "." +
                   std::to_string((ip >> 8) & 0xff) + "." + std::to_string(ip & 0xff);
        }
        return net() ? net()->hostName() : std::string("localhost");
    };
    provide(w, "#57", 2, [this, ourName, fail](Cpu &c) {
        if (!_started)
            return fail(kNOTINITIALISED);
        const std::string name = ourName();
        if (name.size() + 1 > c.arg(1))
            return fail(kEFAULT);
        c.writeString(c.arg(0), name, c.arg(1));
        return 0u;
    });
    // gethostbyname(name): one IPv4 address; this machine's own name as
    // Options::netAddress if set.
    provide(w, "#52", 1, [this, net, buffer, ourName, self, log](Cpu &c) -> uint32_t {
        if (!_started) {
            _lastError[self()] = kNOTINITIALISED;
            return 0;
        }
        const std::string name = c.string(c.arg(0), 256);
        uint32_t ip = 0;
        if (_options.localAddress && name == ourName())
            ip = _options.localAddress;
        else if (!net() && name == ourName())
            ip = 0x7f000001; // no network: the machine is itself alone
        else if (!net() || !net()->resolve(name, ip)) {
            _lastError[self()] = kHostNotFound;
            log("gethostbyname " + name + ": not found");
            return 0;
        }
        const uint32_t at = buffer();
        const uint32_t h = at + 32;
        c.writeString(at + 64, name, 256);
        c.write32(h, at + 64);     // h_name
        c.write32(h + 4, at + 60); // h_aliases: none
        c.write16(h + 8, 2);       // AF_INET
        c.write16(h + 10, 4);
        c.write32(h + 12, at + 48); // h_addr_list
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
    addWinsock2();
}

void Winsock::provide(const char *dll, const char *name, int args, std::function<uint32_t(Cpu &)> fn) {
    // WS2_32 has WSOCK32's functions at the same ordinals, but for
    // inet_addr, inet_ntoa and ioctlsocket (11, 12 and 10 there).
    if (std::string(dll) == "WSOCK32.dll" && name[0] == '#') {
        _functions[name] = fn;
        const int ordinal = std::atoi(name + 1);
        const int ws2 = ordinal == 10 ? 11 : ordinal == 11 ? 12 : ordinal == 12 ? 10 : ordinal;
        const std::string alias = "#" + std::to_string(ws2);
        provide("WS2_32.dll", alias.c_str(), args, fn);
    }
    if (_options.provide) {
        _options.provide(dll, name, args, std::move(fn));
        return;
    }
    const uint32_t pop = uint32_t(args) * 4;
    _process->provide(dll, name, [fn = std::move(fn), pop](Cpu &c) {
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    });
}

uint32_t Winsock::call(Cpu &c, const std::string &ordinal, const std::vector<uint32_t> &args) {
    const auto it = _functions.find(ordinal);
    if (it == _functions.end())
        return kSocketError;
    // Its arguments and a return address below the caller's, where it
    // reads them; the stack as it was after.
    const uint32_t esp = c.reg[Cpu::ESP];
    for (auto a = args.rbegin(); a != args.rend(); ++a) {
        c.reg[Cpu::ESP] -= 4;
        c.write32(c.reg[Cpu::ESP], *a);
    }
    c.reg[Cpu::ESP] -= 4;
    c.write32(c.reg[Cpu::ESP], 0);
    const uint32_t result = it->second(c);
    c.reg[Cpu::ESP] = esp;
    return result;
}

// The network events (FD_READ 1, FD_WRITE 2, FD_ACCEPT 8, FD_CONNECT 16,
// FD_CLOSE 32) asked for that are due: read and accept while there's
// something to take, write once after a connect or a send that would
// block, connect once it's done, close once the peer has.
uint32_t Winsock::networkEvents(Socket &s) {
    net::Sockets *net = _options.sockets.get();
    if (!net || !s.events)
        return 0;
    const net::Sockets::Ready ready = net->ready(s.socket);
    uint32_t due = 0;
    if (s.listening) {
        if (ready.read)
            due |= 8;
    } else if (ready.read) {
        uint32_t bytes = 0;
        net->pending(s.socket, bytes);
        if (bytes || !s.stream)
            due |= 1;
        else if (!s.closeSeen)
            due |= 32;
    }
    if (s.connecting && (ready.write || ready.failed))
        due |= 16;
    if (s.writeArmed && ready.write && !s.connecting)
        due |= 2;
    if (ready.failed && s.stream && !s.connecting && !s.listening && !s.closeSeen)
        due |= 32;
    return due & s.events;
}

// Windows Sockets 2's own functions, by name in WS2_32: built on WSOCK32's
// (call), and its events on KERNEL32's. Overlapped calls are done before
// they return, as Windows may do them; completion routines aren't.
void Winsock::addWinsock2() {
    const char *w = "WS2_32.dll";
    auto self = [this] { return _process->currentThread() ? _process->currentThread()->id : 0u; };
    auto fail = [this, self](uint32_t error) {
        _lastError[self()] = error;
        return kSocketError;
    };
    auto find = [this](uint32_t handle) -> Socket * {
        auto it = _handles.find(handle);
        return it == _handles.end() ? nullptr : &it->second;
    };
    constexpr uint32_t kEOPNOTSUPP = 10045;
    // A KERNEL32 function, called from here.
    auto kernel = [this](Cpu &c, const char *name, const std::vector<uint32_t> &args) {
        uint32_t result = 0;
        if (c.call(_process->provided("KERNEL32.dll", name), args, result))
            return 0u;
        return result;
    };
    // A function the same as KERNEL32's, its arguments the same: the
    // program goes on into it, as if it had called it.
    auto forward = [this](const char *name, const char *to) {
        provide("WS2_32.dll", name, 0, [this, to](Cpu &c) {
            c.reg[Cpu::ESP] -= 4;
            c.write32(c.reg[Cpu::ESP], _process->provided("KERNEL32.dll", to));
            return c.reg[Cpu::EAX];
        });
    };
    forward("WSACloseEvent", "CloseHandle");
    forward("WSASetEvent", "SetEvent");
    forward("WSAResetEvent", "ResetEvent");
    forward("WSAWaitForMultipleEvents", "WaitForMultipleObjectsEx");
    // WSACreateEvent(): a manual-reset event, not set.
    provide(w, "WSACreateEvent", 0, [kernel](Cpu &c) { return kernel(c, "CreateEventA", {0, 1, 0, 0}); });

    // WSASocketA(af, type, protocol, protocol info, group, flags): socket's.
    provide(w, "WSASocketA", 6, [this, fail](Cpu &c) {
        if (c.arg(3))
            return fail(kEOPNOTSUPP); // a protocol chosen by its WSAPROTOCOL_INFO
        return call(c, "#23", {c.arg(0), c.arg(1), c.arg(2)});
    });
    // WSAConnect(s, name, namelen, caller data, callee data, QOS, group QOS): connect's.
    provide(w, "WSAConnect", 7, [this](Cpu &c) { return call(c, "#4", {c.arg(0), c.arg(1), c.arg(2)}); });

    // A call's overlapped part: done already, its event set and its
    // result in it (Internal 0, InternalHigh the bytes).
    auto overlapped = [kernel](Cpu &c, uint32_t at, uint32_t bytes) {
        if (!at)
            return;
        c.write32(at, 0);
        c.write32(at + 4, bytes);
        if (const uint32_t event = c.read32(at + 16))
            kernel(c, "SetEvent", {event});
    };
    // WSASend(s, buffers, count, sent*, flags, overlapped, routine) and
    // WSASendTo(s, buffers, count, sent*, flags, to, tolen, overlapped,
    // routine): each WSABUF (a length, then where) sent in turn.
    for (const bool to : {false, true})
        provide(w, to ? "WSASendTo" : "WSASend", to ? 9 : 7, [this, fail, to, overlapped](Cpu &c) {
            const uint32_t routine = c.arg(to ? 8 : 6);
            if (routine)
                return fail(kEOPNOTSUPP);
            uint32_t total = 0;
            for (uint32_t i = 0; i < c.arg(2); ++i) {
                const uint32_t buffer = c.arg(1) + 8 * i;
                const uint32_t n =
                    to ? call(c, "#20",
                              {c.arg(0), c.read32(buffer + 4), c.read32(buffer), c.arg(4), c.arg(5), c.arg(6)})
                       : call(c, "#19", {c.arg(0), c.read32(buffer + 4), c.read32(buffer), c.arg(4)});
                if (n == kSocketError) {
                    if (!total)
                        return kSocketError; // its error, as send left it
                    break;
                }
                total += n;
            }
            if (c.arg(3))
                c.write32(c.arg(3), total);
            overlapped(c, c.arg(to ? 7 : 5), total);
            return 0u;
        });
    // WSARecv(s, buffers, count, received*, flags*, overlapped, routine)
    // and WSARecvFrom(s, buffers, count, received*, flags*, from, fromlen*,
    // overlapped, routine): one receive, spread over the buffers in turn.
    for (const bool from : {false, true})
        provide(w, from ? "WSARecvFrom" : "WSARecv", from ? 9 : 7, [this, fail, from, overlapped](Cpu &c) {
            if (c.arg(from ? 8 : 6))
                return fail(kEOPNOTSUPP);
            uint32_t total = 0;
            for (uint32_t i = 0; i < c.arg(2); ++i)
                total += c.read32(c.arg(1) + 8 * i);
            if (total > _scratchSize) {
                _scratch = _process->memory().alloc(total);
                _scratchSize = _scratch ? total : 0;
            }
            if (total && !_scratch)
                return fail(10055); // WSAENOBUFS
            const uint32_t flags = c.arg(4) ? c.read32(c.arg(4)) : 0;
            const uint32_t n = from ? call(c, "#17", {c.arg(0), _scratch, total, flags, c.arg(5), c.arg(6)})
                                    : call(c, "#16", {c.arg(0), _scratch, total, flags});
            if (n == kSocketError)
                return kSocketError;
            uint32_t at = 0;
            for (uint32_t i = 0; i < c.arg(2) && at < n; ++i) {
                const uint32_t buffer = c.arg(1) + 8 * i;
                const uint32_t take = std::min(c.read32(buffer), n - at);
                for (uint32_t k = 0; k < take; ++k)
                    c.write8(c.read32(buffer + 4) + k, c.read8(_scratch + at + k));
                at += take;
            }
            if (c.arg(3))
                c.write32(c.arg(3), n);
            if (c.arg(4))
                c.write32(c.arg(4), 0);
            overlapped(c, c.arg(from ? 7 : 5), n);
            return 0u;
        });

    // WSAEventSelect(s, event, network events): the event set whenever one
    // of them is due, the socket made non-blocking; none asked, neither.
    provide(w, "WSAEventSelect", 3, [this, find, fail](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        s->event = c.arg(1);
        s->events = c.arg(2);
        if (!s->events)
            return 0u;
        s->nonBlocking = true;
        s->writeArmed = true;
        const uint32_t handle = c.arg(0);
        _process->signalEventWhen(s->event, [this, handle] {
            auto it = _handles.find(handle);
            return it != _handles.end() && networkEvents(it->second) != 0;
        });
        return 0u;
    });
    // WSAEnumNetworkEvents(s, event, WSANETWORKEVENTS*): those due (a mask,
    // then ten error codes, all 0 but a failed connect's), each taken; the
    // event reset, to be set again when more are.
    provide(w, "WSAEnumNetworkEvents", 3, [this, find, fail](Cpu &c) {
        Socket *s = find(c.arg(0));
        if (!s)
            return fail(kENOTSOCK);
        const uint32_t due = networkEvents(*s);
        const uint32_t out = c.arg(2);
        if (!c.valid(out, 44))
            return fail(kEFAULT);
        for (uint32_t i = 0; i < 44; i += 4)
            c.write32(out + i, 0);
        c.write32(out, due);
        if (due & 16) { // FD_CONNECT_BIT 4
            s->connecting = false;
            s->writeArmed = true;
            if (_options.sockets && _options.sockets->ready(s->socket).failed)
                c.write32(out + 4 + 4 * 4, kECONNREFUSED);
        }
        if (due & 2)
            s->writeArmed = false;
        if (due & 32)
            s->closeSeen = true;
        const uint32_t handle = c.arg(0);
        const uint32_t event = c.arg(1) ? c.arg(1) : s->event;
        if (event && s->events)
            _process->signalEventWhen(event, [this, handle] {
                auto it = _handles.find(handle);
                return it != _handles.end() && networkEvents(it->second) != 0;
            });
        return 0u;
    });

    // WSAStringToAddressA(text, family, protocol info, address, length*):
    // "a.b.c.d" or "a.b.c.d:port", IPv4 only.
    provide(w, "WSAStringToAddressA", 5, [fail](Cpu &c) {
        if (c.arg(1) != 2)
            return fail(kEINVAL);
        unsigned a = 0;
        unsigned b = 0;
        unsigned d = 0;
        unsigned e = 0;
        unsigned port = 0;
        const std::string text = c.string(c.arg(0), 64);
        const int got = std::sscanf(text.c_str(), "%u.%u.%u.%u:%u", &a, &b, &d, &e, &port);
        if (got < 4 || a > 255 || b > 255 || d > 255 || e > 255 || port > 65535)
            return fail(kEINVAL);
        if (!c.arg(4) || c.read32(c.arg(4)) < 16) {
            if (c.arg(4))
                c.write32(c.arg(4), 16);
            return fail(kEFAULT);
        }
        writeAddress(c, c.arg(3), c.arg(4), {a << 24 | b << 16 | d << 8 | e, uint16_t(port)});
        return 0u;
    });
    // WSAAddressToStringA(address, length, protocol info, text, length*):
    // "a.b.c.d", with ":port" if it has one; the length counts the 0.
    provide(w, "WSAAddressToStringA", 5, [fail](Cpu &c) {
        net::Address a;
        if (!readAddress(c, c.arg(0), c.arg(1), a))
            return fail(kEINVAL);
        std::string text = net::Address{a.ip, 0}.toString();
        if (const size_t colon = text.rfind(':'); colon != std::string::npos)
            text.resize(colon);
        if (a.port)
            text += ":" + std::to_string(a.port);
        const uint32_t need = uint32_t(text.size() + 1);
        if (!c.arg(4) || c.read32(c.arg(4)) < need) {
            if (c.arg(4))
                c.write32(c.arg(4), need);
            return fail(kEFAULT);
        }
        c.writeString(c.arg(3), text, need);
        c.write32(c.arg(4), need);
        return 0u;
    });
    // WSAIoctl and WSAJoinLeaf (multipoint sockets): not stood for, said so.
    provide(w, "WSAIoctl", 9, [fail](Cpu &) { return fail(kEOPNOTSUPP); });
    provide(w, "WSAJoinLeaf", 8, [fail](Cpu &) { return fail(kEOPNOTSUPP); });
}

void Winsock::traffic(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                      bool dropped) const {
    if (_options.traffic && size)
        _options.traffic(sent, handle, peer, data, size, dropped);
}

} // namespace reluva::x86
