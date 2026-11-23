// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <cstdint>
#include <memory>

namespace reluva::audio {
class OutputStream;
}

namespace reluva::x86 {

/**
 * DirectSound (DSOUND.dll: DirectSoundCreate and DirectSoundEnumerate,
 * IDirectSound and IDirectSoundBuffer) for programs run in the emulator
 * (docs/common/x86/dsound.md), written from the DirectX SDK's
 * documentation. One device, in software. A buffer's sound lives in the
 * process's memory, where the program writes it (Lock); its play position
 * follows the process's clock, and what the buffers playing hold is mixed
 * as it's heard (output()).
 */
class DirectSound {
public:
    /** Stands in for DSOUND.dll in `process`. */
    explicit DirectSound(Win32Process &process);
    ~DirectSound();

    /**
     * The buffers playing, mixed: for the mixer (Mixer::playOutput). It
     * plays silence once this is gone.
     */
    std::shared_ptr<audio::OutputStream> output();

    /** Secondary buffers made and not yet released, and those playing. */
    ///@{
    int buffers() const;
    int playing() const;
    ///@}

    struct State;

private:
    std::shared_ptr<State> _s;
};

} // namespace reluva::x86
