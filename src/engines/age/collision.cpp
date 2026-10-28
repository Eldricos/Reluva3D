// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/collision.h"

#include <algorithm>
#include <limits>

namespace reluva::age {

namespace {

// The point of triangle a, b, c nearest p (from the regions of the
// triangle's plane: the textbook method of Ericson's Real-Time Collision
// Detection, section 5.1.5).
Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return a;
    const Vec3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = p - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1 / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

} // namespace

void CollisionWorld::add(const Vec3& a, const Vec3& b, const Vec3& c, Surface surface) {
    const Vec3 n = normalized(cross(b - a, c - a));
    if (length(n) < 0.5f)
        return; // degenerate
    triangles_.push_back({a, b, c, n, surface});
}

void CollisionWorld::setMoving(const std::vector<std::array<Vec3, 3>>& triangles, Surface surface) {
    moving_.clear();
    for (const auto& t : triangles) {
        const Vec3 n = normalized(cross(t[1] - t[0], t[2] - t[0]));
        if (length(n) >= 0.5f)
            moving_.push_back({t[0], t[1], t[2], n, surface});
    }
}

void CollisionWorld::build(float cellSize) {
    cellSize_ = cellSize;
    grid_.clear();
    for (uint32_t i = 0; i < triangles_.size(); ++i) {
        const Triangle& t = triangles_[i];
        const int x0 = int(std::floor(std::min({t.a[0], t.b[0], t.c[0]}) / cellSize_));
        const int x1 = int(std::floor(std::max({t.a[0], t.b[0], t.c[0]}) / cellSize_));
        const int z0 = int(std::floor(std::min({t.a[2], t.b[2], t.c[2]}) / cellSize_));
        const int z1 = int(std::floor(std::max({t.a[2], t.b[2], t.c[2]}) / cellSize_));
        for (int ix = x0; ix <= x1; ++ix)
            for (int iz = z0; iz <= z1; ++iz)
                grid_[cellOf(ix, iz)].push_back(i);
    }
    seen_.assign(triangles_.size(), 0);
    query_ = 0;
}

template <typename F>
void CollisionWorld::forEachNear(float x0, float z0, float x1, float z1, F&& f) const {
    if (++query_ == 0) { // wrapped: forget every stamp
        std::fill(seen_.begin(), seen_.end(), 0);
        query_ = 1;
    }
    const int ix0 = int(std::floor(x0 / cellSize_)), ix1 = int(std::floor(x1 / cellSize_));
    const int iz0 = int(std::floor(z0 / cellSize_)), iz1 = int(std::floor(z1 / cellSize_));
    for (int ix = ix0; ix <= ix1; ++ix)
        for (int iz = iz0; iz <= iz1; ++iz) {
            auto it = grid_.find(cellOf(ix, iz));
            if (it == grid_.end())
                continue;
            for (uint32_t i : it->second)
                if (seen_[i] != query_) {
                    seen_[i] = query_;
                    f(triangles_[i]);
                }
        }
    for (const Triangle& t : moving_)
        if (std::max({t.a[0], t.b[0], t.c[0]}) >= x0 && std::min({t.a[0], t.b[0], t.c[0]}) <= x1 &&
            std::max({t.a[2], t.b[2], t.c[2]}) >= z0 && std::min({t.a[2], t.b[2], t.c[2]}) <= z1)
            f(t);
}

bool CollisionWorld::raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RayHit& hit) const {
    const Vec3 end = origin + direction * maxDistance;
    float best = maxDistance;
    bool found = false;
    forEachNear(std::min(origin[0], end[0]), std::min(origin[2], end[2]), std::max(origin[0], end[0]),
                std::max(origin[2], end[2]), [&](const Triangle& t) {
                    // Moller and Trumbore's ray-triangle intersection.
                    const Vec3 e1 = t.b - t.a, e2 = t.c - t.a;
                    const Vec3 p = cross(direction, e2);
                    const float det = dot(e1, p);
                    if (std::fabs(det) < 1e-9f)
                        return;
                    const float inv = 1 / det;
                    const Vec3 s = origin - t.a;
                    const float u = dot(s, p) * inv;
                    if (u < 0 || u > 1)
                        return;
                    const Vec3 q = cross(s, e1);
                    const float v = dot(direction, q) * inv;
                    if (v < 0 || u + v > 1)
                        return;
                    const float d = dot(e2, q) * inv;
                    if (d < 0 || d >= best)
                        return;
                    best = d;
                    found = true;
                    hit.distance = d;
                    hit.point = origin + direction * d;
                    hit.normal = dot(t.normal, direction) > 0 ? -t.normal : t.normal;
                    hit.surface = t.surface;
                });
    return found;
}

bool CollisionWorld::sphereContact(const Vec3& center, float radius, Contact& contact) const {
    contact = Contact{};
    bool found = false;
    forEachNear(center[0] - radius, center[2] - radius, center[0] + radius, center[2] + radius,
                [&](const Triangle& t) {
                    const Vec3 closest = closestOnTriangle(center, t.a, t.b, t.c);
                    const Vec3 away = center - closest;
                    const float d = length(away);
                    if (d >= radius || radius - d <= contact.depth)
                        return;
                    // On the triangle's face, out of its plane on the
                    // centre's side; otherwise away from the edge.
                    contact.normal = d > 1e-5f ? away * (1 / d)
                                               : (dot(t.normal, center - t.a) >= 0 ? t.normal : -t.normal);
                    contact.depth = radius - d;
                    contact.surface = t.surface;
                    found = true;
                });
    return found;
}

} // namespace reluva::age
