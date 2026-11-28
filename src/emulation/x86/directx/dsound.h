// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <cstddef>
#include <memory>

namespace reluva::audio {
class OutputStream;
}

namespace reluva::x86 {

/**
 * DirectSound (DSOUND.dll's DirectSoundCreate and DirectSoundEnumerateA,
 * up to DirectX 5's interfaces) for the programs that use it, written from
 * Microsoft's documentation and SDK headers: one sound card mixing in
 * software, its buffers in the process's memory where the program writes
 * them (Lock, Unlock), and DirectSound 3D's buffers and listener
 * (sound3d.h).
 *
 * A buffer's play position follows the process's clock at its frequency,
 * so programs that stream into a buffer or wait for one to end see it
 * move whether or not anything is heard. What's heard is the card's mix,
 * output(), read from each playing buffer's memory as it plays.
 */
class DirectSound {
public:
    /**
     * Provides DSOUND.dll's functions to `process`. Their stand-ins point
     * into this object, so it has to outlive the program's use of them;
     * output() goes silent once it's gone.
     */
    explicit DirectSound(Win32Process &process);
    ~DirectSound();

    /**
     * The card's output: every playing buffer mixed at its play position,
     * for audio::Mixer::playOutput. It reads the process's memory on the
     * mixer's thread.
     */
    std::shared_ptr<audio::OutputStream> output();
    /** Buffers made so far (duplicates too), and those playing now. */
    ///@{
    size_t buffersMade() const;
    size_t buffersPlaying() const;
    ///@}

    struct State;

private:
    std::shared_ptr<State> _s;
};

} // namespace reluva::x86
