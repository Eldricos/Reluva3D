// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace reluva {

// A track of a CD image described by a cue sheet (.cue with its .bin
// files), so that games' CD audio can be ripped from an image of the disc.
struct CueTrack {
    int number = 0;
    std::string mode;            // AUDIO, MODE1/2352, ...
    bool audio = false;
    int sectorSize = 2352;
    std::filesystem::path file;  // the image file holding it, found ignoring case
    bool bigEndian = false;      // the file is MOTOROLA (audio samples big-endian)
    uint64_t offset = 0;         // where INDEX 01 starts in the file, in bytes
    uint64_t size = 0;           // up to the next track (its pregap left out) or the file's end
};

// Read a cue sheet whose files are in `dir`. Only BINARY and MOTOROLA
// files are supported: WAVE, MP3 and AIFF files fail. PREGAP and POSTGAP
// aren't stored in the files and so are ignored.
Error parseCueSheet(std::string_view text, const std::filesystem::path& dir, std::vector<CueTrack>& out);
Error readCueSheet(const std::filesystem::path& cue, std::vector<CueTrack>& out);

// Write an audio track as a WAV file: 44.1 kHz, 16-bit stereo.
Error writeCueTrackWav(const CueTrack& track, const std::filesystem::path& wav);

} // namespace reluva
