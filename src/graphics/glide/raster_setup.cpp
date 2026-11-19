// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/glide/raster_setup.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace reluva::glide::raster {

void load(const State &s, const Vertex &v, Values &out) {
    out.v[kR] = v.r;
    out.v[kG] = v.g;
    out.v[kB] = v.b;
    out.v[kA] = v.a;
    out.v[kZ] = v.ooz;
    out.v[kW] = v.oow;
    // s and t: TMU0's from tmuvtx[0], each later TMU the same unless the
    // hints say its own differ; 1/w: the vertex's oow unless a hint names
    // the TMU's own (Glide 2.4 Reference Manual, grHints).
    float sow = v.tmu[0].sow;
    float tow = v.tmu[0].tow;
    float q = v.oow;
    if (s.stwHints & kStwWDiffTmu0)
        q = v.tmu[0].oow;
    for (int i = 0; i < kMaxTmus; ++i) {
        if (i > 0 && (s.stwHints & (i == 1 ? kStwStDiffTmu1 : kStwStDiffTmu2))) {
            sow = v.tmu[size_t(i)].sow;
            tow = v.tmu[size_t(i)].tow;
        }
        if (i > 0 && (s.stwHints & (i == 1 ? kStwWDiffTmu1 : kStwWDiffTmu2)))
            q = v.tmu[size_t(i)].oow;
        out.v[kS0 + 3 * i] = sow;
        out.v[kS0 + 3 * i + 1] = tow;
        out.v[kS0 + 3 * i + 2] = q;
    }
}

namespace {
// Integer division rounding down and up, for any signs.
int64_t floorDiv(int64_t a, int64_t b) {
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)));
}
int64_t ceilDiv(int64_t a, int64_t b) {
    return -floorDiv(-a, b);
}
} // namespace

bool Triangle::span(int y, int &xs, int &xe) const {
    const int64_t Yc = int64_t(y) * 16 + 8;
    // For each edge, the pixels k from x0 where E0 + 16 a k >= t (t 0 on
    // an inclusive edge, else 1).
    int64_t kmin = 0;
    int64_t kmax = int64_t(x1 - x0) - 1;
    for (int i = 0; i < 3 && kmin <= kmax; ++i) {
        const int64_t E0 = e[i].a * (int64_t(x0) * 16 + 8) + e[i].b * Yc + e[i].c;
        const int64_t t = e[i].inclusive ? 0 : 1;
        const int64_t step = e[i].a * 16;
        if (step > 0)
            kmin = std::max(kmin, ceilDiv(t - E0, step));
        else if (step < 0)
            kmax = std::min(kmax, floorDiv(E0 - t, -step));
        else if (E0 < t)
            kmax = -1;
    }
    if (kmin > kmax)
        return false;
    xs = x0 + int(kmin);
    xe = x0 + int(kmax);
    return true;
}

bool setupTriangle(const State &s, int width, int height, const Vertex &va, const Vertex &vb, const Vertex &vc,
                   Triangle &out, PixelStats &stats) {
    ++stats.trianglesIn;
    // The vertices snapped to 1/16 pixel (the hardware takes x and y as
    // 12.4 fixed point).
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
        return false;
    // The sign of the area, as grCullMode reads it: positive for a triangle
    // turning clockwise on the screen with the origin at the top left.
    if ((s.cull == kCullNegative && area < 0) || (s.cull == kCullPositive && area > 0))
        return false;
    if (area < 0) {
        std::swap(v[1], v[2]);
        std::swap(X[1], X[2]);
        std::swap(Y[1], Y[2]);
        area = -area;
    }
    ++stats.trianglesOut;
    // The pixels: those whose centre is inside all three edges, or on a
    // left edge or an edge along the lower y (Glide 2.4 Reference Manual,
    // grDrawTriangle), within the clip window and the screen.
    const int64_t minX = std::min({X[0], X[1], X[2]});
    const int64_t maxX = std::max({X[0], X[1], X[2]});
    const int64_t minY = std::min({Y[0], Y[1], Y[2]});
    const int64_t maxY = std::max({Y[0], Y[1], Y[2]});
    out.x0 = int(std::max<int64_t>({(minX - 8 + 15) >> 4, s.clip[0], 0}));
    out.x1 = int(std::min<int64_t>({((maxX - 8) >> 4) + 1, s.clip[2], width}));
    out.y0 = int(std::max<int64_t>({(minY - 8 + 15) >> 4, s.clip[1], 0}));
    out.y1 = int(std::min<int64_t>({((maxY - 8) >> 4) + 1, s.clip[3], height}));
    if (out.x0 >= out.x1 || out.y0 >= out.y1)
        return false;
    // Edge i runs from vertex i to vertex i+1; with the area positive the
    // inside is where each edge function is positive.
    for (int i = 0; i < 3; ++i) {
        const int j = (i + 1) % 3;
        const int64_t dx = X[j] - X[i];
        const int64_t dy = Y[j] - Y[i];
        out.e[i].a = -dy;
        out.e[i].b = dx;
        out.e[i].c = dy * X[i] - dx * Y[i];
        out.e[i].inclusive = dy < 0 || (dy == 0 && dx > 0); // a left edge, or the edge along the lower y
    }
    // The plane of each value over the snapped positions.
    Values at[3];
    for (int i = 0; i < 3; ++i)
        load(s, *v[i], at[i]);
    out.at0 = at[0];
    out.fx0 = double(X[0]) / 16;
    out.fy0 = double(Y[0]) / 16;
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
    return true;
}

} // namespace reluva::glide::raster
