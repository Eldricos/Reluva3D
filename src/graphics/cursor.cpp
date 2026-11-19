// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/cursor.h"

#include <algorithm>
#include <cmath>

namespace reluva::cursor {

Cursor fromIndexed(const uint8_t *pixels, int w, int h, int pitch, const uint8_t *palette, int transparent, int hotX,
                   int hotY) {
    Cursor c;
    if (!pixels || !palette || w <= 0 || h <= 0)
        return c;
    c.w = w;
    c.h = h;
    c.hotX = hotX;
    c.hotY = hotY;
    c.rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t *src = pixels + size_t(y) * size_t(pitch);
        for (int x = 0; x < w; ++x) {
            const int i = src[x];
            if (i == transparent)
                continue;
            uint8_t *p = c.pixel(x, y);
            p[0] = palette[i * 3];
            p[1] = palette[i * 3 + 1];
            p[2] = palette[i * 3 + 2];
            p[3] = 255;
        }
    }
    return c;
}

Cursor fromSurface(const Surface &surface, const Color *palette, int transparent, int hotX, int hotY) {
    if (surface.w <= 0 || surface.h <= 0 || surface.pixels.empty())
        return {};
    if (surface.format == PixelFormat::kCLUT8) {
        if (!palette)
            return {};
        uint8_t rgb[768];
        for (int i = 0; i < 256; ++i) {
            rgb[i * 3] = palette[i].r;
            rgb[i * 3 + 1] = palette[i].g;
            rgb[i * 3 + 2] = palette[i].b;
        }
        return fromIndexed(surface.pixels.data(), surface.w, surface.h, surface.pitch, rgb, transparent, hotX, hotY);
    }
    Cursor c;
    c.w = surface.w;
    c.h = surface.h;
    c.hotX = hotX;
    c.hotY = hotY;
    c.rgba.resize(size_t(c.w) * size_t(c.h) * 4);
    for (int y = 0; y < c.h; ++y)
        std::copy_n(surface.row(y), size_t(c.w) * 4, c.pixel(0, y));
    return c;
}

Cursor scaled(const Cursor &c, float factor, bool smooth) {
    if (c.empty() || !(factor > 0))
        return c;
    Cursor out;
    out.w = std::max(1, int(std::lround(float(c.w) * factor)));
    out.h = std::max(1, int(std::lround(float(c.h) * factor)));
    if (out.w == c.w && out.h == c.h)
        return c;
    out.hotX = std::clamp(int(std::lround(float(c.hotX) * factor)), 0, out.w - 1);
    out.hotY = std::clamp(int(std::lround(float(c.hotY) * factor)), 0, out.h - 1);
    out.rgba.assign(size_t(out.w) * size_t(out.h) * 4, 0);
    const float sx = float(c.w) / float(out.w);
    const float sy = float(c.h) / float(out.h);
    for (int y = 0; y < out.h; ++y) {
        for (int x = 0; x < out.w; ++x) {
            uint8_t *d = out.pixel(x, y);
            if (!smooth) {
                const int u = std::min(c.w - 1, int((float(x) + 0.5f) * sx));
                const int v = std::min(c.h - 1, int((float(y) + 0.5f) * sy));
                std::copy_n(c.pixel(u, v), 4, d);
                continue;
            }
            // Bilinear, weighting colour by alpha so see-through pixels
            // don't darken the edges.
            const float fu = std::clamp((float(x) + 0.5f) * sx - 0.5f, 0.0f, float(c.w - 1));
            const float fv = std::clamp((float(y) + 0.5f) * sy - 0.5f, 0.0f, float(c.h - 1));
            const int u0 = int(fu);
            const int v0 = int(fv);
            const int u1 = std::min(c.w - 1, u0 + 1);
            const int v1 = std::min(c.h - 1, v0 + 1);
            const float tu = fu - float(u0);
            const float tv = fv - float(v0);
            const uint8_t *q[4] = {c.pixel(u0, v0), c.pixel(u1, v0), c.pixel(u0, v1), c.pixel(u1, v1)};
            const float wt[4] = {(1 - tu) * (1 - tv), tu * (1 - tv), (1 - tu) * tv, tu * tv};
            float rgb[3] = {0, 0, 0};
            float a = 0;
            for (int k = 0; k < 4; ++k) {
                const float wa = wt[k] * float(q[k][3]);
                for (int ch = 0; ch < 3; ++ch)
                    rgb[ch] += wa * float(q[k][ch]);
                a += wa;
            }
            if (a > 0)
                for (int ch = 0; ch < 3; ++ch)
                    d[ch] = uint8_t(std::lround(std::clamp(rgb[ch] / a, 0.0f, 255.0f)));
            d[3] = uint8_t(std::lround(std::clamp(a, 0.0f, 255.0f)));
        }
    }
    return out;
}

void draw(Surface &target, const Cursor &c, int x, int y) {
    if (c.empty() || target.format != PixelFormat::kRGBA32)
        return;
    const int left = x - c.hotX;
    const int top = y - c.hotY;
    for (int v = std::max(0, -top); v < c.h && top + v < target.h; ++v) {
        uint8_t *row = target.row(top + v);
        for (int u = std::max(0, -left); u < c.w && left + u < target.w; ++u) {
            const uint8_t *s = c.pixel(u, v);
            uint8_t *d = row + size_t(left + u) * 4;
            const int a = s[3];
            if (a == 0)
                continue;
            for (int ch = 0; ch < 3; ++ch)
                d[ch] = uint8_t((s[ch] * a + d[ch] * (255 - a) + 127) / 255);
            d[3] = uint8_t(std::max<int>(d[3], a));
        }
    }
}

} // namespace reluva::cursor
