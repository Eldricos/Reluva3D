// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/server/server_world.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

constexpr int kTreeDepth = 4; // as the original's (16 leafs)

bool overlaps(const Vec3f &aMin, const Vec3f &aMax, const Vec3f &bMin, const Vec3f &bMax) {
    for (int i = 0; i < 3; ++i)
        if (aMin[size_t(i)] > bMax[size_t(i)] || aMax[size_t(i)] < bMin[size_t(i)])
            return false;
    return true;
}

} // namespace

void ServerWorld::setLevel(const Bsp *map, const CollisionWorld *world, const AreaPortals *portals,
                           std::function<bool()> noAreas) {
    _map = map;
    _world = world;
    _portals = portals;
    _noAreas = std::move(noAreas);
    clear();
}

void ServerWorld::clear() {
    _linked.clear();
    _placements.clear();
    _nodeOf.clear();
    _unplaced.clear();
    _nodes.clear();
    // The tree over the level's bounds (its first model's), each node
    // split across its longer side.
    Vec3f mins{-65536, -65536, -65536};
    Vec3f maxs{65536, 65536, 65536};
    if (_map && !_map->models.empty())
        for (int i = 0; i < 3; ++i) {
            mins[size_t(i)] = _map->models[0].mins[size_t(i)];
            maxs[size_t(i)] = _map->models[0].maxs[size_t(i)];
        }
    build(0, mins, maxs);
}

void ServerWorld::build(int depth, const Vec3f &mins, const Vec3f &maxs) {
    const int index = int(_nodes.size());
    _nodes.emplace_back();
    if (depth == kTreeDepth)
        return;
    const int axis = maxs[0] - mins[0] > maxs[1] - mins[1] ? 0 : 1;
    const float dist = 0.5f * (maxs[size_t(axis)] + mins[size_t(axis)]);
    _nodes[size_t(index)].axis = axis;
    _nodes[size_t(index)].dist = dist;
    Vec3f upperMin = mins;
    Vec3f lowerMax = maxs;
    upperMin[size_t(axis)] = dist;
    lowerMax[size_t(axis)] = dist;
    _nodes[size_t(index)].children[0] = int(_nodes.size());
    build(depth + 1, upperMin, maxs);
    _nodes[size_t(index)].children[1] = int(_nodes.size());
    build(depth + 1, mins, lowerMax);
}

ServerWorld::Placement ServerWorld::place(const Vec3f &absMin, const Vec3f &absMax) const {
    Placement p;
    p.absMin = absMin;
    p.absMax = absMax;
    if (!_world)
        return p;
    std::vector<int> leafs;
    _world->boxLeafs(absMin, absMax, leafs);
    p.leafCount = leafs.size();
    for (const int leaf : leafs) {
        // The first area it's in, and a second if it stands in two (a door
        // between them).
        const int area = _world->leafArea(leaf);
        if (area >= 0) {
            if (p.areas.first != -1 && p.areas.first != area)
                p.areas.second = area;
            else
                p.areas.first = area;
        }
        const int cluster =
            _map && leaf >= 0 && size_t(leaf) < _map->leafs.size() ? _map->leafs[size_t(leaf)].cluster : -1;
        if (cluster >= 0 && std::find(p.clusters.begin(), p.clusters.end(), cluster) == p.clusters.end())
            p.clusters.push_back(cluster);
    }
    return p;
}

void ServerWorld::remove(int32_t n) {
    if (const auto it = _nodeOf.find(n); it != _nodeOf.end()) {
        auto &list = _nodes[size_t(it->second)].entities;
        list.erase(std::find(list.begin(), list.end(), n));
        _nodeOf.erase(it);
    }
    _unplaced.erase(std::remove(_unplaced.begin(), _unplaced.end(), n), _unplaced.end());
    _placements.erase(n);
}

void ServerWorld::link(int32_t n, const EntityShape &s, Vec3f &absMin, Vec3f &absMax) {
    remove(n);
    _linked.insert(n);
    if (s.bmodel && (s.angles[0] || s.angles[1] || s.angles[2])) {
        float radius = 0;
        for (int i = 0; i < 3; ++i) {
            const float m = std::max(std::abs(s.mins[size_t(i)]), std::abs(s.maxs[size_t(i)]));
            radius += m * m;
        }
        radius = std::sqrt(radius);
        for (int i = 0; i < 3; ++i) {
            absMin[size_t(i)] = s.origin[size_t(i)] - radius;
            absMax[size_t(i)] = s.origin[size_t(i)] + radius;
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            absMin[size_t(i)] = s.origin[size_t(i)] + s.mins[size_t(i)];
            absMax[size_t(i)] = s.origin[size_t(i)] + s.maxs[size_t(i)];
        }
    }
    // A unit larger all round: moves stop just short of edges.
    for (int i = 0; i < 3; ++i) {
        absMin[size_t(i)] -= 1;
        absMax[size_t(i)] += 1;
    }
    _placements[n] = place(absMin, absMax);
    // Into the lowest node of the tree that holds it whole.
    int node = 0;
    while (_nodes[size_t(node)].axis >= 0) {
        const Node &nd = _nodes[size_t(node)];
        if (absMin[size_t(nd.axis)] > nd.dist)
            node = nd.children[0];
        else if (absMax[size_t(nd.axis)] < nd.dist)
            node = nd.children[1];
        else
            break;
    }
    _nodes[size_t(node)].entities.push_back(n);
    _nodeOf[n] = node;
}

void ServerWorld::linkUnplaced(int32_t n) {
    remove(n);
    _linked.insert(n);
    Placement p;
    p.everywhere = true;
    _placements[n] = p;
    _unplaced.push_back(n);
}

void ServerWorld::unlink(int32_t n) {
    remove(n);
    _linked.erase(n);
}

const ServerWorld::Placement *ServerWorld::placement(int32_t n) const {
    const auto it = _placements.find(n);
    return it == _placements.end() ? nullptr : &it->second;
}

bool ServerWorld::areasConnected(int area1, int area2) const {
    if (_noAreas && _noAreas())
        return true;
    return _portals && _portals->connected(area1, area2);
}

void ServerWorld::collect(const Vec3f &mins, const Vec3f &maxs, std::vector<int32_t> &out) const {
    out.clear();
    if (_nodes.empty())
        return;
    int stack[2 * kTreeDepth + 2];
    int top = 0;
    stack[top++] = 0;
    while (top) {
        const Node &node = _nodes[size_t(stack[--top])];
        for (const int32_t n : node.entities) {
            const Placement &p = _placements.at(n);
            if (overlaps(p.absMin, p.absMax, mins, maxs))
                out.push_back(n);
        }
        if (node.axis < 0)
            continue;
        if (maxs[size_t(node.axis)] > node.dist)
            stack[top++] = node.children[0];
        if (mins[size_t(node.axis)] < node.dist)
            stack[top++] = node.children[1];
    }
    out.insert(out.end(), _unplaced.begin(), _unplaced.end());
    std::sort(out.begin(), out.end());
}

void ServerWorld::entitiesInBox(const Vec3f &mins, const Vec3f &maxs, std::vector<int32_t> &out) const {
    collect(mins, maxs, out);
}

TraceResult ServerWorld::trace(const Vec3f &start, const Vec3f &mins, const Vec3f &maxs, const Vec3f &end, int32_t pass,
                               int32_t mask, int32_t &hit, bool entities) const {
    TraceResult best;
    hit = kNone;
    if (_world) {
        best = _world->trace(start, end, mins, maxs, 0, mask);
        hit = best.fraction < 1 ? kWorld : kNone;
    }
    // The bounds of the whole move, for the entities near it.
    Vec3f moveMin{};
    Vec3f moveMax{};
    for (int i = 0; i < 3; ++i) {
        moveMin[size_t(i)] = std::min(start[size_t(i)], end[size_t(i)]) + mins[size_t(i)] - 1;
        moveMax[size_t(i)] = std::max(start[size_t(i)], end[size_t(i)]) + maxs[size_t(i)] + 1;
    }
    std::vector<int32_t> near;
    if (entities)
        collect(moveMin, moveMax, near);
    EntityShape passShape;
    const int32_t passOwner = pass >= 0 && _shape(pass, passShape) ? passShape.owner : -1;
    for (const int32_t n : near) {
        if (best.allSolid)
            break;
        EntityShape s;
        if (!_shape(n, s))
            continue;
        // Not the mover, nor what it owns or what owns it.
        if (n == pass || (pass >= 0 && (s.owner == pass || n == passOwner)))
            continue;
        if (!(s.contents & mask))
            continue;
        TraceResult t;
        if (s.bmodel && _world) {
            // Its brushes, where it stands now and turned as it is.
            t = _world->transformedTrace(start, end, mins, maxs, s.modelIndex, mask, s.origin, s.angles);
        } else {
            Vec3f boxMin = s.mins;
            Vec3f boxMax = s.maxs;
            for (int i = 0; i < 3; ++i) {
                boxMin[size_t(i)] += s.origin[size_t(i)];
                boxMax[size_t(i)] += s.origin[size_t(i)];
            }
            t = CollisionWorld::traceBox(start, end, mins, maxs, boxMin, boxMax, s.contents);
        }
        if (t.allSolid || t.startSolid) {
            best.allSolid |= t.allSolid;
            best.startSolid = true;
            hit = n;
        }
        if (t.fraction < best.fraction) {
            const bool startSolid = best.startSolid;
            best = t;
            best.startSolid |= startSolid;
            hit = n;
        }
    }
    for (int i = 0; i < 3; ++i)
        best.endPos[size_t(i)] = start[size_t(i)] + best.fraction * (end[size_t(i)] - start[size_t(i)]);
    return best;
}

int32_t ServerWorld::pointContents(const Vec3f &point, int32_t pass) const {
    int32_t result = _world ? _world->pointContents(point, 0) : 0;
    std::vector<int32_t> near;
    collect(point, point, near);
    for (const int32_t n : near) {
        EntityShape s;
        if (n == pass || !_shape(n, s))
            continue;
        if (s.bmodel && _world) {
            if (_world->transformedPointContents(point, s.modelIndex, s.origin, s.angles))
                result |= s.contents;
        } else {
            bool inside = true;
            for (int i = 0; i < 3; ++i)
                inside &= point[size_t(i)] >= s.origin[size_t(i)] + s.mins[size_t(i)] &&
                          point[size_t(i)] <= s.origin[size_t(i)] + s.maxs[size_t(i)];
            if (inside)
                result |= s.contents;
        }
    }
    return result;
}

int32_t ServerWorld::packedSolid(const EntityShape &s, int32_t brushModel) {
    if (s.bmodel)
        return brushModel;
    constexpr int32_t kSolidOrBody = 1 | 0x2000000;
    if (!(s.contents & kSolidOrBody))
        return 0;
    const auto clampByte = [](float v) { return std::clamp(int32_t(v), 1, 255); };
    return (clampByte(s.maxs[2] + 32) << 16) | (clampByte(-s.mins[2]) << 8) | clampByte(s.maxs[0]);
}

} // namespace reluva::idtech3
