// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's four worlds' maps', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/attribute-map.md.

#include "engines/tpw/formats/attribute_map.h"

#include <cstring>

namespace reluva::tpw {

namespace {

// "TP2M", then the file's description, to here.
constexpr size_t kFirstChunk = 0x24;
// A MAP chunk's width, height and five words not worked out.
constexpr size_t kMapFields = 28;

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

} // namespace

Error AttributeMap::parse(const std::vector<uint8_t> &data) {
    *this = {};
    if (data.size() < kFirstChunk || std::memcmp(data.data(), "TP2M", 4) != 0)
        return Error::fail("not an attribute map (TP2M)");
    // Chunks of a 4-letter name and a size, to an END chunk.
    for (size_t at = kFirstChunk; at + 8 <= data.size();) {
        const size_t size = le32(data, at + 4);
        if (size > data.size() - at - 8)
            return Error::fail("a chunk of the attribute map runs past the end");
        if (std::memcmp(&data[at], "END ", 4) == 0)
            break;
        if (std::memcmp(&data[at], "MAP ", 4) == 0) {
            if (size < kMapFields)
                return Error::fail("the attribute map's MAP chunk is cut short");
            const int width = int(le32(data, at + 8));
            const int height = int(le32(data, at + 12));
            if (width <= 0 || height <= 0 || size_t(width) * size_t(height) > size - kMapFields)
                return Error::fail("the attribute map's tiles run past its MAP chunk");
            _width = width;
            _height = height;
            const auto first = data.begin() + long(at + 8 + kMapFields);
            _tiles.assign(first, first + long(width) * height);
        }
        at += 8 + size;
    }
    if (_tiles.empty())
        return Error::fail("no MAP chunk in the attribute map");
    return Error::success();
}

} // namespace reluva::tpw
