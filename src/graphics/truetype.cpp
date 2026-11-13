// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/truetype.h"

#include "common/text.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <algorithm>
#include <cmath>

namespace reluva::ttf {

struct Font::Info {
    stbtt_fontinfo font;
};

Font::Font() = default;
Font::~Font() = default;

Error Font::load(std::vector<uint8_t> data) {
    _data.clear();
    _glyphs.clear();
    _info = std::make_unique<Info>();
    const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
    if (data.size() < 12 || offset < 0 || !stbtt_InitFont(&_info->font, data.data(), offset)) {
        _info.reset();
        return Error::fail("Not a TrueType font");
    }
    _data = std::move(data);
    // stbtt keeps a pointer to the data: point it at our copy.
    stbtt_InitFont(&_info->font, _data.data(), offset);
    stbtt_GetFontVMetrics(&_info->font, &_ascent, &_descent, &_lineGap);
    return Error::success();
}

float Font::scale(float pixels) const {
    return _info ? stbtt_ScaleForPixelHeight(&_info->font, pixels) : 0;
}

int Font::lineHeight(float pixels) const {
    return int(std::ceil(float(_ascent - _descent + _lineGap) * scale(pixels)));
}

float Font::sizeForPoints(float points) const {
    if (!_info)
        return points;
    const float em = points * 96.0f / 72.0f;
    return em * stbtt_ScaleForMappingEmToPixels(&_info->font, 1.0f) * float(_ascent - _descent);
}

int Font::ascent(float pixels) const {
    return int(std::lround(float(_ascent) * scale(pixels)));
}

const Font::Glyph &Font::glyph(uint32_t cp, float pixels) {
    const auto key = std::make_pair(cp, int(pixels * 16));
    auto it = _glyphs.find(key);
    if (it != _glyphs.end())
        return it->second;
    Glyph &g = _glyphs[key];
    if (!_info)
        return g;
    const float s = scale(pixels);
    int advance = 0;
    int bearing = 0;
    stbtt_GetCodepointHMetrics(&_info->font, int(cp), &advance, &bearing);
    g.advance = float(advance) * s;
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&_info->font, int(cp), s, s, &x0, &y0, &x1, &y1);
    g.width = std::max(0, x1 - x0);
    g.height = std::max(0, y1 - y0);
    g.xoff = x0;
    g.yoff = y0;
    if (g.width && g.height) {
        g.coverage.resize(size_t(g.width) * size_t(g.height));
        stbtt_MakeCodepointBitmap(&_info->font, g.coverage.data(), g.width, g.height, g.width, s, s, int(cp));
    }
    return g;
}

int Font::width(std::string_view utf8, float pixels) {
    float x = 0;
    for (size_t i = 0; i < utf8.size();)
        x += glyph(text::nextUtf8(utf8, i), pixels).advance;
    return int(std::ceil(x));
}

int Font::draw(Surface &dst, int x, int y, std::string_view utf8, float pixels, Color color, uint8_t alpha) {
    if (dst.format != PixelFormat::kRGBA32)
        return x;
    const int baseline = y + ascent(pixels);
    float pen = float(x);
    for (size_t i = 0; i < utf8.size();) {
        const Glyph &g = glyph(text::nextUtf8(utf8, i), pixels);
        const int gx = int(std::lround(pen)) + g.xoff;
        const int gy = baseline + g.yoff;
        for (int r = 0; r < g.height; r++) {
            const int dy = gy + r;
            if (dy < 0 || dy >= dst.h)
                continue;
            uint8_t *row = dst.row(dy);
            for (int c = 0; c < g.width; c++) {
                const int dx = gx + c;
                if (dx < 0 || dx >= dst.w)
                    continue;
                const int a = g.coverage[size_t(r) * size_t(g.width) + size_t(c)] * alpha / 255;
                if (!a)
                    continue;
                uint8_t *p = row + dx * 4;
                p[0] = uint8_t((color.r * a + p[0] * (255 - a)) / 255);
                p[1] = uint8_t((color.g * a + p[1] * (255 - a)) / 255);
                p[2] = uint8_t((color.b * a + p[2] * (255 - a)) / 255);
                p[3] = uint8_t(std::max<int>(p[3], a + p[3] * (255 - a) / 255));
            }
        }
        pen += g.advance;
    }
    return int(std::ceil(pen));
}

std::vector<std::string> Font::wrap(std::string_view utf8, float pixels, int maxWidth) {
    std::vector<std::string> lines;
    std::string line;
    size_t start = 0;
    auto flush = [&] {
        lines.push_back(line);
        line.clear();
    };
    while (start <= utf8.size()) {
        size_t end = utf8.find_first_of(" \n", start);
        if (end == std::string_view::npos)
            end = utf8.size();
        const std::string_view word = utf8.substr(start, end - start);
        if (!word.empty()) {
            const std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
            if (!line.empty() && width(candidate, pixels) > maxWidth)
                flush(), line = std::string(word);
            else
                line = candidate;
        }
        if (end < utf8.size() && utf8[end] == '\n')
            flush();
        start = end + 1;
    }
    if (!line.empty() || lines.empty())
        flush();
    return lines;
}

} // namespace reluva::ttf
