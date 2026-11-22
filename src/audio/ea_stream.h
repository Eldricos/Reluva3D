// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/sound.h"
#include "common/types.h"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace reluva::audio {

/**
 * A sound's "PT" header: tagged values describing its audio, as
 * docs/common/audio/ea-streams.md describes. Values not given
 * take the defaults.
 */
struct SoundHeader {
    /** The compression tag's values (kPcm16 when there is none). */
    enum Compression { kPcm16 = -1, kEaXa = 7, kMicroTalk = 9 };
    int compression = kPcm16;
    int channels = 1;
    int rate = 22050;
    uint32_t samples = 0;
    uint32_t dataOffset = 0; // in a bank: from the bank's start
    // The loop, in samples: its first and its end (the last sample, or in
    // some banks the count); both 0 if it doesn't loop.
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    std::map<int, uint32_t> tags; // every tag as read, the known ones too
};

/**
 * Reads a PT header from `data` at `at` (at "PT\0\0"). Fails if it runs
 * past `size`, or gives other than 1 or 2 channels or no rate.
 * @param end If not null, gets the offset after the header.
 */
Error parseSoundHeader(const uint8_t *data, size_t size, size_t at, SoundHeader &out, size_t *end = nullptr);

/**
 * An EA sound stream: segments one after another, each an SCHl header (a
 * PT header), an SCCl block count, SCDl data blocks and SCEl, as Need for
 * Speed III's music (.mus) and the sound of EA's movies; see
 * docs/common/audio/ea-streams.md. Other chunks between them (a movie's
 * pictures) are passed over. The data is kept; segments are decoded when
 * asked.
 */
class EaSoundStream {
public:
    /** One segment: its header and where its data blocks are. */
    struct Segment {
        uint32_t offset = 0; // of its SCHl, in the file
        SoundHeader header;
        std::vector<std::pair<uint32_t, uint32_t>> blocks; // each SCDl's payload: offset, size
    };

    /** Takes the file and finds its segments; fails if a chunk runs past the end. */
    Error open(std::vector<uint8_t> data);
    /** The segments, in the file's order. */
    const std::vector<Segment> &segments() const { return _segments; }
    /** The segment starting at a file offset (as a map gives them), or -1. */
    int segmentAt(uint32_t offset) const;
    /** Decodes a segment; only stereo EA-XA and 16-bit PCM are known. */
    Error decode(size_t segment, Sound &out) const;

private:
    std::vector<uint8_t> _data;
    std::vector<Segment> _segments;
};

} // namespace reluva::audio
