// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/picking.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace reluva::awe {

int pick(const std::vector<PickCandidate>& c, bool itemInHand) {
    // The list by depth, back to front (a bubble sort: equal ones keep
    // their order, 0x435e40), then tried from the end.
    std::vector<size_t> order(c.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return c[a].depth < c[b].depth; });
    // 1: pickFirst, with a rectangle or an outline (0x435f23).
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if ((c[*it].rect || c[*it].outline) && c[*it].pickFirst && c[*it].hit)
            return int(*it);
    // 2: the characters: by their rectangle or outline if they have one,
    // else by their bones (0x435fd1).
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const PickCandidate& p = c[*it];
        if (!p.character)
            continue;
        const bool byBones = !p.rect && !p.outline;
        if (byBones && !itemInHand && !p.clickable)
            continue;
        if (byBones ? p.boneHit : p.hit)
            return int(*it);
    }
    // 3: everything.
    for (auto it = order.rbegin(); it != order.rend(); ++it)
        if (c[*it].hit)
            return int(*it);
    return -1;
}

bool inPolygon(const std::vector<std::pair<uint32_t, uint32_t>>& points, int x, int y) {
    // The winding number about the pixel's centre.
    if (points.size() < 3)
        return false;
    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
    int winding = 0;
    for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const float x0 = float(int32_t(points[j].first)), y0 = float(int32_t(points[j].second));
        const float x1 = float(int32_t(points[i].first)), y1 = float(int32_t(points[i].second));
        const float side = (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0);
        if (y0 <= py) {
            if (y1 > py && side > 0)
                winding++;
        } else if (y1 <= py && side < 0) {
            winding--;
        }
    }
    return winding != 0;
}

bool rayHitsBox(const gfx::Mat4& m, const float lo[3], const float hi[3], const gfx::Vec3& from, const gfx::Vec3& dir) {
    // The ray in the box's axes: through the inverse of m's 3x3 part (its
    // adjugate over its determinant), after taking off m's translation.
    auto a = [&](int r, int c) { return m[size_t(c * 4 + r)]; };
    float adj[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            const int r0 = (c + 1) % 3, r1 = (c + 2) % 3, c0 = (r + 1) % 3, c1 = (r + 2) % 3;
            adj[r][c] = a(r0, c0) * a(r1, c1) - a(r0, c1) * a(r1, c0);
        }
    const float det = a(0, 0) * adj[0][0] + a(0, 1) * adj[1][0] + a(0, 2) * adj[2][0];
    if (std::abs(det) < 1e-20f)
        return false;
    float o[3], d[3];
    const float rel[3] = {from[0] - a(0, 3), from[1] - a(1, 3), from[2] - a(2, 3)};
    for (int r = 0; r < 3; r++) {
        o[r] = (adj[r][0] * rel[0] + adj[r][1] * rel[1] + adj[r][2] * rel[2]) / det;
        d[r] = (adj[r][0] * dir[0] + adj[r][1] * dir[1] + adj[r][2] * dir[2]) / det;
    }
    // Then the slabs, ahead of the ray's start.
    float t0 = 0, t1 = 1e30f;
    for (int k = 0; k < 3; k++) {
        if (std::abs(d[k]) < 1e-12f) {
            if (o[k] < lo[k] || o[k] > hi[k])
                return false;
            continue;
        }
        float tNear = (lo[k] - o[k]) / d[k], tFar = (hi[k] - o[k]) / d[k];
        if (tNear > tFar)
            std::swap(tNear, tFar);
        t0 = std::max(t0, tNear), t1 = std::min(t1, tFar);
        if (t0 > t1)
            return false;
    }
    return true;
}

} // namespace reluva::awe
