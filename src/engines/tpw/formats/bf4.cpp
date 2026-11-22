// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 35 fonts', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/bf4.md.

#include "engines/tpw/formats/bf4.h"

namespace reluva::tpw {

namespace {

constexpr size_t kGlyphHeader = 24;
constexpr uint32_t kFourBits = 0;
constexpr uint32_t kOneBit = 2;

uint32_t le16(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8;
}
uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return le16(d, at) | le16(d, at + 2) << 16;
}

} // namespace

Error Font::parse(const std::vector<uint8_t> &data) {
    _glyphs.clear();
    if (data.size() < 8 || data[0] != 'F' || data[1] != '4' || data[2] != 'F' || data[3] != 'B')
        return Error::fail("not a font (F4FB)");
    _kind = data[4];
    _size = data[5];
    const size_t count = le16(data, 6);
    if (8 + 4 * count > data.size())
        return Error::fail("the font is cut short");
    _glyphs.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t at = le32(data, 8 + 4 * i);
        auto fail = [&](const char *why) { return Error::fail("glyph " + std::to_string(i + 1) + ": " + why); };
        if (at + kGlyphHeader > data.size())
            return fail("past the end");
        Glyph &g = _glyphs[i];
        g.character = char32_t(le32(data, at));
        const size_t stored = le32(data, at + 4);
        const uint32_t storage = le32(data, at + 12);
        g.width = int(le16(data, at + 16));
        g.height = int(le16(data, at + 18));
        g.left = data[at + 20];
        g.top = data[at + 21];
        g.advance = int(le16(data, at + 22));
        const size_t pixels = size_t(g.width) * size_t(g.height);
        const size_t bits = storage == kFourBits ? 4 : 1;
        if (storage != kFourBits && storage != kOneBit) {
            g.readable = false;
            continue;
        }
        if (at + kGlyphHeader + stored > data.size() || stored < (pixels * bits + 7) / 8)
            return fail("its pixels run past the end");
        // 4 bits a pixel, the high half first; or 1 bit, the top bit
        // first; rows straight on, with no padding between them.
        const uint8_t *p = data.data() + at + kGlyphHeader;
        g.coverage.resize(pixels);
        for (size_t px = 0; px < pixels; ++px)
            g.coverage[px] = bits == 4 ? uint8_t(p[px / 2] >> (px % 2 ? 0 : 4) & 15)
                                       : uint8_t(p[px / 8] >> (7 - px % 8) & 1 ? 15 : 0);
    }
    return Error::success();
}

} // namespace reluva::tpw
