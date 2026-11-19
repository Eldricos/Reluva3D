// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/render/shadows.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

std::array<float, 3> shadowLight(const Ac2Scene &scene, const float origin[3]) {
    // 0x428250: D3DLIGHT8.Position (x, y, z) of each light; only those
    // with shadowOnly set if any are. Type, direction and colour don't
    // count. The sum starts from the mesh's world translation.
    std::vector<const Ac2Record *> lights;
    std::vector<const Ac2Record *> marked;
    for (const Ac2Record *o : scene.objects())
        if (o->tag == "LITE") {
            lights.push_back(o);
            if (o->i("shadowOnly") != 0)
                marked.push_back(o);
        }
    const std::vector<const Ac2Record *> &use = marked.empty() ? lights : marked;
    std::array<float, 3> sum{0, 0, 0};
    if (origin)
        sum = {origin[0], origin[1], origin[2]};
    for (const Ac2Record *l : use) {
        sum[0] += l->f("x");
        sum[1] += l->f("y");
        sum[2] += l->f("z");
    }
    const float n = use.empty() ? 1.0f : float(use.size());
    return {sum[0] / n, sum[1] / n + 1500.0f, sum[2] / n};
}

std::array<float, 3> flattenOntoFloor(const std::array<float, 3> &light, const float point[3]) {
    return flattenOntoPlane(light, 0, point);
}

std::array<float, 3> flattenOntoPlane(const std::array<float, 3> &light, float height, const float point[3]) {
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
    for (int y = _minY; y <= _maxY; y++)
        std::fill(_bits.begin() + ptrdiff_t(size_t(y) * size_t(_w) + size_t(_minX)),
                  _bits.begin() + ptrdiff_t(size_t(y) * size_t(_w) + size_t(_maxX) + 1), uint8_t(0));
    _minX = _minY = 1 << 30;
    _maxX = _maxY = -1;
}

void CoverageMask::set(int x, int y) {
    _bits[size_t(y) * size_t(_w) + size_t(x)] = 1;
    _minX = std::min(_minX, x), _maxX = std::max(_maxX, x);
    _minY = std::min(_minY, y), _maxY = std::max(_maxY, y);
}

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
    const int x1 = std::min(_w - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
    const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
    const int y1 = std::min(_h - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
    if (x1 < x0 || y1 < y0)
        return;
    const float sign = area > 0 ? 1.0f : -1.0f;
    bool any = false;
    for (int y = y0; y <= y1; y++) {
        const float py = float(y) + 0.5f;
        uint8_t *row = _bits.data() + size_t(y) * size_t(_w);
        // Each edge's value only changes sign once along a row, so the
        // pixels inside are one run: done once it's left.
        bool in = false;
        for (int x = x0; x <= x1; x++) {
            const float px = float(x) + 0.5f;
            if (sign * edge(a, b, px, py) >= 0 && sign * edge(b, c, px, py) >= 0 && sign * edge(c, a, px, py) >= 0) {
                row[x] = 1;
                any = in = true;
            } else if (in) {
                break;
            }
        }
    }
    if (any) {
        _minX = std::min(_minX, x0), _maxX = std::max(_maxX, x1);
        _minY = std::min(_minY, y0), _maxY = std::max(_maxY, y1);
    }
}

void ShadowStencil::clear() {
    if (_maxX < _minX)
        return;
    for (int y = _minY; y <= _maxY; y++) {
        const size_t row = size_t(y) * size_t(_w);
        std::fill(_stencil.begin() + ptrdiff_t(row + size_t(_minX)),
                  _stencil.begin() + ptrdiff_t(row + size_t(_maxX) + 1), uint8_t(0));
        std::fill(_depth.begin() + ptrdiff_t(row + size_t(_minX)), _depth.begin() + ptrdiff_t(row + size_t(_maxX) + 1),
                  kFar);
    }
    _minX = _minY = 1 << 30;
    _maxX = _maxY = -1;
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
    const int x1 = std::min(_w - 1, int(std::ceil(std::max({a[0], b[0], c[0]}))));
    const int y0 = std::max(0, int(std::floor(std::min({a[1], b[1], c[1]}))));
    const int y1 = std::min(_h - 1, int(std::ceil(std::max({a[1], b[1], c[1]}))));
    if (x1 < x0 || y1 < y0)
        return;
    const float inv = 1.0f / area;
    bool any = false;
    for (int y = y0; y <= y1; y++) {
        const float py = float(y) + 0.5f;
        const size_t row = size_t(y) * size_t(_w);
        // The edges' parts that don't change along the row, worked out
        // once: the same sums as edge() per pixel.
        const float ra = (c[0] - b[0]) * (py - b[1]);
        const float rb = (a[0] - c[0]) * (py - c[1]);
        const float rc = (b[0] - a[0]) * (py - a[1]);
        bool in = false;
        for (int x = x0; x <= x1; x++) {
            const float px = float(x) + 0.5f;
            // Each corner's weight: the opposite edge's share of the area.
            const float wa = (ra - (c[1] - b[1]) * (px - b[0])) * inv;
            const float wb = (rb - (a[1] - c[1]) * (px - c[0])) * inv;
            const float wc = (rc - (b[1] - a[1]) * (px - a[0])) * inv;
            if (wa < 0 || wb < 0 || wc < 0) {
                // Each weight only changes sign once along a row, so the
                // pixels inside are one run: done once it's left.
                if (in)
                    break;
                continue;
            }
            in = true;
            const float z = wa * a[2] + wb * b[2] + wc * c[2];
            float &d = _depth[row + size_t(x)];
            if (z > d)
                continue;
            _stencil[row + size_t(x)] = ref;
            if (writeDepth)
                d = z;
            any = true;
        }
    }
    if (any) {
        _minX = std::min(_minX, x0), _maxX = std::max(_maxX, x1);
        _minY = std::min(_minY, y0), _maxY = std::max(_maxY, y1);
    }
}

void ShadowStencil::reset(const std::vector<uint8_t> &cover) {
    if (_maxX < _minX || cover.size() != _stencil.size())
        return;
    for (int y = _minY; y <= _maxY; y++)
        for (int x = _minX; x <= _maxX; x++) {
            const size_t i = size_t(y) * size_t(_w) + size_t(x);
            if (cover[i])
                _stencil[i] = 0;
        }
}

void ShadowStencil::select(uint8_t ref, CoverageMask &out) const {
    out.clear();
    if (_maxX < _minX)
        return;
    for (int y = _minY; y <= _maxY; y++)
        for (int x = _minX; x <= _maxX; x++)
            if (_stencil[size_t(y) * size_t(_w) + size_t(x)] == ref)
                out.set(x, y);
}

} // namespace reluva::awe
