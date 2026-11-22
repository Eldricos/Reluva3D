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
 * SDK's documentation. There's no network yet: the lobby says the program
 * wasn't started by one, no connection or session is found, and a
 * connection can't be made; DirectPlay addresses are built and read as
 * documented.
 */
class DirectPlay {
public:
    /**
     * Stands in for DPLAYX.dll in `process`, and for its classes in
     * CoCreateInstance, wrapping its createComObject.
     */
    explicit DirectPlay(Win32Process &process);
    ~DirectPlay();

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
