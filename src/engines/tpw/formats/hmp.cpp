// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 411 height maps', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/hmp.md.

#include "engines/tpw/formats/hmp.h"

#include <cstring>

namespace reluva::tpw {

namespace {

constexpr uint16_t kVersion = 3;
constexpr uint16_t kMagic = 0xab1e;
constexpr size_t kHeaderSize = 0x30;

uint32_t le16(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8;
}
uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return le16(d, at) | le16(d, at + 2) << 16;
}

} // namespace

Error HeightMap::parse(const std::vector<uint8_t> &data) {
    *this = {};
    if (data.size() < kHeaderSize || le16(data, 0) != kVersion || le16(data, 2) != kMagic)
        return Error::fail("not a height map (.hmp)");
    if (le16(data, 4) != kSamplesPerTile)
        return Error::fail("a height map of " + std::to_string(le16(data, 4)) + " samples a tile, not " +
                           std::to_string(kSamplesPerTile));
    const int width = int(le16(data, 8));
    const int depth = int(le16(data, 10));
    const size_t tiles = size_t(width) * size_t(depth);
    const size_t samples = tiles * kSamplesPerTile * kSamplesPerTile;
    const size_t samplesAt = le32(data, 12);
    const size_t heightsAt = le32(data, 16);
    const size_t flagsAt = le32(data, 20);
    for (size_t at : {samplesAt + samples, heightsAt + tiles, flagsAt + tiles})
        if (at > data.size())
            return Error::fail("the height map is cut short");
    _width = width;
    _depth = depth;
    _samples.assign(data.begin() + long(samplesAt), data.begin() + long(samplesAt + samples));
    _tileHeights.assign(data.begin() + long(heightsAt), data.begin() + long(heightsAt + tiles));
    _tileFlags.assign(data.begin() + long(flagsAt), data.begin() + long(flagsAt + tiles));
    for (size_t i = 0; i < 6; ++i) {
        const uint32_t bits = le32(data, 0x18 + 4 * i);
        std::memcpy(&_bounds[i], &bits, sizeof(float));
    }
    return Error::success();
}

} // namespace reluva::tpw
