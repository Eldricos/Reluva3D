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
 * over IPv4, and IPX datagrams carried over UDP. WS2_32, Windows Sockets 2,
 * the same functions at its own ordinals and its extensions on them:
 * scatter and gather, events, addresses as text. Without sockets,
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
    /** Tells `log` what the calls do, and `traffic` each packet (Options' hooks), from now on. */
    void setLog(std::function<void(const std::string &line)> log,
                std::function<void(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data,
                                   size_t size, bool dropped)>
                    traffic) {
        _options.log = std::move(log);
        _options.traffic = std::move(traffic);
    }

private:
    struct Socket {
        int socket = -1;
        bool stream = false;
        bool nonBlocking = false;
        bool bound = false;
        bool ipx = false; // an IPX datagram socket, carried over UDP
        bool listening = false;
        bool connecting = false; // a non-blocking connect under way, for FD_CONNECT
        // WSAEventSelect: the event and the network events (FD_...) asked for.
        uint32_t event = 0;
        uint32_t events = 0;
        bool writeArmed = false; // FD_WRITE is due (after a connect, or a send that would block)
        bool closeSeen = false;  // FD_CLOSE given once
    };
    void provide(const char *dll, const char *name, int args, std::function<uint32_t(Cpu &)> fn);
    void addWinsock2();
    // Runs one of WSOCK32's functions (by its ordinal, "#19") as if the
    // program had called it with `args`, for WS2_32's built on them.
    uint32_t call(Cpu &c, const std::string &ordinal, const std::vector<uint32_t> &args);
    // The network events (FD_...) of those asked for that are due now.
    uint32_t networkEvents(Socket &s);
    void traffic(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                 bool dropped = false) const;

    Win32Process *_process;
    Options _options;
    std::map<uint32_t, Socket> _handles; // Winsock's handle -> the host's socket
    uint32_t _nextHandle = 0x100;
    std::map<uint32_t, uint32_t> _lastError; // by thread
    bool _started = false;
    uint32_t _buffer = 0;                                             // inet_ntoa's and gethostbyname's results
    std::map<std::string, std::function<uint32_t(Cpu &)>> _functions; // WSOCK32's, by ordinal
    uint32_t _scratch = 0;                                            // program memory for a scattered receive
    uint32_t _scratchSize = 0;
};

} // namespace reluva::x86
