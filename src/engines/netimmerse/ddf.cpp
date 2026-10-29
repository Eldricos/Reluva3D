// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/ddf.h"

#include <algorithm>

namespace reluva::netimmerse {

namespace {

constexpr size_t kNameOffset = 4, kNameSize = 64;
constexpr size_t kMetricsOffset = 68;
constexpr size_t kGlyphsOffset = 86, kGlyphSize = 18;

int16_t s16(const std::vector<uint8_t>& d, size_t at) {
    return static_cast<int16_t>(d[at] | d[at + 1] << 8);
}

} // namespace

Error DdfFont::parse(const std::vector<uint8_t>& data) {
    *this = {};
    if (data.size() < kGlyphsOffset || std::string_view(reinterpret_cast<const char*>(data.data()), 4) != "FNT2")
        return Error::fail("not a DDF font (no \"FNT2\")");
    const char* n = reinterpret_cast<const char*>(data.data() + kNameOffset);
    name.assign(n, std::find(n, n + kNameSize, '\0'));
    int m[9];
    for (int i = 0; i < 9; i++)
        m[i] = s16(data, kMetricsOffset + i * 2);
    pageWidth = m[0];
    pageHeight = m[1];
    first = m[2];
    last = m[3];
    pages = m[4];
    lineHeight = m[5];
    descent = m[6];
    emHeight = m[7];
    spaceAdvance = m[8];
    if (first < 0 || last > 255 || last < first - 1)
        return Error::fail("DDF font: characters " + std::to_string(first) + " to " + std::to_string(last));
    size_t count = static_cast<size_t>(last - first + 1);
    if (data.size() < kGlyphsOffset + count * kGlyphSize)
        return Error::fail("DDF font: cut short in its characters");
    glyphs.resize(count);
    for (size_t i = 0; i < count; i++) {
        size_t at = kGlyphsOffset + i * kGlyphSize;
        Glyph& g = glyphs[i];
        g.x = s16(data, at);
        g.y = s16(data, at + 2);
        g.above = s16(data, at + 4);
        g.below = s16(data, at + 6);
        g.left = -s16(data, at + 8); // stored the other way round
        g.right = s16(data, at + 10);
        g.advance = s16(data, at + 12);
        g.page = static_cast<uint16_t>(s16(data, at + 16));
        if (g.page >= pages)
            return Error::fail("DDF font: character " + std::to_string(first + i) + " on page " +
                               std::to_string(g.page) + " of " + std::to_string(pages));
    }
    return Error::success();
}

const DdfFont::Glyph* DdfFont::glyph(uint8_t c) const {
    if (c < first || c > last)
        return nullptr;
    return &glyphs[c - first];
}

std::string DdfFont::pageName(const std::string& file, int page) {
    size_t dot = file.rfind('.');
    std::string stem = dot == std::string::npos ? file : file.substr(0, dot);
    return stem + "_" + std::to_string(page) + ".TGA";
}

int DdfFont::measure(std::string_view text) const {
    int w = 0;
    for (char c : text) {
        const Glyph* g = glyph(static_cast<uint8_t>(c));
        w += g ? g->advance : spaceAdvance;
    }
    return w;
}

int DdfFont::draw(Surface& surface, const std::vector<image::Image>& pageImages, int x, int y,
                  std::string_view text, Color color) const {
    int baseline = y + ascent();
    for (char c : text) {
        const Glyph* g = glyph(static_cast<uint8_t>(c));
        if (!g) {
            x += spaceAdvance;
            continue;
        }
        if (g->page < static_cast<int>(pageImages.size()) && surface.format == PixelFormat::RGBA32) {
            const image::Image& page = pageImages[g->page];
            int top = baseline - g->above;
            for (int row = 0; row < g->height(); row++) {
                int sy = g->y + row, dy = top + row;
                if (sy < 0 || sy >= page.h || dy < 0 || dy >= surface.h)
                    continue;
                for (int col = 0; col < g->width(); col++) {
                    int sx = g->x + col, dx = x + g->left + col;
                    if (sx < 0 || sx >= page.w || dx < 0 || dx >= surface.w)
                        continue;
                    int a = page.pixel(sx, sy)[3];
                    uint8_t* p = surface.row(dy) + dx * 4;
                    p[0] = static_cast<uint8_t>((color.r * a + p[0] * (255 - a)) / 255);
                    p[1] = static_cast<uint8_t>((color.g * a + p[1] * (255 - a)) / 255);
                    p[2] = static_cast<uint8_t>((color.b * a + p[2] * (255 - a)) / 255);
                }
            }
        }
        x += g->advance;
    }
    return x;
}

} // namespace reluva::netimmerse
