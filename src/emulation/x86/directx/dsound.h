// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <cstddef>
#include <memory>

namespace reluva::audio {
class Mixer;
}

namespace reluva::x86 {

/**
 * DSOUND.dll: DirectSound up to DirectX 5, for the programs that use it,
 * written from Microsoft's DirectX documentation
 * (docs/common/x86/directsound.md).
 *
 * A buffer's sound lives in the process's memory, where the program writes
 * it (Lock, Unlock). Its play position follows the process's clock, so its
 * position and status move as they would, a buffer played once stops at
 * its end, and what it passes is handed to the mixer as a stream, at its
 * volume and pan. 3D buffers become a volume and a pan from where they are
 * from the listener.
 */
class DirectSound {
public:
    /**
     * Adds DirectSoundCreate to `process`. What it makes points into this
     * object, so it has to outlive the program's use of it.
     */
    explicit DirectSound(Win32Process &process);
    ~DirectSound();

    // Where sound goes; none, and buffers play in silence (their positions
    // still move).
    audio::Mixer *mixer = nullptr;
    /** Buffers made so far, the primary included, and those playing now. */
    ///@{
    size_t buffersMade() const;
    size_t buffersPlaying() const;
    ///@}

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
