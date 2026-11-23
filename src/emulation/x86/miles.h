// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <functional>
#include <memory>
#include <vector>

namespace reluva::audio {
class Mixer;
}

namespace reluva::x86 {

/**
 * The Miles Sound System's digital sample functions (mss32.dll's `AIL_*`),
 * for the programs of 1998 to 2002 that play their sound through them,
 * written from how the programs call them and Miles' published function
 * reference: start-up, a wave-out driver, and samples played from two
 * buffers the program fills in turn (AIL_sample_buffer_ready,
 * AIL_load_sample_buffer), in 8- or 16-bit PCM or IMA ADPCM, mono or
 * stereo, with their rate, volume and pan.
 *
 * A sample's play position follows the process's clock, so a buffer comes
 * free as it would have been heard; what's loaded goes to the mixer, as
 * it's loaded, as a stream. Only the functions a program asks for are
 * there: those Starfleet Command II imports.
 */
class Miles {
public:
    /** Stands in for mss32.dll's functions in `process`. */
    explicit Miles(Win32Process &process);
    ~Miles();

    // Where sound goes; none, and samples play in silence (their buffers
    // still come free as they would).
    audio::Mixer *mixer = nullptr;

    /**
     * Told of each buffer loaded, decoded: its sample's handle, and its
     * 16-bit samples (interleaved when stereo) at `rate`; for checking
     * what's played.
     */
    std::function<void(uint32_t sample, const std::vector<int16_t> &samples, int rate, int channels)> bufferLoaded;

    /** Samples allocated now, and those playing. */
    ///@{
    size_t samplesAllocated() const;
    size_t samplesPlaying() const;
    ///@}

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
