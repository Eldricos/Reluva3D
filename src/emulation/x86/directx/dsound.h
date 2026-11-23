// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <memory>

namespace reluva::x86 {

/**
 * DirectSound (DSOUND.dll's DirectSoundCreate, up to DirectX 7's
 * interfaces), written from Microsoft's documentation, for programs that
 * need a sound card to go on: a silent one. Buffers play by the process's
 * clock (Win32Process::now()), so a program waiting for a sound to end
 * sees it end when it would have; nothing is heard.
 */
class DirectSound {
public:
    /**
     * Provides DirectSoundCreate to `process`, by name and as DSOUND.dll's
     * first export. Its stand-ins point into this object, so it has to
     * outlive the program's use of them.
     */
    explicit DirectSound(Win32Process &process);
    ~DirectSound();

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
