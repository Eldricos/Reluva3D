// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

using Vec3f = std::array<float, 3>;

// Content flags (the shaders' surfaceparms, as the games use them).
namespace contents {
constexpr int32_t Solid = 1;
constexpr int32_t Lava = 8, Slime = 16, Water = 32, Fog = 64;
constexpr int32_t PlayerClip = 0x10000, MonsterClip = 0x20000;
constexpr int32_t Body = 0x2000000, Corpse = 0x4000000, Trigger = 0x40000000;
} // namespace contents

struct TraceResult {
    bool allSolid = false;   // the whole move is inside something solid
    bool startSolid = false; // it starts inside something solid
    float fraction = 1;      // how far it got: 1 = all the way
    Vec3f endPos{};
    Vec3f normal{};          // the plane hit
    float dist = 0;
    int32_t surfaceFlags = 0;
    int32_t contents = 0;    // of what was hit
};

// A map's solid parts for moving things through: a box (or a point) swept
// along a line is stopped by the brushes it meets, each a convex volume
// bounded by planes, found through the BSP tree. Brushes count when their
// contents share a bit with the mask. Curved surfaces (patches) don't
// collide yet. See docs/formats/idtech3/bsp.md.
class CollisionWorld {
public:
    explicit CollisionWorld(const Bsp& bsp);

    // Model 0 is the world, others brush entities' (*1, *2...), traced
    // in their own coordinates. mins/maxs give the box around the moving
    // point (zeros for a point).
    TraceResult trace(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs, int model,
                      int32_t mask) const;
    // The contents of everything at a point.
    int32_t pointContents(const Vec3f& point, int model) const;
    // The leaf a point is in, for visibility.
    int leafAt(const Vec3f& point) const;
    bool modelBounds(int model, Vec3f& mins, Vec3f& maxs) const;
    int modelCount() const { return int(bsp_.models.size()); }
    const Bsp& bsp() const { return bsp_; }

    // A box of given contents, for entities that aren't brush models;
    // traced in the same way.
    static TraceResult traceBox(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs,
                                const Vec3f& boxMins, const Vec3f& boxMaxs, int32_t boxContents);

private:
    struct Work; // one trace's state
    void traceNode(Work& w, int node) const;
    void traceBrush(Work& w, int brush) const;
    int32_t brushContents(int brush) const;

    const Bsp& bsp_;
    mutable std::vector<uint32_t> checked_; // per brush: the trace that last tested it
    mutable uint32_t traceCount_ = 0;
};

} // namespace reluva::idtech3
