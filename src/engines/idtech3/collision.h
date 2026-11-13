// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/bsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

using Vec3f = std::array<float, 3>;

// Content flags (the shaders' surfaceparms, as the games use them).
namespace contents {
constexpr int32_t kSolid = 1;
constexpr int32_t kLava = 8, kSlime = 16, kWater = 32, kFog = 64;
constexpr int32_t kPlayerClip = 0x10000, kMonsterClip = 0x20000;
constexpr int32_t kBody = 0x2000000, kCorpse = 0x4000000, kTrigger = 0x40000000;
} // namespace contents

struct TraceResult {
    bool allSolid = false;   // the whole move is inside something solid
    bool startSolid = false; // it starts inside something solid
    float fraction = 1;      // how far it got: 1 = all the way
    Vec3f endPos{};
    Vec3f normal{}; // the plane hit
    float dist = 0;
    int32_t surfaceFlags = 0;
    int32_t contents = 0; // of what was hit
};

// A map's solid parts for moving things through: a box (or a point) swept
// along a line is stopped by the brushes it meets, each a convex volume
// bounded by planes, found through the BSP tree. Brushes count when their
// contents share a bit with the mask. Curved surfaces (patches) collide
// too: each is cut into triangles once, each triangle a thin convex
// volume (its plane both ways, its edges, and its bounding box), found
// through the same leaves. See docs/engines/idtech3/formats/bsp.md.
class CollisionWorld {
public:
    explicit CollisionWorld(const Bsp &bsp);

    // Model 0 is the world, others brush entities' (*1, *2...), traced
    // in their own coordinates. mins/maxs give the box around the moving
    // point (zeros for a point).
    TraceResult trace(const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs, int model,
                      int32_t mask) const;
    // The contents of everything at a point.
    int32_t pointContents(const Vec3f &point, int model) const;
    // The same against brush model `model` standing at `origin`, turned by
    // `angles` (pitch, yaw, roll in degrees), as the original's
    // CM_TransformedBoxTrace: the box is moved by its middle (an uneven
    // box made even about it), and the move turned into the model's own
    // axes, but not the box (the original leaves it unturned: turning it
    // would spoil the brushes' bevels); the plane hit is turned back, and
    // the end found along the move in the world.
    TraceResult transformedTrace(const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs, int model,
                                 int32_t mask, const Vec3f &origin, const Vec3f &angles) const;
    int32_t transformedPointContents(const Vec3f &point, int model, const Vec3f &origin, const Vec3f &angles) const;
    // A turn's axes, as Quake III works them out from (pitch, yaw, roll):
    // forward, left and up, a point's coordinates in them its dot
    // products with each.
    static std::array<Vec3f, 3> turnAxes(const Vec3f &angles);
    // The leaf a point is in, for visibility.
    int leafAt(const Vec3f &point) const;
    // The leaves a box touches (inside or on their edges).
    void boxLeafs(const Vec3f &mins, const Vec3f &maxs, std::vector<int> &leafs) const;
    // The area a leaf is in (-1: none, as solid leaves).
    int leafArea(int leaf) const {
        return leaf >= 0 && size_t(leaf) < _bsp.leafs.size() ? _bsp.leafs[size_t(leaf)].area : -1;
    }
    // How many areas the map is cut into (by its area portals).
    int areaCount() const { return _areaCount; }
    bool modelBounds(int model, Vec3f &mins, Vec3f &maxs) const;
    int modelCount() const { return int(_bsp.models.size()); }
    const Bsp &bsp() const { return _bsp; }

    // A box of given contents, for entities that aren't brush models;
    // traced in the same way.
    static TraceResult traceBox(const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs,
                                const Vec3f &boxMins, const Vec3f &boxMaxs, int32_t boxContents);

private:
    struct Work; // one trace's state
    void traceNode(Work &w, int node) const;
    void traceBrush(Work &w, int brush) const;
    void tracePatch(Work &w, int surface) const;
    int32_t brushContents(int brush) const;
    void buildPatches();

    // A triangle of a patch, as planes bounding it.
    // (Its planes kept in place: a trace near a curve tests many facets.)
    struct Facet {
        std::array<BspPlane, 11> planes{}; // its plane both ways, 3 edges, 6 box sides
        int planeCount = 0;
        Vec3f mins{}, maxs{};
        void add(const BspPlane &p) { planes[size_t(planeCount++)] = p; }
    };
    // Neighbouring facets (as tessellated, a run along a row) under one
    // box, and runs of those under another, so a trace skips most of a
    // big curve at once.
    struct FacetGroup {
        Vec3f mins{}, maxs{};
        size_t first = 0, count = 0; // facets, or (for the outer level) runs
    };
    static constexpr size_t kFacetRun = 16;
    struct PatchCollide {
        int32_t contents = 0, surfaceFlags = 0;
        Vec3f mins{}, maxs{};
        size_t firstFacet = 0, numFacets = 0;
        size_t firstGroup = 0, numGroups = 0; // outer groups, in groups_
    };
    std::vector<FacetGroup> _runs;   // facets, kFacetRun at most each
    std::vector<FacetGroup> _groups; // runs, kFacetRun at most each

    const Bsp &_bsp;
    mutable std::vector<uint32_t> _checked;        // per brush: the trace that last tested it
    mutable std::vector<uint32_t> _checkedPatches; // per surface, likewise
    mutable uint32_t _traceCount = 0;
    int _areaCount = 0; // the highest area a leaf is in, + 1
    std::vector<Facet> _facets;
    std::vector<int> _patchOf; // per surface: its PatchCollide, -1 none
    std::vector<PatchCollide> _patches;
};

// Which of a map's areas join up, as the original keeps it: areas meet
// only through area portals (where doors stand), each closed until the
// game opens it (as many times as it's opened, less times closed); areas
// joined through open ones, step by step, are connected.
class AreaPortals {
public:
    explicit AreaPortals(int areaCount);
    // The portal between two areas opened, or closed again.
    void adjust(int area1, int area2, bool open);
    // Whether the areas join up (never for -1).
    bool connected(int area1, int area2) const;

private:
    void flood();
    int _count = 0;
    std::vector<int> _open;  // count_ x count_: times each portal is open
    std::vector<int> _group; // by area: areas with the same are connected
};

} // namespace reluva::idtech3
