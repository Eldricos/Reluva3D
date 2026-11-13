// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/collision.h"
#include "engines/idtech3/render/patch.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

// Moves stop this far short of what they hit, so the next move doesn't
// start inside it (the original's value).
constexpr float kClipEpsilon = 0.125f;

float dot(const Vec3f &a, const Vec3f &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

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
    float offset(const Vec3f &n) const {
        return std::abs(n[0]) * extents[0] + std::abs(n[1]) * extents[1] + std::abs(n[2]) * extents[2];
    }
};

CollisionWorld::CollisionWorld(const Bsp &bsp)
    : _bsp(bsp), _checked(bsp.brushes.size(), 0), _checkedPatches(bsp.surfaces.size(), 0) {
    for (const BspLeaf &l : _bsp.leafs)
        _areaCount = std::max(_areaCount, l.area + 1);
    buildPatches();
}

void CollisionWorld::buildPatches() {
    // Each solid patch (its shader's contents not empty) cut into
    // triangles, finer the more its control points bend, each triangle
    // kept as planes. A curve cut into n pieces strays from its chords by
    // about its bend / (2 n^2): twice the square root of the largest bend
    // in units (2 to 16 cuts per 3x3 block) keeps that under a tenth of a
    // unit, so sliding along a pillar doesn't bump over its facets.
    _patchOf.assign(_bsp.surfaces.size(), -1);
    auto sub = [](const Vec3f &a, const Vec3f &b) { return Vec3f{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto cross = [](const Vec3f &a, const Vec3f &b) {
        return Vec3f{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    };
    for (size_t s = 0; s < _bsp.surfaces.size(); ++s) {
        const BspSurface &surf = _bsp.surfaces[s];
        if (surf.type != SurfaceType::kPatch || surf.patchWidth < 3 || surf.patchHeight < 3)
            continue;
        if (surf.shader < 0 || size_t(surf.shader) >= _bsp.shaders.size())
            continue;
        const BspShader &shader = _bsp.shaders[size_t(surf.shader)];
        if (!shader.contentFlags)
            continue;
        const BspVertex *control = &_bsp.vertices[size_t(surf.firstVertex)];
        const int w = surf.patchWidth;
        const int h = surf.patchHeight;
        // Within a sixteenth of a unit of round (2 * sqrt(bend) cuts).
        const int level = patchLevel(control, w, h, 0.125f);
        std::vector<BspVertex> verts;
        std::vector<uint32_t> indices;
        tessellatePatch(control, w, h, level, verts, indices);
        PatchCollide pc;
        pc.contents = shader.contentFlags;
        pc.surfaceFlags = shader.surfaceFlags;
        pc.firstFacet = _facets.size();
        pc.mins = {1e9f, 1e9f, 1e9f};
        pc.maxs = {-1e9f, -1e9f, -1e9f};
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const Vec3f p[3] = {verts[indices[i]].pos, verts[indices[i + 1]].pos, verts[indices[i + 2]].pos};
            Vec3f n = cross(sub(p[1], p[0]), sub(p[2], p[0]));
            const float len = std::sqrt(dot(n, n));
            if (len < 1e-4f)
                continue; // degenerate
            for (float &c : n)
                c /= len;
            Facet f;
            // Its plane both ways, half a unit thick, so even a point
            // can't pass between them.
            const float d = dot(n, p[0]);
            f.add({n, d + 0.5f});
            f.add({{-n[0], -n[1], -n[2]}, -d + 0.5f});
            // Its edges, facing out, a tenth of a unit wide of them: the
            // edge planes lean with the triangle, so where two meet at an
            // angle a thin wedge would be left between them on the inside
            // of the bend (half a unit deep, as the triangles are thick).
            // Cut as finely as above, neighbours meet at small angles, so
            // that covers it, and the ridge it leaves on the outside of a
            // curve is about a hundredth of a unit: nothing to catch on.
            for (int e = 0; e < 3; ++e) {
                const Vec3f &a = p[e];
                const Vec3f &b = p[(e + 1) % 3];
                Vec3f en = cross(sub(b, a), n);
                const float el = std::sqrt(dot(en, en));
                if (el < 1e-6f)
                    continue;
                for (float &c : en)
                    c /= el;
                if (dot(en, p[(e + 2) % 3]) - dot(en, a) > 0)
                    for (float &c : en)
                        c = -c;
                f.add({en, dot(en, a) + 0.1f});
            }
            // And its bounding box, so a box's corners don't get past
            // its edges' planes.
            for (int axis = 0; axis < 3; ++axis) {
                const float lo = std::min({p[0][size_t(axis)], p[1][size_t(axis)], p[2][size_t(axis)]});
                const float hi = std::max({p[0][size_t(axis)], p[1][size_t(axis)], p[2][size_t(axis)]});
                f.mins[size_t(axis)] = lo - 0.5f;
                f.maxs[size_t(axis)] = hi + 0.5f;
                BspPlane up;
                BspPlane down;
                up.normal[size_t(axis)] = 1;
                up.dist = hi + 0.5f;
                down.normal[size_t(axis)] = -1;
                down.dist = -(lo - 0.5f);
                f.add(up);
                f.add(down);
                pc.mins[size_t(axis)] = std::min(pc.mins[size_t(axis)], f.mins[size_t(axis)]);
                pc.maxs[size_t(axis)] = std::max(pc.maxs[size_t(axis)], f.maxs[size_t(axis)]);
            }
            _facets.push_back(std::move(f));
        }
        pc.numFacets = _facets.size() - pc.firstFacet;
        // The facets in runs under boxes, and the runs likewise.
        auto bound = [](FacetGroup &g, const Vec3f &mins, const Vec3f &maxs) {
            for (size_t k = 0; k < 3; ++k) {
                g.mins[k] = std::min(g.mins[k], mins[k]);
                g.maxs[k] = std::max(g.maxs[k], maxs[k]);
            }
        };
        const size_t firstRun = _runs.size();
        for (size_t f = pc.firstFacet; f < pc.firstFacet + pc.numFacets; f += kFacetRun) {
            FacetGroup run{
                {1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, f, std::min(kFacetRun, pc.firstFacet + pc.numFacets - f)};
            for (size_t i = run.first; i < run.first + run.count; ++i)
                bound(run, _facets[i].mins, _facets[i].maxs);
            _runs.push_back(run);
        }
        pc.firstGroup = _groups.size();
        for (size_t r = firstRun; r < _runs.size(); r += kFacetRun) {
            FacetGroup group{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}, r, std::min(kFacetRun, _runs.size() - r)};
            for (size_t i = group.first; i < group.first + group.count; ++i)
                bound(group, _runs[i].mins, _runs[i].maxs);
            _groups.push_back(group);
        }
        pc.numGroups = _groups.size() - pc.firstGroup;
        if (pc.numFacets) {
            _patchOf[s] = int(_patches.size());
            _patches.push_back(pc);
        }
    }
}

void CollisionWorld::tracePatch(Work &w, int surface) const {
    const int index = _patchOf[size_t(surface)];
    if (index < 0)
        return;
    const PatchCollide &pc = _patches[size_t(index)];
    if (!(pc.contents & w.mask))
        return;
    // The swept box's bounds, to skip what it can't reach.
    Vec3f lo{};
    Vec3f hi{};
    for (size_t i = 0; i < 3; ++i) {
        lo[i] = std::min(w.start[i], w.end[i]) - w.extents[i] - 1;
        hi[i] = std::max(w.start[i], w.end[i]) + w.extents[i] + 1;
    }
    auto apart = [&](const Vec3f &mins, const Vec3f &maxs) {
        for (size_t i = 0; i < 3; ++i)
            if (mins[i] > hi[i] || maxs[i] < lo[i])
                return true;
        return false;
    };
    if (apart(pc.mins, pc.maxs))
        return;
    // Each facet as a brush's sides (traceBrush).
    auto testFacet = [&](const Facet &facet) {
        float enter = -1;
        float leave = 1;
        const BspPlane *hitPlane = nullptr;
        bool startsOut = false;
        for (int pi = 0; pi < facet.planeCount; ++pi) {
            const BspPlane &p = facet.planes[size_t(pi)];
            const float dist = p.dist + w.offset(p.normal);
            const float d1 = dot(w.start, p.normal) - dist;
            const float d2 = dot(w.end, p.normal) - dist;
            startsOut |= d1 > 0;
            if (d1 > 0 && (d2 >= kClipEpsilon || d2 >= d1))
                return; // outside it all the way
            if (d1 <= 0 && d2 <= 0)
                continue;
            if (d1 > d2) {
                const float frac = (d1 - kClipEpsilon) / (d1 - d2);
                if (frac > enter) {
                    enter = frac;
                    hitPlane = &p;
                }
            } else {
                leave = std::min(leave, (d1 + kClipEpsilon) / (d1 - d2));
            }
        }
        // Starting inside a patch (standing on one's edge): not held there,
        // as the original's patches only stop what enters them.
        if (!startsOut)
            return;
        if (enter < leave && enter > -1 && enter < w.result.fraction && hitPlane) {
            w.result.fraction = std::max(enter, 0.0f);
            w.result.normal = hitPlane->normal;
            w.result.dist = hitPlane->dist;
            w.result.surfaceFlags = pc.surfaceFlags;
            w.result.contents = pc.contents;
        }
    };
    for (size_t g = pc.firstGroup; g < pc.firstGroup + pc.numGroups; ++g) {
        const FacetGroup &group = _groups[g];
        if (apart(group.mins, group.maxs))
            continue;
        for (size_t r = group.first; r < group.first + group.count; ++r) {
            const FacetGroup &run = _runs[r];
            if (apart(run.mins, run.maxs))
                continue;
            for (size_t f = run.first; f < run.first + run.count; ++f)
                if (!apart(_facets[f].mins, _facets[f].maxs))
                    testFacet(_facets[f]);
        }
    }
}

int32_t CollisionWorld::brushContents(int brush) const {
    const int shader = _bsp.brushes[size_t(brush)].shader;
    return shader >= 0 && size_t(shader) < _bsp.shaders.size() ? _bsp.shaders[size_t(shader)].contentFlags : 0;
}

void CollisionWorld::traceBrush(Work &w, int brush) const {
    const BspBrush &b = _bsp.brushes[size_t(brush)];
    if (b.numSides <= 0 || !(brushContents(brush) & w.mask))
        return;
    // Clip the line against each side's plane, pushed out by the box:
    // the line is inside the brush between the last plane it enters
    // and the first it leaves.
    float enter = -1;
    float leave = 1;
    const BspBrushSide *hitSide = nullptr;
    bool startsOut = false;
    bool endsOut = false;
    for (int s = 0; s < b.numSides; ++s) {
        const BspBrushSide &side = _bsp.brushSides[size_t(b.firstSide + s)];
        const BspPlane &p = _bsp.planes[size_t(side.plane)];
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
        const BspPlane &p = _bsp.planes[size_t(hitSide->plane)];
        w.result.fraction = std::max(enter, 0.0f);
        w.result.normal = p.normal;
        w.result.dist = p.dist;
        const int shader = hitSide->shader >= 0 ? hitSide->shader : b.shader;
        w.result.surfaceFlags =
            shader >= 0 && size_t(shader) < _bsp.shaders.size() ? _bsp.shaders[size_t(shader)].surfaceFlags : 0;
        w.result.contents = brushContents(brush);
    }
}

void CollisionWorld::traceNode(Work &w, int node) const {
    while (node >= 0) {
        const BspNode &n = _bsp.nodes[size_t(node)];
        const BspPlane &p = _bsp.planes[size_t(n.plane)];
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
    const BspLeaf &leaf = _bsp.leafs[size_t(-1 - node)];
    for (int i = 0; i < leaf.numLeafBrushes; ++i) {
        const int brush = _bsp.leafBrushes[size_t(leaf.firstLeafBrush + i)];
        if (_checked[size_t(brush)] == w.id)
            continue; // already tested from another leaf
        _checked[size_t(brush)] = w.id;
        traceBrush(w, brush);
        if (w.result.allSolid)
            return;
    }
    if (_patches.empty())
        return;
    for (int i = 0; i < leaf.numLeafSurfaces; ++i) {
        const int surface = _bsp.leafSurfaces[size_t(leaf.firstLeafSurface + i)];
        if (surface < 0 || size_t(surface) >= _patchOf.size() || _patchOf[size_t(surface)] < 0 ||
            _checkedPatches[size_t(surface)] == w.id)
            continue;
        _checkedPatches[size_t(surface)] = w.id;
        tracePatch(w, surface);
    }
}

TraceResult CollisionWorld::trace(const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs, int model,
                                  int32_t mask) const {
    Work w;
    for (int i = 0; i < 3; ++i) {
        w.centre[size_t(i)] = (mins[size_t(i)] + maxs[size_t(i)]) * 0.5f;
        w.extents[size_t(i)] = (maxs[size_t(i)] - mins[size_t(i)]) * 0.5f;
        w.start[size_t(i)] = start[size_t(i)] + w.centre[size_t(i)];
        w.end[size_t(i)] = end[size_t(i)] + w.centre[size_t(i)];
    }
    w.mask = mask;
    w.id = ++_traceCount;
    if (w.id == 0) { // wrapped: forget what was tested
        std::fill(_checked.begin(), _checked.end(), 0);
        std::fill(_checkedPatches.begin(), _checkedPatches.end(), 0);
        w.id = ++_traceCount;
    }
    if (model >= 0 && size_t(model) < _bsp.models.size()) {
        if (model == 0 && !_bsp.nodes.empty()) {
            traceNode(w, 0);
        } else {
            const BspModel &m = _bsp.models[size_t(model)];
            for (int i = 0; i < m.numBrushes && !w.result.allSolid; ++i)
                traceBrush(w, m.firstBrush + i);
            for (int i = 0; i < m.numSurfaces && !w.result.allSolid; ++i)
                if (const int surface = m.firstSurface + i; surface >= 0 && size_t(surface) < _patchOf.size())
                    tracePatch(w, surface);
        }
    }
    TraceResult r = w.result;
    for (int i = 0; i < 3; ++i)
        r.endPos[size_t(i)] = start[size_t(i)] + r.fraction * (end[size_t(i)] - start[size_t(i)]);
    return r;
}

TraceResult CollisionWorld::traceBox(const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs,
                                     const Vec3f &boxMins, const Vec3f &boxMaxs, int32_t boxContents) {
    // The box as a brush of six axial planes (+x, -x, +y, -y, +z, -z),
    // clipped as traceBrush does, with nothing built: each plane's
    // normal picks out one coordinate, and the moving box reaches its
    // half size along it.
    TraceResult r;
    Vec3f from{};
    Vec3f to{};
    Vec3f extents{};
    for (int i = 0; i < 3; ++i) {
        const float centre = (mins[size_t(i)] + maxs[size_t(i)]) * 0.5f;
        extents[size_t(i)] = (maxs[size_t(i)] - mins[size_t(i)]) * 0.5f;
        from[size_t(i)] = start[size_t(i)] + centre;
        to[size_t(i)] = end[size_t(i)] + centre;
    }
    auto finish = [&] {
        for (int i = 0; i < 3; ++i)
            r.endPos[size_t(i)] = start[size_t(i)] + r.fraction * (end[size_t(i)] - start[size_t(i)]);
        return r;
    };
    if (boxContents == 0) // nothing to hit
        return finish();
    float enter = -1;
    float leave = 1;
    int hitPlane = -1; // axis * 2, + 1 for the low side
    bool startsOut = false;
    bool endsOut = false;
    for (int plane = 0; plane < 6; ++plane) {
        const int axis = plane / 2;
        const bool low = plane & 1;
        const float planeDist = low ? -boxMins[size_t(axis)] : boxMaxs[size_t(axis)];
        const float dist = planeDist + extents[size_t(axis)];
        const float d1 = (low ? -from[size_t(axis)] : from[size_t(axis)]) - dist;
        const float d2 = (low ? -to[size_t(axis)] : to[size_t(axis)]) - dist;
        startsOut |= d1 > 0;
        endsOut |= d2 > 0;
        if (d1 > 0 && (d2 >= kClipEpsilon || d2 >= d1))
            return finish();
        if (d1 <= 0 && d2 <= 0)
            continue;
        if (d1 > d2) {
            const float f = (d1 - kClipEpsilon) / (d1 - d2);
            if (f > enter) {
                enter = f;
                hitPlane = plane;
            }
        } else {
            leave = std::min(leave, (d1 + kClipEpsilon) / (d1 - d2));
        }
    }
    if (!startsOut) {
        r.startSolid = true;
        if (!endsOut) {
            r.allSolid = true;
            r.fraction = 0;
            r.contents = boxContents;
        }
        return finish();
    }
    if (enter < leave && enter > -1 && enter < r.fraction && hitPlane >= 0) {
        const int axis = hitPlane / 2;
        const bool low = hitPlane & 1;
        r.fraction = std::max(enter, 0.0f);
        r.normal = Vec3f{};
        r.normal[size_t(axis)] = low ? -1.0f : 1.0f;
        r.dist = low ? -boxMins[size_t(axis)] : boxMaxs[size_t(axis)];
        r.surfaceFlags = 0;
        r.contents = boxContents;
    }
    return finish();
}

int CollisionWorld::leafAt(const Vec3f &point) const {
    if (_bsp.nodes.empty())
        return _bsp.leafs.empty() ? -1 : 0;
    int node = 0;
    while (node >= 0) {
        const BspNode &n = _bsp.nodes[size_t(node)];
        const BspPlane &p = _bsp.planes[size_t(n.plane)];
        node = n.children[dot(point, p.normal) - p.dist >= 0 ? 0 : 1];
    }
    return -1 - node;
}

void CollisionWorld::boxLeafs(const Vec3f &mins, const Vec3f &maxs, std::vector<int> &leafs) const {
    if (_bsp.nodes.empty()) {
        if (!_bsp.leafs.empty())
            leafs.push_back(0);
        return;
    }
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int node = stack.back();
        stack.pop_back();
        if (node < 0) {
            leafs.push_back(-1 - node);
            continue;
        }
        const BspNode &n = _bsp.nodes[size_t(node)];
        const BspPlane &p = _bsp.planes[size_t(n.plane)];
        // The box's nearest and farthest corners from the plane.
        float lo = -p.dist;
        float hi = -p.dist;
        for (size_t k = 0; k < 3; ++k) {
            lo += p.normal[k] * (p.normal[k] >= 0 ? mins[k] : maxs[k]);
            hi += p.normal[k] * (p.normal[k] >= 0 ? maxs[k] : mins[k]);
        }
        if (hi >= 0)
            stack.push_back(n.children[0]);
        if (lo < 0)
            stack.push_back(n.children[1]);
    }
}

AreaPortals::AreaPortals(int areaCount)
    : _count(std::max(areaCount, 0)), _open(size_t(_count) * size_t(_count), 0), _group(size_t(_count), 0) {
    flood();
}

void AreaPortals::adjust(int area1, int area2, bool open) {
    if (area1 < 0 || area2 < 0 || area1 >= _count || area2 >= _count)
        return;
    int &a = _open[size_t(area1) * size_t(_count) + size_t(area2)];
    int &b = _open[size_t(area2) * size_t(_count) + size_t(area1)];
    if (open) {
        ++a;
        ++b;
    } else if (a > 0) { // the original stops on closing a closed one
        --a;
        --b;
    }
    flood();
}

bool AreaPortals::connected(int area1, int area2) const {
    if (area1 < 0 || area2 < 0 || area1 >= _count || area2 >= _count)
        return false;
    return _group[size_t(area1)] == _group[size_t(area2)];
}

void AreaPortals::flood() {
    std::fill(_group.begin(), _group.end(), -1);
    for (int start = 0; start < _count; ++start) {
        if (_group[size_t(start)] >= 0)
            continue;
        std::vector<int> stack{start};
        _group[size_t(start)] = start;
        while (!stack.empty()) {
            const int area = stack.back();
            stack.pop_back();
            for (int other = 0; other < _count; ++other)
                if (_group[size_t(other)] < 0 && _open[size_t(area) * size_t(_count) + size_t(other)] > 0) {
                    _group[size_t(other)] = start;
                    stack.push_back(other);
                }
        }
    }
}

std::array<Vec3f, 3> CollisionWorld::turnAxes(const Vec3f &angles) {
    constexpr float kRad = 3.14159265358979f / 180;
    const float sp = std::sin(angles[0] * kRad);
    const float cp = std::cos(angles[0] * kRad);
    const float sy = std::sin(angles[1] * kRad);
    const float cy = std::cos(angles[1] * kRad);
    const float sr = std::sin(angles[2] * kRad);
    const float cr = std::cos(angles[2] * kRad);
    const Vec3f forward{cp * cy, cp * sy, -sp};
    const Vec3f left{sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp};
    const Vec3f up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    return {forward, left, up};
}

namespace {

bool turned(const Vec3f &angles) {
    return angles[0] != 0 || angles[1] != 0 || angles[2] != 0;
}

Vec3f intoAxes(const std::array<Vec3f, 3> &axes, const Vec3f &v) {
    Vec3f out{};
    for (size_t r = 0; r < 3; ++r)
        out[r] = axes[r][0] * v[0] + axes[r][1] * v[1] + axes[r][2] * v[2];
    return out;
}

Vec3f outOfAxes(const std::array<Vec3f, 3> &axes, const Vec3f &v) {
    Vec3f out{};
    for (size_t k = 0; k < 3; ++k)
        out[k] = axes[0][k] * v[0] + axes[1][k] * v[1] + axes[2][k] * v[2];
    return out;
}

} // namespace

TraceResult CollisionWorld::transformedTrace(const Vec3f &start, const Vec3f &end, const Vec3f &mins, const Vec3f &maxs,
                                             int model, int32_t mask, const Vec3f &origin, const Vec3f &angles) const {
    Vec3f half{};
    Vec3f a{};
    Vec3f b{};
    for (size_t k = 0; k < 3; ++k) {
        const float middle = (mins[k] + maxs[k]) * 0.5f;
        half[k] = maxs[k] - middle;
        a[k] = start[k] + middle - origin[k];
        b[k] = end[k] + middle - origin[k];
    }
    const bool turn = model != 0 && turned(angles);
    std::array<Vec3f, 3> axes{};
    if (turn) {
        axes = turnAxes(angles);
        a = intoAxes(axes, a);
        b = intoAxes(axes, b);
    }
    const Vec3f negHalf{-half[0], -half[1], -half[2]};
    TraceResult t = trace(a, b, negHalf, half, model, mask);
    if (turn && t.fraction < 1)
        t.normal = outOfAxes(axes, t.normal);
    for (size_t k = 0; k < 3; ++k)
        t.endPos[k] = start[k] + t.fraction * (end[k] - start[k]);
    return t;
}

int32_t CollisionWorld::transformedPointContents(const Vec3f &point, int model, const Vec3f &origin,
                                                 const Vec3f &angles) const {
    Vec3f p{point[0] - origin[0], point[1] - origin[1], point[2] - origin[2]};
    if (model != 0 && turned(angles))
        p = intoAxes(turnAxes(angles), p);
    return pointContents(p, model);
}

int32_t CollisionWorld::pointContents(const Vec3f &point, int model) const {
    auto inside = [&](int brush) {
        const BspBrush &b = _bsp.brushes[size_t(brush)];
        for (int s = 0; s < b.numSides; ++s) {
            const BspPlane &p = _bsp.planes[size_t(_bsp.brushSides[size_t(b.firstSide + s)].plane)];
            if (dot(point, p.normal) - p.dist > 0)
                return false;
        }
        return b.numSides > 0;
    };
    int32_t result = 0;
    if (model < 0 || size_t(model) >= _bsp.models.size())
        return 0;
    if (model == 0 && !_bsp.nodes.empty()) {
        const int leaf = leafAt(point);
        if (leaf < 0)
            return 0;
        const BspLeaf &l = _bsp.leafs[size_t(leaf)];
        for (int i = 0; i < l.numLeafBrushes; ++i) {
            const int brush = _bsp.leafBrushes[size_t(l.firstLeafBrush + i)];
            if (inside(brush))
                result |= brushContents(brush);
        }
    } else {
        const BspModel &m = _bsp.models[size_t(model)];
        for (int i = 0; i < m.numBrushes; ++i)
            if (inside(m.firstBrush + i))
                result |= brushContents(m.firstBrush + i);
    }
    return result;
}

bool CollisionWorld::modelBounds(int model, Vec3f &mins, Vec3f &maxs) const {
    if (model < 0 || size_t(model) >= _bsp.models.size())
        return false;
    mins = _bsp.models[size_t(model)].mins;
    maxs = _bsp.models[size_t(model)].maxs;
    return true;
}

} // namespace reluva::idtech3
