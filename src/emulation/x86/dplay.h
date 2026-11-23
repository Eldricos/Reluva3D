// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/dplay_session.h"
#include "emulation/x86/win32.h"

#include <memory>

namespace reluva::x86 {

/**
 * DirectPlay of DirectX 6 (DPLAYX.dll: DirectPlayCreate, DirectPlayEnumerate,
 * DirectPlayLobbyCreate, and the two classes through CoCreateInstance) for
 * programs run in the emulator (docs/common/x86/dplay.md), written from the
 * SDK's documentation. The lobby says the program wasn't started by one;
 * DirectPlay addresses are built and read as documented. Without a
 * network, no connection or session is found and none can be made; on
 * one, the TCP/IP and IPX service providers find each other's sessions,
 * players, groups and messages, over UDP (dplay_session.h).
 */
class DirectPlay {
public:
    /**
     * The network the DirectPlay objects play on: the UDP sockets they're
     * given, where a search goes, and where hosts listen
     * (dplay::Transport). Shared by the programs that play together.
     */
    struct Network : dplay::Transport {
        /**
         * One in memory, for programs in one Reluva process: its sockets
         * reach only each other (net::LoopbackNetwork).
         */
        static std::shared_ptr<Network> inMemory();
    };

    /**
     * Stands in for DPLAYX.dll in `process`, and for its classes in
     * CoCreateInstance, wrapping its createComObject.
     */
    explicit DirectPlay(Win32Process &process);
    ~DirectPlay();

    // The network the program plays on, set before it looks for one;
    // none, and it finds no service provider that can connect.
    std::shared_ptr<Network> network;

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
