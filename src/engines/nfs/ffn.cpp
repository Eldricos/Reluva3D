// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/ffn.h"

#include <cstring>

namespace reluva::nfs {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

constexpr size_t kHeaderSize = 32;
constexpr size_t kGlyphSize = 11;
constexpr uint8_t kAtlas4Bit = 0x7a;

} // namespace

Error parseFont(const std::vector<uint8_t>& data, Font& out) {
    out = {};
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .ffn font: " + why); };
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "FNTF", 4) != 0)
        return bad("no FNTF header");
    const unsigned count = le16(&data[10]);
    out.flags = le16(&data[12]);
    out.lineHeight = data[18];
    const uint32_t atlas = le32(&data[28]);
    if (kHeaderSize + count * kGlyphSize > atlas || static_cast<uint64_t>(atlas) + 16 > data.size())
        return bad("its glyph table runs past its atlas");

    // The atlas: a picture block as in .fsh files, of 4-bit pixels.
    if (data[atlas] != kAtlas4Bit)
        return bad("its atlas has unknown type " + std::to_string(data[atlas]));
    out.atlasWidth = le16(&data[atlas + 4]);
    out.atlasHeight = le16(&data[atlas + 6]);
    const size_t pixels = static_cast<size_t>(out.atlasWidth) * out.atlasHeight;
    if (atlas + 16 + (pixels + 1) / 2 > data.size())
        return bad("its atlas runs past the end");
    out.coverage.resize(pixels);
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t b = data[atlas + 16 + i / 2];
        out.coverage[i] = static_cast<uint8_t>((i % 2 == 0 ? b >> 4 : b & 0x0f) * 17); // high nibble first
    }

    // Each glyph: code (u16), width, height, x (u16), y (u16), advance,
    // x offset, y offset (signed).
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t* g = &data[kHeaderSize + i * kGlyphSize];
        const unsigned code = le16(g);
        Font::Glyph glyph;
        glyph.width = g[2];
        glyph.height = g[3];
        glyph.x = le16(g + 4);
        glyph.y = le16(g + 6);
        glyph.advance = g[8];
        glyph.offsetX = static_cast<int8_t>(g[9]);
        glyph.offsetY = static_cast<int8_t>(g[10]);
        if (code > 255 || glyph.width == 0)
            continue;
        if (glyph.x + glyph.width > out.atlasWidth || glyph.y + glyph.height > out.atlasHeight)
            return bad("glyph " + std::to_string(code) + " lies outside its atlas");
        out.glyphs[code] = glyph;
    }
    return Error::success();
}

int Font::measure(std::string_view text) const {
    int w = 0;
    for (char c : text)
        w += glyphs[static_cast<uint8_t>(c)].advance;
    return w;
}

int Font::draw(Surface& surface, int x, int y, std::string_view text, Color color) const {
    for (char c : text) {
        const Glyph& g = glyphs[static_cast<uint8_t>(c)];
        for (int gy = 0; gy < g.height; ++gy) {
            const int sy = y + g.offsetY + gy;
            if (sy < 0 || sy >= surface.h)
                continue;
            uint8_t* row = surface.row(sy);
            const uint8_t* src = &coverage[static_cast<size_t>(g.y + gy) * atlasWidth + g.x];
            for (int gx = 0; gx < g.width; ++gx) {
                const int sx = x + g.offsetX + gx;
                const unsigned a = src[gx];
                if (sx < 0 || sx >= surface.w || a == 0)
                    continue;
                uint8_t* p = row + sx * 4;
                p[0] = static_cast<uint8_t>((color.r * a + p[0] * (255 - a)) / 255);
                p[1] = static_cast<uint8_t>((color.g * a + p[1] * (255 - a)) / 255);
                p[2] = static_cast<uint8_t>((color.b * a + p[2] * (255 - a)) / 255);
            }
        }
        x += g.advance;
    }
    return x;
}

} // namespace reluva::nfs
