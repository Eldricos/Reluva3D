// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/vecmath.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace reluva::age {

// What a collision triangle is made of.
struct CollisionSurface {
    float friction = 0.5f, elasticity = 0.1f;
};

// Triangles to collide with, in a grid over X and Z for finding those
// near a point or along a short ray. Both sides of a triangle are solid.
class CollisionWorld {
public:
    using Surface = CollisionSurface;
    struct Triangle {
        Vec3 a, b, c;
        Vec3 normal; // by the winding a, b, c (right-handed)
        Surface surface;
    };

    void add(const Vec3& a, const Vec3& b, const Vec3& c, Surface surface = {});
    // Files the triangles into the grid; call after adding them all.
    void build(float cellSize = 8);
    size_t size() const { return triangles_.size(); }

    struct RayHit {
        float distance = 0;
        Vec3 point{}, normal{}; // the normal facing back along the ray
        Surface surface;
    };
    // The nearest triangle along a ray (`direction` of length 1) within
    // `maxDistance`.
    bool raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RayHit& hit) const;

    struct Contact {
        Vec3 normal{}; // out of the triangle, towards the sphere's centre
        float depth = 0;
        Surface surface;
    };
    // The deepest overlap of a sphere with the triangles.
    bool sphereContact(const Vec3& center, float radius, Contact& contact) const;

private:
    using Cell = uint64_t;
    Cell cellOf(int ix, int iz) const { return uint64_t(uint32_t(ix)) << 32 | uint32_t(iz); }
    // Each triangle near the box (X, Z), once.
    template <typename F>
    void forEachNear(float x0, float z0, float x1, float z1, F&& f) const;

    std::vector<Triangle> triangles_;
    std::unordered_map<Cell, std::vector<uint32_t>> grid_;
    float cellSize_ = 8;
    mutable std::vector<uint32_t> seen_; // per triangle, the query that last saw it
    mutable uint32_t query_ = 0;
};

} // namespace reluva::age
