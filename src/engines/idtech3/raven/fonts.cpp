// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Raven's fonts. The file's layout and how a line is laid out were read
// from the GPL Jedi Outcast renderer (qfiles.h, tr_font.cpp) and checked
// against Jedi Outcast's three fonts; docs/engines/idtech3/formats/fonts.md
// describes them. This is new code written from that description.

#include "engines/idtech3/raven/fonts.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3::raven {

namespace {

constexpr size_t kGlyphSize = 28;
constexpr size_t kGlyphs = 256;
constexpr size_t kSizesAt = kGlyphSize * kGlyphs;
constexpr size_t kFileSize = kSizesAt + 5 * 2;

int16_t i16(const uint8_t *p) {
    return int16_t(p[0] | p[1] << 8);
}
int32_t i32(const uint8_t *p) {
    return int32_t(uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24);
}
float f32(const uint8_t *p) {
    const uint32_t bits = uint32_t(i32(p));
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Halves up, as the original rounds.
int round(float f) {
    return int(std::floor(f + 0.5f));
}

} // namespace

Error parseFontdat(const std::vector<uint8_t> &data, const std::string &name, Font &out) {
    out = Font{};
    if (data.size() < kFileSize)
        return Error::fail(name + ": a font file is " + std::to_string(kFileSize) + " bytes or more, this is " +
                           std::to_string(data.size()));
    out.name = name;
    for (size_t i = 0; i < kGlyphs; ++i) {
        const uint8_t *g = &data[i * kGlyphSize];
        FontGlyph &o = out.glyphs[i];
        o.width = i16(g);
        o.height = i16(g + 2);
        o.advance = i16(g + 4);
        o.offset = i16(g + 6);
        o.baseline = i32(g + 8);
        o.s = f32(g + 12);
        o.t = f32(g + 16);
        o.s2 = f32(g + 20);
        o.t2 = f32(g + 24);
    }
    out.pointSize = i16(&data[kSizesAt]);
    out.height = i16(&data[kSizesAt + 2]);
    out.ascender = i16(&data[kSizesAt + 4]);
    out.descender = i16(&data[kSizesAt + 6]);
    out.roundSizes = name.find("ergo") != std::string::npos;
    return Error::success();
}

const FontGlyph &Font::glyph(uint8_t c) const {
    return glyphs[c].width ? glyphs[c] : glyphs['.'];
}

int Font::textWidth(std::string_view text, float scale) const {
    int widest = 0;
    int width = 0;
    for (const char ch : text) {
        const auto c = static_cast<uint8_t>(ch);
        if (c == '\n') {
            width = 0;
            continue;
        }
        width += round(float(glyphs[c].advance ? glyphs[c].advance : glyphs['.'].advance) * scale);
        widest = std::max(widest, width);
    }
    return widest;
}

std::vector<Font::Piece> Font::layout(std::string_view text, int x, int y, float scale, int maxWidth,
                                      int maxChars) const {
    std::vector<Piece> out;
    int colour = -1;
    int penX = x;
    // The line's baseline: the font's height less half its descender down.
    int lineY = y + round(float(height - (descender >> 1)) * scale);
    auto size = [&](int v) { return roundSizes ? float(round(float(v) * scale)) : float(v) * scale; };
    for (size_t i = 0; i < text.size() && maxChars != 0; ++i) {
        const auto c = static_cast<uint8_t>(text[i]);
        if (maxChars > 0 && c != '^' && c != '\n' && c != '\r')
            --maxChars; // this one's drawn, and the last if it was 1
        if (c == '^' && i + 1 < text.size()) {
            colour = (static_cast<uint8_t>(text[++i]) - '0') & 7;
        } else if (c == '\n') {
            penX = x;
            lineY += round(float(pointSize) * scale);
        } else if (c == '\r') {
            continue;
        } else if (c == ' ') {
            penX += round(float(glyphs[' '].advance) * scale);
            if (maxWidth != -1 && penX - x > maxWidth)
                break;
        } else {
            const FontGlyph &g = glyph(c);
            const int advance = round(float(g.advance) * scale);
            if (maxWidth != -1 && penX + advance - x > maxWidth)
                break;
            Piece p;
            p.x = float(penX + round(float(g.offset) * scale));
            p.y = float(lineY) - size(g.baseline);
            p.w = size(g.width);
            p.h = size(g.height);
            p.s = g.s;
            p.t = g.t;
            p.s2 = g.s2;
            p.t2 = g.t2;
            p.colour = colour;
            out.push_back(p);
            penX += advance;
        }
    }
    return out;
}

} // namespace reluva::idtech3::raven
