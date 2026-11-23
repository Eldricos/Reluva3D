// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

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
 * players, groups and messages.
 */
class DirectPlay {
public:
    /**
     * Where DirectPlay objects meet: the sessions open, their players and
     * groups, and the messages between them. Shared by the copies that
     * play together, each its own process's DirectPlay; calls from their
     * threads at once are safe. In memory, for copies in one Reluva
     * process.
     */
    class Network {
    public:
        Network();
        ~Network();
        /** The sessions open on it. */
        size_t sessions() const;
        struct State;
        /** Its state, for the stand-in's objects. */
        State &state() const { return *_s; }

    private:
        std::unique_ptr<State> _s;
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
