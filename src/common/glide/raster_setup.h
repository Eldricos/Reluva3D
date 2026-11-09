// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What the software rasterizer and the GPU backend share before the
// pixels (docs/engines/glide.md, The software rasterizer, steps 1-3): the
// values iterated across a primitive, a triangle's set-up (vertices
// snapped to 1/16 pixel, culling, the fill rule's edge functions, each
// row's span of pixels, the plane of each value), and a line's pixels. Both
// renderers take the same pixels with the same planes; only the per-pixel
// work differs in where it runs.

#include "common/glide/pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace reluva::glide::raster {

// Values iterated across a primitive: linear in screen space.
enum Param {
    kR,
    kG,
    kB,
    kA,
    kZ,
    kW,
    kS0, // then T0, Q0 (the TMU's 1/w), S1, T1, Q1...
    kParams = kS0 + 3 * kMaxTmus,
};

struct Values {
    float v[kParams];
};

// A vertex's values, s, t and 1/w per TMU read as the stw hints say
// (Glide 2.4 Reference Manual, grHints).
void load(const State& s, const Vertex& v, Values& out);

struct Triangle {
    // The pixel rectangle the triangle may cover, within the clip window
    // and the screen: x0 <= x < x1, y0 <= y < y1 (drawing coordinates).
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    // The values at vertex 0 (after the winding is made positive), its
    // snapped position in pixels, and each value's gradients.
    Values at0{};
    double fx0 = 0, fy0 = 0;
    float dx[kParams] = {}, dy[kParams] = {};
    // Row y's pixels xs..xe (inclusive): false if none.
    bool span(int y, int& xs, int& xe) const;

    struct Edge {
        int64_t a, b, c; // E(X, Y) = a * X + b * Y + c, X and Y in 1/16 pixel
        bool inclusive;
    } e[3]{};
};

// Sets up a triangle for drawing: false if it draws nothing (no area,
// culled, outside the clip window). Counts it in `stats` as the hardware
// does (in, and out if not culled).
bool setupTriangle(const State& s, int width, int height, const Vertex& a, const Vertex& b, const Vertex& c,
                   Triangle& out, PixelStats& stats);

// A line's pixels: one a step along the longer axis, at each pixel centre
// the line crosses, the values linear between the ends; `pixel` gets the
// pixel (drawing coordinates, inside the clip window and screen) and its
// values.
template <class F>
void forLinePixels(const State& s, int width, int height, const Vertex& a, const Vertex& b, F&& pixel) {
    Values va, vb;
    load(s, a, va);
    load(s, b, vb);
    const float dx = b.x - a.x, dy = b.y - a.y;
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    const bool xMajor = std::fabs(dx) >= std::fabs(dy);
    const float from = xMajor ? std::min(a.x, b.x) : std::min(a.y, b.y);
    const float to = xMajor ? std::max(a.x, b.x) : std::max(a.y, b.y);
    const int first = int(std::ceil(from - 0.5f)), last = int(std::ceil(to - 0.5f)) - 1;
    if (last - first > 4096)
        return;
    const float span = xMajor ? dx : dy;
    for (int i = first; i <= last; ++i) {
        const float c = float(i) + 0.5f;
        const float t = span != 0 ? (c - (xMajor ? a.x : a.y)) / span : 0.0f;
        const float other = xMajor ? a.y + dy * t : a.x + dx * t;
        const int x = xMajor ? i : int(std::floor(other)), y = xMajor ? int(std::floor(other)) : i;
        if (x < std::max(s.clip[0], 0) || x >= std::min(s.clip[2], width) || y < std::max(s.clip[1], 0) ||
            y >= std::min(s.clip[3], height))
            continue;
        Values v;
        for (int k = 0; k < kParams; ++k)
            v.v[k] = va.v[k] + (vb.v[k] - va.v[k]) * t;
        pixel(x, y, v);
    }
}

// A point's pixel: false if outside the clip window or screen.
inline bool pointPixel(const State& s, int width, int height, const Vertex& a, int& x, int& y) {
    if (!std::isfinite(a.x) || !std::isfinite(a.y))
        return false;
    x = int(std::floor(a.x));
    y = int(std::floor(a.y));
    return x >= std::max(s.clip[0], 0) && x < std::min(s.clip[2], width) && y >= std::max(s.clip[1], 0) &&
           y < std::min(s.clip[3], height);
}

} // namespace reluva::glide::raster
