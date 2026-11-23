// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/ea_stream.h"
#include "common/types.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace reluva::video {

/**
 * An EA movie (`.tgq`): an EA sound stream with its pictures' chunks
 * (`pIQT`, TQI video) between the sound's, at 15 frames a second, as
 * docs/common/video/tqi.md describes.
 */
class EaMovie {
public:
    /** Whether data starts as an EA movie does: an `SCHl` chunk. */
    static bool looksLike(const std::vector<uint8_t> &data);

    /** Takes the file and finds its frames and sound; fails if a chunk runs past the end. */
    Error open(std::vector<uint8_t> data);

    /** How many frames, and a frame's bytes: a `pIQT` chunk's contents. */
    ///@{
    size_t frameCount() const { return _frames.size(); }
    const uint8_t *frame(size_t i) const { return _data.data() + _frames[i].first; }
    size_t frameSize(size_t i) const { return _frames[i].second; }
    ///@}
    /** Frames a second: TQI's are always 15. */
    static constexpr int kFramesPerSecond = 15;
    /** The sound, its segments one after another; empty if it has none. */
    const audio::EaSoundStream &sound() const { return _sound; }

private:
    std::vector<uint8_t> _data;
    std::vector<std::pair<size_t, size_t>> _frames; // each pIQT's contents: offset, size
    audio::EaSoundStream _sound;
};

} // namespace reluva::video
