// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/net.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::x86 {

/**
 * WSOCK32, Windows Sockets 1.1, as games of the time import it (by
 * ordinal), on the host's network (docs/common/x86/winsock.md): TCP and UDP
 * over IPv4, and IPX datagrams carried over UDP. Without sockets,
 * WSAStartup fails and a game offers no network play.
 */
class Winsock {
public:
    /** What the engine gives it. */
    struct Options {
        /** The network; none if null. May be given later (setSockets). */
        std::shared_ptr<net::Sockets> sockets;
        /**
         * The address the program's sockets get where it asks for any (0: as
         * asked), for copies on one machine to have one each.
         */
        uint32_t localAddress = 0;
        /** Where a broadcast goes instead of 255.255.255.255 (empty: there). */
        std::vector<uint32_t> broadcastTo;
        /** Whether a packet sent now is lost, to test a game's resends (null: none). */
        std::function<bool()> dropOutgoing;
        /** Told of each packet sent or received: its socket, peer and bytes. */
        std::function<void(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                           bool dropped)>
            traffic;
        /** Told what the calls do (a socket made, bound, connected...). */
        std::function<void(const std::string &line)> log;
        /** Registers a function, in place of Win32Process::provide (null: that). */
        std::function<void(const char *dll, const char *name, int args, std::function<uint32_t(Cpu &)> fn)> provide;
    };

    /** Stands in for WSOCK32's functions in `process`, before its program is loaded. */
    Winsock(Win32Process &process, Options options);

    /**
     * Started, as if another DLL of the process had called WSAStartup:
     * DirectPlay's TCP/IP provider does for a DirectPlay game, which may
     * then call gethostname without starting it itself.
     */
    void start() { _started = true; }
    /** Gives it its network, or another, from the next call on. */
    void setSockets(std::shared_ptr<net::Sockets> sockets) { _options.sockets = std::move(sockets); }

private:
    struct Socket {
        int socket = -1;
        bool stream = false;
        bool nonBlocking = false;
        bool bound = false;
        bool ipx = false; // an IPX datagram socket, carried over UDP
    };
    void provide(const char *dll, const char *name, int args, std::function<uint32_t(Cpu &)> fn);
    void traffic(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                 bool dropped = false) const;

    Win32Process *_process;
    Options _options;
    std::map<uint32_t, Socket> _handles; // Winsock's handle -> the host's socket
    uint32_t _nextHandle = 0x100;
    std::map<uint32_t, uint32_t> _lastError; // by thread
    bool _started = false;
    uint32_t _buffer = 0; // inet_ntoa's and gethostbyname's results
};

} // namespace reluva::x86
