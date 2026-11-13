// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Anti-aliased points, lines and triangles (docs/common/graphics/glide.md,
// Anti-aliasing). The Glide 2.4 Programming Guide (Chapter 4,
// Anti-aliasing) and Reference Manual (grAADrawTriangle, grAADrawLine,
// grAADrawPoint), and the Glide 3.0 ones (grEnable(GR_AA_ORDERED)), say
// that Glide draws them "by setting up the alpha iterator so that it
// represents pixel coverage", the vertex alpha "multiplied by the pixel
// coverage", that a point is "four pixels, each blended according to the
// computed pixel coverage", that lines come out "somewhat fatter than
// expected", and that a steep colour gradient shows "anomalies at the
// edges". Reluva's reading: the alpha iterator is linear, so the coverage
// it stands for falls linearly with the distance from the edge, 1 on the
// edge and 0 a pixel out; outside a triangle's edge the other values are
// its planes carried on (hence the anomalies, clamped as any iterated
// colour); inside, the pixels are the ones grDrawTriangle draws with the
// vertex alpha as it is. The blending that turns coverage into a softened
// edge is the application's (grAlphaBlendFunction), as the manuals say.

#include "graphics/glide/raster_setup.h"

#include <algorithm>
#include <cmath>

namespace reluva::glide::raster {

namespace {

bool inClip(const State &s, int width, int height, int x, int y) {
    return x >= std::max(s.clip[0], 0) && x < std::min(s.clip[2], width) && y >= std::max(s.clip[1], 0) &&
           y < std::min(s.clip[3], height);
}

} // namespace

void aaTriangleFringe(const State &s, int width, int height, const Vertex &va, const Vertex &vb, const Vertex &vc,
                      const bool aaIn[3], Fragments &out) {
    out.list.clear();
    out.derivatives = false;
    // Snapped to 1/16 pixel as the triangle itself is (setupTriangle).
    auto snap = [](float f) { return std::isfinite(f) ? int64_t(std::llround(double(f) * 16.0)) : int64_t(0); };
    const Vertex *v[3] = {&va, &vb, &vc};
    int64_t X[3];
    int64_t Y[3];
    for (int i = 0; i < 3; ++i) {
        X[i] = snap(v[i]->x);
        Y[i] = snap(v[i]->y);
    }
    int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0)
        return;
    if ((s.cull == kCullNegative && area < 0) || (s.cull == kCullPositive && area > 0))
        return;
    // Edge i runs from vertex i to i + 1: a-b, b-c, c-a.
    bool aa[3] = {aaIn[0], aaIn[1], aaIn[2]};
    if (area < 0) {
        // Turned round (a, c, b): its edges a-c, c-b, b-a are c-a, b-c, a-b.
        std::swap(v[1], v[2]);
        std::swap(X[1], X[2]);
        std::swap(Y[1], Y[2]);
        const bool ab = aa[0];
        const bool bc = aa[1];
        const bool ca = aa[2];
        aa[0] = ca;
        aa[1] = bc;
        aa[2] = ab;
        area = -area;
    }
    if (!aa[0] && !aa[1] && !aa[2])
        return;
    struct Edge {
        int64_t a, b, c;
        bool inclusive;
        double length; // in 1/16 pixel
    } e[3];
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        const int64_t dx = X[j] - X[i];
        const int64_t dy = Y[j] - Y[i];
        e[i] = {-dy, dx, dy * X[i] - dx * Y[i], dy < 0 || (dy == 0 && dx > 0), std::sqrt(double(dx * dx + dy * dy))};
    }
    // The planes, as setupTriangle makes them.
    Values at[3];
    for (int i = 0; i < 3; ++i)
        load(s, *v[i], at[i]);
    const double fx0 = double(X[0]) / 16;
    const double fy0 = double(Y[0]) / 16;
    const double ex1 = double(X[1] - X[0]) / 16;
    const double ey1 = double(Y[1] - Y[0]) / 16;
    const double ex2 = double(X[2] - X[0]) / 16;
    const double ey2 = double(Y[2] - Y[0]) / 16;
    const double det = ex1 * ey2 - ex2 * ey1;
    for (int k = 0; k < kParams; ++k) {
        const double d1 = double(at[1].v[k]) - at[0].v[k];
        const double d2 = double(at[2].v[k]) - at[0].v[k];
        out.dx[k] = float((d1 * ey2 - d2 * ey1) / det);
        out.dy[k] = float((d2 * ex1 - d1 * ex2) / det);
    }
    out.derivatives = true;
    // The pixels round the triangle: its box and a pixel more.
    const int64_t minX = std::min({X[0], X[1], X[2]});
    const int64_t maxX = std::max({X[0], X[1], X[2]});
    const int64_t minY = std::min({Y[0], Y[1], Y[2]});
    const int64_t maxY = std::max({Y[0], Y[1], Y[2]});
    const int x0 = int(std::max<int64_t>({(minX >> 4) - 1, s.clip[0], 0}));
    const int x1 = int(std::min<int64_t>({(maxX >> 4) + 2, s.clip[2], width}));
    const int y0 = int(std::max<int64_t>({(minY >> 4) - 1, s.clip[1], 0}));
    const int y1 = int(std::min<int64_t>({(maxY >> 4) + 2, s.clip[3], height}));
    if (int64_t(x1 - x0) * int64_t(y1 - y0) > (int64_t(1) << 22))
        return; // a runaway triangle: no fringe
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const int64_t Xc = int64_t(x) * 16 + 8;
            const int64_t Yc = int64_t(y) * 16 + 8;
            bool inside = true;
            double coverage = 1;
            for (int i = 0; i < 3; ++i) {
                const int64_t E = e[i].a * Xc + e[i].b * Yc + e[i].c;
                if (E >= (e[i].inclusive ? 0 : 1))
                    continue;
                inside = false;
                if (!aa[i]) {
                    coverage = 0;
                    break;
                }
                // The distance from the edge in pixels (E is in 1/256ths of
                // a pixel squared times the edge's length in 1/16ths).
                const double d = double(-E) / (e[i].length * 16.0);
                coverage *= std::max(0.0, 1.0 - d);
            }
            if (inside || coverage <= 0)
                continue; // drawn by the triangle itself, or not covered
            Fragment f;
            f.x = x;
            f.y = y;
            const double px = x + 0.5 - fx0;
            const double py = y + 0.5 - fy0;
            for (int k = 0; k < kParams; ++k)
                f.v.v[k] = float(at[0].v[k] + out.dx[k] * px + out.dy[k] * py);
            f.v.v[kA] = float(std::clamp(double(f.v.v[kA]), 0.0, 255.0) * coverage);
            out.list.push_back(f);
        }
    // Alpha comes from each pixel's own value, not the plane.
    out.dx[kA] = out.dy[kA] = 0;
}

void aaLinePixels(const State &s, int width, int height, const Vertex &a, const Vertex &b, Fragments &out) {
    out.list.clear();
    out.derivatives = false;
    const double dx = double(b.x) - a.x;
    const double dy = double(b.y) - a.y;
    if (!std::isfinite(dx) || !std::isfinite(dy))
        return;
    const double len2 = dx * dx + dy * dy;
    Values va;
    Values vb;
    load(s, a, va);
    load(s, b, vb);
    const int x0 = int(std::floor(std::min(a.x, b.x))) - 1;
    const int x1 = int(std::floor(std::max(a.x, b.x))) + 2;
    const int y0 = int(std::floor(std::min(a.y, b.y))) - 1;
    const int y1 = int(std::floor(std::max(a.y, b.y))) + 2;
    if (int64_t(x1 - x0) * int64_t(y1 - y0) > (int64_t(1) << 22))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            if (!inClip(s, width, height, x, y))
                continue;
            const double px = x + 0.5 - a.x;
            const double py = y + 0.5 - a.y;
            // Along the line (0 at a, 1 at b), and the distance across it.
            const double t = len2 > 0 ? (px * dx + py * dy) / len2 : 0.0;
            if (t < 0 || t > 1)
                continue;
            const double d = len2 > 0 ? std::fabs(px * dy - py * dx) / std::sqrt(len2) : std::hypot(px, py);
            const double coverage = 1.0 - d;
            if (coverage <= 0)
                continue;
            Fragment f;
            f.x = x;
            f.y = y;
            for (int k = 0; k < kParams; ++k)
                f.v.v[k] = float(va.v[k] + (vb.v[k] - va.v[k]) * t);
            f.v.v[kA] = float(std::clamp(double(f.v.v[kA]), 0.0, 255.0) * coverage);
            out.list.push_back(f);
        }
}

void aaPointPixels(const State &s, int width, int height, const Vertex &a, Fragments &out) {
    out.list.clear();
    out.derivatives = false;
    if (!std::isfinite(a.x) || !std::isfinite(a.y))
        return;
    // A pixel-sized square centred on the point, over the four pixels it
    // touches: each pixel's share of it.
    const double sx = double(a.x) - 0.5;
    const double sy = double(a.y) - 0.5;
    const int x0 = int(std::floor(sx));
    const int y0 = int(std::floor(sy));
    const double fx = sx - x0;
    const double fy = sy - y0;
    Values v;
    load(s, a, v);
    const double alpha = std::clamp(double(v.v[kA]), 0.0, 255.0);
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const double coverage = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
            if (coverage <= 0 || !inClip(s, width, height, x0 + i, y0 + j))
                continue;
            Fragment f;
            f.x = x0 + i;
            f.y = y0 + j;
            f.v = v;
            f.v.v[kA] = float(alpha * coverage);
            out.list.push_back(f);
        }
}

} // namespace reluva::glide::raster
