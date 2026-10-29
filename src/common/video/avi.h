// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::video {

// A Microsoft AVI file (RIFF "AVI "): its first video stream's frames and
// its first audio stream, read from the whole file in memory. See
// docs/formats/video/avi.md.
struct AviFile {
    struct Chunk {
        size_t offset = 0, size = 0; // in the file; a frame of size 0 repeats the last
    };
    int width = 0, height = 0;
    double framesPerSecond = 0;
    uint32_t videoCodec = 0; // the fourcc, e.g. 'IV50' as 0x30355649
    std::vector<Chunk> frames;
    // The audio stream's WAVEFORMATEX and its data, in order.
    int audioFormat = 0, audioChannels = 0, audioRate = 0, audioBits = 0;
    std::vector<Chunk> audio;

    Error parse(const std::vector<uint8_t>& data);
    // The sound, if it's PCM; nullptr otherwise.
    std::shared_ptr<audio::Sound> sound(const std::vector<uint8_t>& data) const;
};

constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

} // namespace reluva::video
