// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/truetype.h"

#include "common/text.h"
#include "graphics/truetype_hinting.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::ttf {

namespace {

// Coverage as Windows' smoothed text comes out: heavier than the share of
// the pixel the outline covers, as if raised to 1/2.2. Measured on And Then
// There Were None's options screen against a screenshot of the original
// (the text's mean difference 9.4 levels plain, 6.2 at 2.2, the best of
// 1.8 to 3.0): seen, not traced in Windows' code. The table is
// 255 * (i / 255)^(1/2.2), rounded, written out so it's the same on every
// platform, as a maths library's pow needn't be.
uint8_t smoothed(uint8_t coverage) {
    static constexpr std::array<uint8_t, 256> kTable = {
        0,   21,  28,  34,  39,  43,  46,  50,  53,  56,  59,  61,  64,  66,  68,  70,  72,  74,  76,  78,  80,  82,
        84,  85,  87,  89,  90,  92,  93,  95,  96,  98,  99,  101, 102, 103, 105, 106, 107, 109, 110, 111, 112, 114,
        115, 116, 117, 118, 119, 120, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137,
        138, 139, 140, 141, 142, 143, 144, 144, 145, 146, 147, 148, 149, 150, 151, 151, 152, 153, 154, 155, 156, 156,
        157, 158, 159, 160, 160, 161, 162, 163, 164, 164, 165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173, 173,
        174, 175, 175, 176, 177, 178, 178, 179, 180, 180, 181, 182, 182, 183, 184, 184, 185, 186, 186, 187, 188, 188,
        189, 190, 190, 191, 192, 192, 193, 194, 194, 195, 195, 196, 197, 197, 198, 199, 199, 200, 200, 201, 202, 202,
        203, 203, 204, 205, 205, 206, 206, 207, 207, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215,
        215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 223, 223, 224, 224, 225, 225, 226, 226, 227,
        227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238,
        238, 239, 239, 240, 240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 247, 247, 248, 248, 249,
        249, 249, 250, 250, 251, 251, 252, 252, 253, 253, 254, 254, 255, 255,
    };
    return kTable[coverage];
}

} // namespace

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
    _hinter = std::make_unique<Hinter>();
    if (!_hinter->load(_data) || !_hinter->hasPrograms())
        _hinter.reset();
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
    if (_hinter && hintedGlyph(cp, pixels, g))
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
        for (uint8_t &c : g.coverage)
            c = smoothed(c);
    }
    return g;
}

bool Font::hintedGlyph(uint32_t cp, float pixels, Glyph &g) {
    // At the size's whole number of pixels to the em, as Windows scales a
    // hinted font (its 'head' asks for it).
    const int ppem = int(std::lround(pixels * float(_hinter->unitsPerEm()) / float(_ascent - _descent)));
    HintedGlyph h;
    if (ppem <= 0 || !_hinter->glyph(stbtt_FindGlyphIndex(&_info->font, int(cp)), ppem, h))
        return false;
    g.advance = float(h.advance);
    if (h.contourEnds.empty())
        return true;
    // Its contours as stb_truetype's vertices, in 64ths of a pixel: an
    // off-curve point between two others makes the curve's control, two
    // off-curve points in a row have an on-curve one between them.
    std::vector<stbtt_vertex> v;
    int32_t minX = INT32_MAX;
    int32_t minY = INT32_MAX;
    int32_t maxX = INT32_MIN;
    int32_t maxY = INT32_MIN;
    for (size_t k = 0; k < h.x.size(); k++) {
        minX = std::min(minX, h.x[k]);
        maxX = std::max(maxX, h.x[k]);
        minY = std::min(minY, h.y[k]);
        maxY = std::max(maxY, h.y[k]);
    }
    auto add = [&](uint8_t type, int32_t x, int32_t y, int32_t cx, int32_t cy) {
        stbtt_vertex e{};
        e.type = type;
        e.x = stbtt_vertex_type(x);
        e.y = stbtt_vertex_type(y);
        e.cx = stbtt_vertex_type(cx);
        e.cy = stbtt_vertex_type(cy);
        v.push_back(e);
    };
    size_t start = 0;
    for (const int endPoint : h.contourEnds) {
        const size_t end = size_t(endPoint);
        if (end >= h.x.size() || end < start)
            break;
        const size_t n = end - start + 1;
        auto at = [&](size_t k) { return start + k % n; };
        // Start on an on-curve point, or between two off-curve ones.
        size_t first = 0;
        while (first < n && !h.onCurve[at(first)])
            first++;
        int32_t sx = 0;
        int32_t sy = 0;
        if (first < n) {
            sx = h.x[at(first)];
            sy = h.y[at(first)];
        } else {
            first = 0;
            sx = (h.x[at(0)] + h.x[at(1)]) / 2;
            sy = (h.y[at(0)] + h.y[at(1)]) / 2;
        }
        add(STBTT_vmove, sx, sy, 0, 0);
        bool control = false;
        int32_t cx = 0;
        int32_t cy = 0;
        for (size_t k = 1; k <= n; k++) {
            const size_t i = at(first + k);
            const int32_t x = h.x[i];
            const int32_t y = h.y[i];
            if (h.onCurve[i]) {
                if (control)
                    add(STBTT_vcurve, x, y, cx, cy);
                else
                    add(STBTT_vline, x, y, 0, 0);
                control = false;
            } else {
                if (control)
                    add(STBTT_vcurve, (cx + x) / 2, (cy + y) / 2, cx, cy);
                cx = x, cy = y, control = true;
            }
        }
        if (control)
            add(STBTT_vcurve, sx, sy, cx, cy);
        start = end + 1;
    }
    const int x0 = int(std::floor(minX / 64.0));
    const int x1 = int(std::ceil(maxX / 64.0));
    const int y0 = int(std::floor(-maxY / 64.0));
    const int y1 = int(std::ceil(-minY / 64.0));
    g.width = std::max(0, x1 - x0);
    g.height = std::max(0, y1 - y0);
    g.xoff = x0;
    g.yoff = y0;
    if (g.width && g.height) {
        g.coverage.assign(size_t(g.width) * size_t(g.height), 0);
        stbtt__bitmap bitmap{g.width, g.height, g.width, g.coverage.data()};
        stbtt_Rasterize(&bitmap, 0.35f, v.data(), int(v.size()), 1.0f / 64, 1.0f / 64, 0, 0, x0, y0, 1, nullptr);
        for (uint8_t &c : g.coverage)
            c = smoothed(c);
    }
    return true;
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
