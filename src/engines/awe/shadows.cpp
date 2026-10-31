// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/shadows.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

std::array<float, 3> shadowLight(const Ac2Scene& scene, const float origin[3]) {
    // 0x428250: D3DLIGHT8.Position (x, y, z) of each light; only those
    // with shadowOnly set if any are. Type, direction and colour don't
    // count. The sum starts from the mesh's world translation.
    std::vector<const Ac2Record*> lights, marked;
    for (const Ac2Record* o : scene.objects())
        if (o->tag == "LITE") {
            lights.push_back(o);
            if (o->i("shadowOnly") != 0)
                marked.push_back(o);
        }
    const std::vector<const Ac2Record*>& use = marked.empty() ? lights : marked;
    std::array<float, 3> sum{0, 0, 0};
    if (origin)
        sum = {origin[0], origin[1], origin[2]};
    for (const Ac2Record* l : use) {
        sum[0] += l->f("x");
        sum[1] += l->f("y");
        sum[2] += l->f("z");
    }
    const float n = use.empty() ? 1.0f : float(use.size());
    return {sum[0] / n, sum[1] / n + 1500.0f, sum[2] / n};
}

std::array<float, 3> flattenOntoFloor(const std::array<float, 3>& light, const float point[3]) {
    return flattenOntoPlane(light, 0, point);
}

std::array<float, 3> flattenOntoPlane(const std::array<float, 3>& light, float height, const float point[3]) {
    // D3DXMatrixShadow(light, plane (0, 1, 0, -height)) on the point: w is
    // light.y - point.y, the rest scaled to meet the plane.
    const float drop = light[1] - point[1];
    if (drop <= 1e-3f)
        return {point[0], height, point[2]};
    const float s = (light[1] - height) / drop;
    return {light[0] + (point[0] - light[0]) * s, height, light[2] + (point[2] - light[2]) * s};
}

void CoverageMask::clear() {
    if (empty())
        return;
    for (int y = minY_; y <= maxY_; y++)
        std::fill(bits_.begin() + ptrdiff_t(size_t(y) * size_t(w_) + size_t(minX_)),
                  bits_.begin() + ptrdiff_t(size_t(y) * size_t(w_) + size_t(maxX_) + 1), uint8_t(0));
    minX_ = minY_ = 1 << 30;
    maxX_ = maxY_ = -1;
}

void CoverageMask::set(int x, int y) {
    bits_[size_t(y) * size_t(w_) + size_t(x)] = 1;
    minX_ = std::min(minX_, x), maxX_ = std::max(maxX_, x);
    minY_ = std::min(minY_, y), maxY_ = std::max(maxY_, y);
}

namespace {

// Calls fill(x, y) for each pixel of the rectangle x0..x1, y0..y1 that
// inside(x, y) holds for, inside(x, y) being a triangle's edge tests at
// the pixel's centre. Each edge's test only changes once along a row
// (its value is monotonic in x, rounding and all), so a row's pixels
// inside are one run: found from where the edges cross the row, then
// followed while the tests hold, rather than every pixel of the
// rectangle tested.
template <class Inside, class Fill>
void eachInside(const float a[], const float b[], const float c[], int x0, int x1, int y0, int y1, Inside inside,
                Fill fill) {
    const float* corners[3] = {a, b, c};
    const float sign = ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) > 0 ? 1.0f : -1.0f;
    for (int y = y0; y <= y1; y++) {
        const float py = float(y) + 0.5f;
        float lo = float(x0), hi = float(x1);
        bool none = false;
        for (int e = 0; e < 3; e++) {
            const float* p = corners[e];
            const float* q = corners[(e + 1) % 3];
            // sign * edge = sign * (dx * (py - p.y) - dy * (px - p.x)),
            // zero at px = p.x + dx * (py - p.y) / dy.
            const float dx = q[0] - p[0], dy = q[1] - p[1];
            if (dy == 0) {
                none |= sign * dx * (py - p[1]) < 0;
                continue;
            }
            const float at = p[0] + dx * (py - p[1]) / dy - 0.5f; // the pixel whose centre is on it
            if (sign * dy < 0)
                lo = std::max(lo, at - 2);
            else
                hi = std::min(hi, at + 2);
        }
        if (none || lo > hi + 1)
            continue;
        int x = std::clamp(int(std::floor(lo)), x0, x1);
        const int last = std::clamp(int(std::ceil(hi)), x0, x1);
        while (x <= last && !inside(x, y))
            x++;
        if (x > last)
            continue;
        while (x > x0 && inside(x - 1, y))
            x--;
        for (; x <= x1 && inside(x, y); x++)
            fill(x, y);
    }
}

} // namespace

void CoverageMask::fillTriangle(const float a[2], const float b[2], const float c[2]) {
    // Pixel centres inside the triangle, by its edge functions (either
    // winding: shadows are two-sided).
    auto edge = [](const float p[2], const float q[2], float x, float y) {
        return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0]);
    };
    const float area = edge(a, b, c[0], c[1]);
    if (std::fabs(area) < 1e-6f)
        return;
    const int x0 = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
    const int x1 = std::min(w_ - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
    const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
    const int y1 = std::min(h_ - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
    if (x1 < x0 || y1 < y0)
        return;
    const float sign = area > 0 ? 1.0f : -1.0f;
    bool any = false;
    eachInside(
        a, b, c, x0, x1, y0, y1,
        [&](int x, int y) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            return sign * edge(a, b, px, py) >= 0 && sign * edge(b, c, px, py) >= 0 && sign * edge(c, a, px, py) >= 0;
        },
        [&](int x, int y) {
            bits_[size_t(y) * size_t(w_) + size_t(x)] = 1;
            any = true;
        });
    if (any) {
        minX_ = std::min(minX_, x0), maxX_ = std::max(maxX_, x1);
        minY_ = std::min(minY_, y0), maxY_ = std::max(maxY_, y1);
    }
}

void ShadowStencil::clear() {
    if (maxX_ < minX_)
        return;
    for (int y = minY_; y <= maxY_; y++) {
        const size_t row = size_t(y) * size_t(w_);
        std::fill(stencil_.begin() + ptrdiff_t(row + size_t(minX_)), stencil_.begin() + ptrdiff_t(row + size_t(maxX_) + 1),
                  uint8_t(0));
        std::fill(depth_.begin() + ptrdiff_t(row + size_t(minX_)), depth_.begin() + ptrdiff_t(row + size_t(maxX_) + 1),
                  kFar);
    }
    minX_ = minY_ = 1 << 30;
    maxX_ = maxY_ = -1;
}

void ShadowStencil::fillTriangle(const float a[3], const float b[3], const float c[3], uint8_t ref, bool writeDepth) {
    // Pixel centres inside, as CoverageMask (either winding: the game
    // draws characters and their shadows without culling), the depth
    // interpolated across the screen.
    auto edge = [](const float p[3], const float q[3], float x, float y) {
        return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0]);
    };
    const float area = edge(a, b, c[0], c[1]);
    if (std::fabs(area) < 1e-6f)
        return;
    const int x0 = std::max(0, int(std::floor(std::min({a[0], b[0], c[0]}))));
    const int x1 = std::min(w_ - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
    const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
    const int y1 = std::min(h_ - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
    if (x1 < x0 || y1 < y0)
        return;
    const float inv = 1.0f / area;
    bool any = false;
    eachInside(
        a, b, c, x0, x1, y0, y1,
        [&](int x, int y) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            return edge(b, c, px, py) * inv >= 0 && edge(c, a, px, py) * inv >= 0 && edge(a, b, px, py) * inv >= 0;
        },
        [&](int x, int y) {
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            // Each corner's weight: the opposite edge's share of the area.
            const float wa = edge(b, c, px, py) * inv, wb = edge(c, a, px, py) * inv, wc = edge(a, b, px, py) * inv;
            const float z = wa * a[2] + wb * b[2] + wc * c[2];
            const size_t i = size_t(y) * size_t(w_) + size_t(x);
            if (z > depth_[i])
                return;
            stencil_[i] = ref;
            if (writeDepth)
                depth_[i] = z;
            any = true;
        });
    if (any) {
        minX_ = std::min(minX_, x0), maxX_ = std::max(maxX_, x1);
        minY_ = std::min(minY_, y0), maxY_ = std::max(maxY_, y1);
    }
}

void ShadowStencil::reset(const std::vector<uint8_t>& cover) {
    if (maxX_ < minX_ || cover.size() != stencil_.size())
        return;
    for (int y = minY_; y <= maxY_; y++)
        for (int x = minX_; x <= maxX_; x++) {
            const size_t i = size_t(y) * size_t(w_) + size_t(x);
            if (cover[i])
                stencil_[i] = 0;
        }
}

void ShadowStencil::select(uint8_t ref, CoverageMask& out) const {
    out.clear();
    if (maxX_ < minX_)
        return;
    for (int y = minY_; y <= maxY_; y++)
        for (int x = minX_; x <= maxX_; x++)
            if (stencil_[size_t(y) * size_t(w_) + size_t(x)] == ref)
                out.set(x, y);
}

} // namespace reluva::awe
