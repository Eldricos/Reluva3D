// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/ea_movie.h"

#include <cstring>

namespace reluva::video {

bool EaMovie::looksLike(const std::vector<uint8_t> &data) {
    return data.size() >= 8 && std::memcmp(data.data(), "SCHl", 4) == 0;
}

Error EaMovie::open(std::vector<uint8_t> data) {
    _frames.clear();
    // Chunks: a 4-character tag, then their size, the 8 bytes included.
    for (size_t at = 0; at + 8 <= data.size();) {
        const size_t size =
            size_t(data[at + 4]) | size_t(data[at + 5]) << 8 | size_t(data[at + 6]) << 16 | size_t(data[at + 7]) << 24;
        if (size < 8 || at + size > data.size())
            return Error::fail("Not a valid EA movie: a chunk runs past the end");
        if (std::memcmp(&data[at], "pIQT", 4) == 0)
            _frames.emplace_back(at + 8, size - 8);
        at += size;
    }
    if (_frames.empty())
        return Error::fail("Not an EA movie with TQI pictures");
    _data = data;
    return _sound.open(std::move(data));
}

} // namespace reluva::video
