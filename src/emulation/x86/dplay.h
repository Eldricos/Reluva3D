// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <memory>
#include <vector>

namespace reluva::net {
class Sockets;
}

namespace reluva::x86 {

/**
 * DirectPlay, for the programs that use it (dplayx.dll's classes, through
 * CoCreateInstance), written from Microsoft's published DirectX 6
 * documentation and checked against how the programs call it. So far its
 * lobby (`IDirectPlayLobby3A`): a program started by Reluva was never
 * started by a lobby program, and is told so.
 */
class DirectPlay {
public:
    /**
     * Stands in for the classes in `process`, wrapping its
     * createComObject: other classes still go to the one set before.
     */
    explicit DirectPlay(Win32Process &process);
    ~DirectPlay();

    /**
     * The network its TCP/IP connection plays over, between copies of
     * Reluva: questions looking for sessions go to each of `broadcast`'s
     * addresses and the machine itself, and sessions are hosted on `port`
     * (0: dplay::kPort). Without one, sessions are the program's own.
     */
    void setNetwork(std::shared_ptr<net::Sockets> sockets, std::vector<uint32_t> broadcast, uint16_t port = 0);

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
