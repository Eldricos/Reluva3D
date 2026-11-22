// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/bft_font.h"

#include "common/bytes.h"

#include <string>

namespace reluva::sfc {

namespace {

constexpr size_t kHeader = 12;
constexpr size_t kGlyphHeader = 20;
constexpr uint32_t kMaxGlyphs = 65536;

} // namespace

Error BftFont::read(const std::vector<uint8_t> &data) {
    _glyphs.clear();
    if (data.size() < kHeader)
        return Error::fail(".bft: shorter than its header");
    _lineHeight = static_cast<int>(readLE32(data.data()));
    _half = static_cast<int>(readLE32(data.data() + 4));
    const uint32_t count = readLE32(data.data() + 8);
    if (count > kMaxGlyphs)
        return Error::fail(".bft: " + std::to_string(count) + " glyphs");
    size_t at = kHeader;
    for (uint32_t i = 0; i < count; ++i) {
        if (data.size() - at < kGlyphHeader)
            return Error::fail(".bft: glyph " + std::to_string(i) + "'s header is past the end");
        const uint8_t *h = data.data() + at;
        Glyph g;
        g.x = static_cast<int32_t>(readLE32(h));
        g.y = static_cast<int32_t>(readLE32(h + 4));
        g.advance = static_cast<int32_t>(readLE32(h + 8));
        g.width = static_cast<int32_t>(readLE32(h + 12));
        g.height = static_cast<int32_t>(readLE32(h + 16));
        at += kGlyphHeader;
        if (g.width < 0 || g.height < 0 || g.width > 4096 || g.height > 4096)
            return Error::fail(".bft: glyph " + std::to_string(i) + " has a size out of range");
        const size_t n = size_t(g.width) * size_t(g.height);
        if (data.size() - at < n)
            return Error::fail(".bft: glyph " + std::to_string(i) + "'s bitmap is past the end");
        g.coverage.assign(data.begin() + static_cast<ptrdiff_t>(at), data.begin() + static_cast<ptrdiff_t>(at + n));
        at += n;
        _glyphs.push_back(std::move(g));
    }
    if (at != data.size())
        return Error::fail(".bft: " + std::to_string(data.size() - at) + " bytes after the last glyph");
    return Error::success();
}

int BftFont::textWidth(std::string_view text) const {
    int w = 0;
    for (const char c : text) {
        const auto i = static_cast<uint8_t>(c);
        if (i < _glyphs.size())
            w += _glyphs[i].advance;
    }
    return w;
}

int BftFont::draw(Surface &surface, int x, int y, std::string_view text, Color color) const {
    for (const char c : text) {
        const auto i = static_cast<uint8_t>(c);
        if (i >= _glyphs.size())
            continue;
        const Glyph &g = _glyphs[i];
        for (int gy = 0; gy < g.height; ++gy) {
            const int sy = y + g.y + gy;
            if (sy < 0 || sy >= surface.h)
                continue;
            for (int gx = 0; gx < g.width; ++gx) {
                const int sx = x + g.x + gx;
                const unsigned a = g.coverage[size_t(gy) * size_t(g.width) + size_t(gx)];
                if (sx < 0 || sx >= surface.w || a == 0)
                    continue;
                uint8_t *p = surface.row(sy) + 4 * sx;
                p[0] = static_cast<uint8_t>((color.r * a + p[0] * (255 - a)) / 255);
                p[1] = static_cast<uint8_t>((color.g * a + p[1] * (255 - a)) / 255);
                p[2] = static_cast<uint8_t>((color.b * a + p[2] * (255 - a)) / 255);
            }
        }
        x += g.advance;
    }
    return x;
}

} // namespace reluva::sfc
