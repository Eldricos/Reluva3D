// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/net.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::x86 {

/** The UDP port a session's host listens on: DirectPlay's own TCP/IP port. */
constexpr uint16_t kDirectPlayPort = 47624;

/**
 * DirectPlay 3 (the DirectPlay COM class, through CoCreateInstance), written
 * from the DirectX 5 documentation and SDK headers. It offers one
 * connection, TCP/IP, when it has a network: sessions are found on the
 * local network or at a given host, and played over Reluva's own protocol
 * on UDP (docs/common/x86/dplay.md), between copies of Reluva. Without a
 * network it offers none, as on a machine without one.
 */
class DirectPlay {
public:
    /**
     * Stands in for the class in `process`, wrapping its createComObject:
     * other classes still go to the one set before.
     */
    explicit DirectPlay(Win32Process &process);
    ~DirectPlay();

    /** Where the TCP/IP connection's sessions are played. */
    struct Network {
        /** A UDP socket on `port` (0: any), able to broadcast; null if it can't be had. */
        std::function<std::unique_ptr<net::UdpSocket>(uint16_t port)> open;
        /**
         * Where EnumSessions asks for sessions: a host's address, and the
         * local networks' broadcast addresses (their port kDirectPlayPort).
         */
        std::vector<net::Address> search;
    };
    // Set before the program lists its connections; without `open` there's
    // no connection to offer.
    Network network;
    // Called while a call waits on the network (Open joining a session):
    // for a test to answer for the host. Unset, the thread sleeps a little.
    std::function<void()> waiting;
    // Told of each session made, joined, left or lost, and each player come
    // or gone, as a line; for tools.
    std::function<void(const std::string &line)> log;
    /** Datagrams sent and received. */
    struct Traffic {
        uint64_t sent = 0;
        uint64_t received = 0;
    };
    /** The datagrams every object made has sent and received so far. */
    Traffic traffic() const;

    /**
     * Answers what has arrived and resends what hasn't been acknowledged,
     * for each object the program has made. Calls to the objects do this
     * too; this is for when the program makes none for a while. Any thread.
     */
    void update();

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
