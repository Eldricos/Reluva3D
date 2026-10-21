// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/collision.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

// Moves stop this far short of what they hit, so the next move doesn't
// start inside it (the original's value).
constexpr float kClipEpsilon = 0.125f;

float dot(const Vec3f& a, const Vec3f& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

} // namespace

// A sweep of a box centred on the line start-end (the box made symmetric,
// the line moved to its centre), and what it hit so far.
struct CollisionWorld::Work {
    Vec3f start{}, end{};
    Vec3f extents{}; // half the box's size
    Vec3f centre{};  // the box's centre relative to the traced point
    int32_t mask = 0;
    TraceResult result;
    uint32_t id = 0;

    // How far the box reaches beyond its centre along a plane's normal.
    float offset(const Vec3f& n) const {
        return std::abs(n[0]) * extents[0] + std::abs(n[1]) * extents[1] + std::abs(n[2]) * extents[2];
    }
};

CollisionWorld::CollisionWorld(const Bsp& bsp) : bsp_(bsp), checked_(bsp.brushes.size(), 0) {}

int32_t CollisionWorld::brushContents(int brush) const {
    const int shader = bsp_.brushes[size_t(brush)].shader;
    return shader >= 0 && size_t(shader) < bsp_.shaders.size() ? bsp_.shaders[size_t(shader)].contentFlags : 0;
}

void CollisionWorld::traceBrush(Work& w, int brush) const {
    const BspBrush& b = bsp_.brushes[size_t(brush)];
    if (b.numSides <= 0 || !(brushContents(brush) & w.mask))
        return;
    // Clip the line against each side's plane, pushed out by the box:
    // the line is inside the brush between the last plane it enters
    // and the first it leaves.
    float enter = -1, leave = 1;
    const BspBrushSide* hitSide = nullptr;
    bool startsOut = false, endsOut = false;
    for (int s = 0; s < b.numSides; ++s) {
        const BspBrushSide& side = bsp_.brushSides[size_t(b.firstSide + s)];
        const BspPlane& p = bsp_.planes[size_t(side.plane)];
        const float dist = p.dist + w.offset(p.normal);
        const float d1 = dot(w.start, p.normal) - dist;
        const float d2 = dot(w.end, p.normal) - dist;
        startsOut |= d1 > 0;
        endsOut |= d2 > 0;
        // Wholly in front of this plane: outside the brush.
        if (d1 > 0 && (d2 >= kClipEpsilon || d2 >= d1))
            return;
        if (d1 <= 0 && d2 <= 0)
            continue;
        if (d1 > d2) { // entering
            const float f = (d1 - kClipEpsilon) / (d1 - d2);
            if (f > enter) {
                enter = f;
                hitSide = &side;
            }
        } else { // leaving
            const float f = (d1 + kClipEpsilon) / (d1 - d2);
            leave = std::min(leave, f);
        }
    }
    if (!startsOut) {
        w.result.startSolid = true;
        if (!endsOut) {
            w.result.allSolid = true;
            w.result.fraction = 0;
            w.result.contents = brushContents(brush);
        }
        return;
    }
    if (enter < leave && enter > -1 && enter < w.result.fraction && hitSide) {
        const BspPlane& p = bsp_.planes[size_t(hitSide->plane)];
        w.result.fraction = std::max(enter, 0.0f);
        w.result.normal = p.normal;
        w.result.dist = p.dist;
        const int shader = hitSide->shader >= 0 ? hitSide->shader : b.shader;
        w.result.surfaceFlags =
            shader >= 0 && size_t(shader) < bsp_.shaders.size() ? bsp_.shaders[size_t(shader)].surfaceFlags : 0;
        w.result.contents = brushContents(brush);
    }
}

void CollisionWorld::traceNode(Work& w, int node) const {
    while (node >= 0) {
        const BspNode& n = bsp_.nodes[size_t(node)];
        const BspPlane& p = bsp_.planes[size_t(n.plane)];
        const float offset = w.offset(p.normal) + 1; // a little more, for the clip epsilon
        const float d1 = dot(w.start, p.normal) - p.dist;
        const float d2 = dot(w.end, p.normal) - p.dist;
        if (d1 >= offset && d2 >= offset) {
            node = n.children[0];
        } else if (d1 < -offset && d2 < -offset) {
            node = n.children[1];
        } else {
            // The swept box crosses the plane: both sides.
            traceNode(w, n.children[0]);
            if (w.result.allSolid)
                return;
            node = n.children[1];
        }
    }
    const BspLeaf& leaf = bsp_.leafs[size_t(-1 - node)];
    for (int i = 0; i < leaf.numLeafBrushes; ++i) {
        const int brush = bsp_.leafBrushes[size_t(leaf.firstLeafBrush + i)];
        if (checked_[size_t(brush)] == w.id)
            continue; // already tested from another leaf
        checked_[size_t(brush)] = w.id;
        traceBrush(w, brush);
        if (w.result.allSolid)
            return;
    }
}

TraceResult CollisionWorld::trace(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs,
                                  int model, int32_t mask) const {
    Work w;
    for (int i = 0; i < 3; ++i) {
        w.centre[size_t(i)] = (mins[size_t(i)] + maxs[size_t(i)]) * 0.5f;
        w.extents[size_t(i)] = (maxs[size_t(i)] - mins[size_t(i)]) * 0.5f;
        w.start[size_t(i)] = start[size_t(i)] + w.centre[size_t(i)];
        w.end[size_t(i)] = end[size_t(i)] + w.centre[size_t(i)];
    }
    w.mask = mask;
    w.id = ++traceCount_;
    if (w.id == 0) { // wrapped: forget what was tested
        std::fill(checked_.begin(), checked_.end(), 0);
        w.id = ++traceCount_;
    }
    if (model >= 0 && size_t(model) < bsp_.models.size()) {
        if (model == 0 && !bsp_.nodes.empty()) {
            traceNode(w, 0);
        } else {
            const BspModel& m = bsp_.models[size_t(model)];
            for (int i = 0; i < m.numBrushes && !w.result.allSolid; ++i)
                traceBrush(w, m.firstBrush + i);
        }
    }
    TraceResult r = w.result;
    for (int i = 0; i < 3; ++i)
        r.endPos[size_t(i)] = start[size_t(i)] + r.fraction * (end[size_t(i)] - start[size_t(i)]);
    return r;
}

TraceResult CollisionWorld::traceBox(const Vec3f& start, const Vec3f& end, const Vec3f& mins, const Vec3f& maxs,
                                     const Vec3f& boxMins, const Vec3f& boxMaxs, int32_t boxContents) {
    // The box as a brush of six axial planes, in a world of its own.
    Bsp bsp;
    bsp.shaders.push_back({"box", 0, boxContents});
    for (int axis = 0; axis < 3; ++axis) {
        BspPlane hi, lo;
        hi.normal[size_t(axis)] = 1;
        hi.dist = boxMaxs[size_t(axis)];
        lo.normal[size_t(axis)] = -1;
        lo.dist = -boxMins[size_t(axis)];
        bsp.planes.push_back(hi);
        bsp.planes.push_back(lo);
    }
    for (int i = 0; i < 6; ++i)
        bsp.brushSides.push_back({i, 0});
    bsp.brushes.push_back({0, 6, 0});
    BspModel model;
    model.numBrushes = 1;
    bsp.models.push_back(model); // model 0, without nodes: its brushes directly
    const CollisionWorld world(bsp);
    return world.trace(start, end, mins, maxs, 0, boxContents ? boxContents : -1);
}

int CollisionWorld::leafAt(const Vec3f& point) const {
    if (bsp_.nodes.empty())
        return bsp_.leafs.empty() ? -1 : 0;
    int node = 0;
    while (node >= 0) {
        const BspNode& n = bsp_.nodes[size_t(node)];
        const BspPlane& p = bsp_.planes[size_t(n.plane)];
        node = n.children[dot(point, p.normal) - p.dist >= 0 ? 0 : 1];
    }
    return -1 - node;
}

int32_t CollisionWorld::pointContents(const Vec3f& point, int model) const {
    auto inside = [&](int brush) {
        const BspBrush& b = bsp_.brushes[size_t(brush)];
        for (int s = 0; s < b.numSides; ++s) {
            const BspPlane& p = bsp_.planes[size_t(bsp_.brushSides[size_t(b.firstSide + s)].plane)];
            if (dot(point, p.normal) - p.dist > 0)
                return false;
        }
        return b.numSides > 0;
    };
    int32_t result = 0;
    if (model < 0 || size_t(model) >= bsp_.models.size())
        return 0;
    if (model == 0 && !bsp_.nodes.empty()) {
        const int leaf = leafAt(point);
        if (leaf < 0)
            return 0;
        const BspLeaf& l = bsp_.leafs[size_t(leaf)];
        for (int i = 0; i < l.numLeafBrushes; ++i) {
            const int brush = bsp_.leafBrushes[size_t(l.firstLeafBrush + i)];
            if (inside(brush))
                result |= brushContents(brush);
        }
    } else {
        const BspModel& m = bsp_.models[size_t(model)];
        for (int i = 0; i < m.numBrushes; ++i)
            if (inside(m.firstBrush + i))
                result |= brushContents(m.firstBrush + i);
    }
    return result;
}

bool CollisionWorld::modelBounds(int model, Vec3f& mins, Vec3f& maxs) const {
    if (model < 0 || size_t(model) >= bsp_.models.size())
        return false;
    mins = bsp_.models[size_t(model)].mins;
    maxs = bsp_.models[size_t(model)].maxs;
    return true;
}

} // namespace reluva::idtech3
