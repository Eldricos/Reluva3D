// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/font.h"

#include "common/bytes.h"

#include <cstring>
#include <string>

namespace reluva::sith {

namespace {

constexpr uint32_t kVersion = 10;
constexpr size_t kHeaderSize = 40;

} // namespace

Error parseFont(const std::vector<uint8_t> &data, Font &out) {
    out = Font{};
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "SFNT", 4) != 0)
        return Error::fail("not a font: no SFNT header");
    if (readLE32(&data[4]) != kVersion)
        return Error::fail("font version " + std::to_string(readLE32(&data[4])) + ", not 10");
    for (int i = 0; i < 3; ++i)
        out.fields[i] = readLE32(&data[8 + 4 * i]);
    const uint32_t ranges = readLE32(&data[20]);

    size_t pos = kHeaderSize;
    for (uint32_t r = 0; r < ranges; ++r) {
        if (pos + 4 > data.size())
            return Error::fail("font cut short in range " + std::to_string(r));
        const uint16_t first = readLE16(&data[pos]);
        const uint16_t last = readLE16(&data[pos + 2]);
        pos += 4;
        if (last < first || pos + size_t(last - first + 1) * 8 > data.size())
            return Error::fail("font range " + std::to_string(r) + " runs past the end");
        for (uint32_t c = first; c <= last; ++c) {
            out.glyphs[uint16_t(c)] = {int32_t(readLE32(&data[pos])), int32_t(readLE32(&data[pos + 4]))};
            pos += 8;
        }
    }
    if (Error err = parseBitmap(data.data() + pos, data.size() - pos, out.strip))
        return Error::fail("font's glyphs: " + err.message);
    if (out.strip.cels.empty())
        return Error::fail("font's glyph picture has no cels");
    const int stripWidth = out.strip.cels[0].w;
    for (const auto &[code, g] : out.glyphs)
        if (g.x < 0 || g.width < 0 || g.x + g.width > stripWidth)
            return Error::fail("font's glyph " + std::to_string(code) + " is outside its picture");
    return Error::success();
}

} // namespace reluva::sith
