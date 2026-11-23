// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <filesystem>
#include <functional>
#include <memory>

namespace reluva::audio {
class Mixer;
struct Sound;
} // namespace reluva::audio

namespace reluva::x86 {

/**
 * A CD drive's audio, as programs of the 1990s play their music: WINMM's
 * MCI `cdaudio` device (mciSendCommandA) and its auxiliary volume
 * (auxGetNumDevs, auxGetDevCapsA, auxGetVolume, auxSetVolume), written
 * from Microsoft's documentation. The disc's audio tracks come from the
 * engine (disc), and are played through Reluva's mixer as the program asks.
 * A track's play position follows the process's clock, so a program that
 * waits for a track to end, or asks where it is, sees it move.
 */
class CdAudio {
public:
    /** The disc in the drive: how many tracks, and each audio track's sound. */
    struct Disc {
        int tracks = 0; // all of them, the data track too; 0: no disc
        /** A track's length in milliseconds; 0 for a data track, or one that isn't there. */
        std::function<uint32_t(int track)> lengthMs;
        /** A track's sound, 44.1 kHz stereo as on the disc; null where lengthMs is 0. */
        std::function<std::shared_ptr<const audio::Sound>(int track)> load;
    };
    /**
     * A disc of the tracks found in `folder`: a cue sheet's audio tracks,
     * read from its image files (common/cue.h), or else tracks ripped as
     * `trackNN.wav` or `trackNN.mp3` (track 1 being the data track). No
     * disc if there's neither.
     */
    static Disc discIn(const std::filesystem::path &folder);

    /** Provides WINMM's MCI and auxiliary functions to `process`; it has to outlive their use. */
    explicit CdAudio(Win32Process &process);
    ~CdAudio();

    // The disc in the drive, asked when the device is opened and when the
    // disc changes (discChanged).
    std::function<Disc()> disc;
    // Where the music goes; none, and it plays in silence (its position
    // still moves).
    audio::Mixer *mixer = nullptr;
    /** The track playing now, 0 if none. */
    int trackPlaying() const;
    /**
     * Another disc was put in the drive: what was playing stops, and the
     * device has the disc disc() gives now.
     */
    void discChanged();

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
