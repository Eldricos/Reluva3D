// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/shadows.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

std::array<float, 3> shadowLight(const Ac2Scene& scene) {
    // 0x428250: D3DLIGHT8.Position (dd0, dd4, dd8) of each light; only
    // those with e1c set if any are. Type, direction and colour don't
    // count.
    std::vector<const Ac2Record*> lights, marked;
    for (const Ac2Record* o : scene.objects())
        if (o->tag == "LITE") {
            lights.push_back(o);
            if (o->i("e1c") != 0)
                marked.push_back(o);
        }
    const std::vector<const Ac2Record*>& use = marked.empty() ? lights : marked;
    std::array<float, 3> sum{0, 0, 0};
    for (const Ac2Record* l : use) {
        sum[0] += l->f("dd0");
        sum[1] += l->f("dd4");
        sum[2] += l->f("dd8");
    }
    const float n = use.empty() ? 1.0f : float(use.size());
    return {sum[0] / n, sum[1] / n + 1500.0f, sum[2] / n};
}

std::array<float, 3> flattenOntoFloor(const std::array<float, 3>& light, const float point[3]) {
    const float drop = light[1] - point[1];
    if (drop <= 1e-3f)
        return {point[0], 0, point[2]};
    const float s = light[1] / drop;
    return {light[0] + (point[0] - light[0]) * s, 0, light[2] + (point[2] - light[2]) * s};
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
    for (int y = y0; y <= y1; y++) {
        const float py = float(y) + 0.5f;
        uint8_t* row = bits_.data() + size_t(y) * size_t(w_);
        for (int x = x0; x <= x1; x++) {
            const float px = float(x) + 0.5f;
            if (sign * edge(a, b, px, py) >= 0 && sign * edge(b, c, px, py) >= 0 && sign * edge(c, a, px, py) >= 0) {
                row[x] = 1;
                any = true;
            }
        }
    }
    if (any) {
        minX_ = std::min(minX_, x0), maxX_ = std::max(maxX_, x1);
        minY_ = std::min(minY_, y0), maxY_ = std::max(maxY_, y1);
    }
}

} // namespace reluva::awe
