// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/server/server_world.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {

constexpr int kTreeDepth = 4; // as the original's (16 leafs)

bool overlaps(const Vec3f& aMin, const Vec3f& aMax, const Vec3f& bMin, const Vec3f& bMax) {
    for (int i = 0; i < 3; ++i)
        if (aMin[size_t(i)] > bMax[size_t(i)] || aMax[size_t(i)] < bMin[size_t(i)])
            return false;
    return true;
}

} // namespace

void ServerWorld::setLevel(const Bsp* map, const CollisionWorld* world, const AreaPortals* portals,
                           std::function<bool()> noAreas) {
    map_ = map;
    world_ = world;
    portals_ = portals;
    noAreas_ = std::move(noAreas);
    clear();
}

void ServerWorld::clear() {
    linked_.clear();
    placements_.clear();
    nodeOf_.clear();
    unplaced_.clear();
    nodes_.clear();
    // The tree over the level's bounds (its first model's), each node
    // split across its longer side.
    Vec3f mins{-65536, -65536, -65536}, maxs{65536, 65536, 65536};
    if (map_ && !map_->models.empty())
        for (int i = 0; i < 3; ++i) {
            mins[size_t(i)] = map_->models[0].mins[size_t(i)];
            maxs[size_t(i)] = map_->models[0].maxs[size_t(i)];
        }
    build(0, mins, maxs);
}

void ServerWorld::build(int depth, const Vec3f& mins, const Vec3f& maxs) {
    const int index = int(nodes_.size());
    nodes_.emplace_back();
    if (depth == kTreeDepth)
        return;
    const int axis = maxs[0] - mins[0] > maxs[1] - mins[1] ? 0 : 1;
    const float dist = 0.5f * (maxs[size_t(axis)] + mins[size_t(axis)]);
    nodes_[size_t(index)].axis = axis;
    nodes_[size_t(index)].dist = dist;
    Vec3f upperMin = mins, lowerMax = maxs;
    upperMin[size_t(axis)] = dist;
    lowerMax[size_t(axis)] = dist;
    nodes_[size_t(index)].children[0] = int(nodes_.size());
    build(depth + 1, upperMin, maxs);
    nodes_[size_t(index)].children[1] = int(nodes_.size());
    build(depth + 1, mins, lowerMax);
}

ServerWorld::Placement ServerWorld::place(const Vec3f& absMin, const Vec3f& absMax) const {
    Placement p;
    p.absMin = absMin;
    p.absMax = absMax;
    if (!world_)
        return p;
    std::vector<int> leafs;
    world_->boxLeafs(absMin, absMax, leafs);
    p.leafCount = leafs.size();
    for (const int leaf : leafs) {
        // The first area it's in, and a second if it stands in two (a door
        // between them).
        const int area = world_->leafArea(leaf);
        if (area >= 0) {
            if (p.areas.first != -1 && p.areas.first != area)
                p.areas.second = area;
            else
                p.areas.first = area;
        }
        const int cluster = map_ && leaf >= 0 && size_t(leaf) < map_->leafs.size() ? map_->leafs[size_t(leaf)].cluster
                                                                                 : -1;
        if (cluster >= 0 && std::find(p.clusters.begin(), p.clusters.end(), cluster) == p.clusters.end())
            p.clusters.push_back(cluster);
    }
    return p;
}

void ServerWorld::remove(int32_t n) {
    if (const auto it = nodeOf_.find(n); it != nodeOf_.end()) {
        auto& list = nodes_[size_t(it->second)].entities;
        list.erase(std::find(list.begin(), list.end(), n));
        nodeOf_.erase(it);
    }
    unplaced_.erase(std::remove(unplaced_.begin(), unplaced_.end(), n), unplaced_.end());
    placements_.erase(n);
}

void ServerWorld::link(int32_t n, const EntityShape& s, Vec3f& absMin, Vec3f& absMax) {
    remove(n);
    linked_.insert(n);
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
    placements_[n] = place(absMin, absMax);
    // Into the lowest node of the tree that holds it whole.
    int node = 0;
    while (nodes_[size_t(node)].axis >= 0) {
        const Node& nd = nodes_[size_t(node)];
        if (absMin[size_t(nd.axis)] > nd.dist)
            node = nd.children[0];
        else if (absMax[size_t(nd.axis)] < nd.dist)
            node = nd.children[1];
        else
            break;
    }
    nodes_[size_t(node)].entities.push_back(n);
    nodeOf_[n] = node;
}

void ServerWorld::linkUnplaced(int32_t n) {
    remove(n);
    linked_.insert(n);
    Placement p;
    p.everywhere = true;
    placements_[n] = p;
    unplaced_.push_back(n);
}

void ServerWorld::unlink(int32_t n) {
    remove(n);
    linked_.erase(n);
}

const ServerWorld::Placement* ServerWorld::placement(int32_t n) const {
    const auto it = placements_.find(n);
    return it == placements_.end() ? nullptr : &it->second;
}

bool ServerWorld::areasConnected(int area1, int area2) const {
    if (noAreas_ && noAreas_())
        return true;
    return portals_ && portals_->connected(area1, area2);
}

void ServerWorld::collect(const Vec3f& mins, const Vec3f& maxs, std::vector<int32_t>& out) const {
    out.clear();
    if (nodes_.empty())
        return;
    int stack[2 * kTreeDepth + 2];
    int top = 0;
    stack[top++] = 0;
    while (top) {
        const Node& node = nodes_[size_t(stack[--top])];
        for (const int32_t n : node.entities) {
            const Placement& p = placements_.at(n);
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
    out.insert(out.end(), unplaced_.begin(), unplaced_.end());
    std::sort(out.begin(), out.end());
}

void ServerWorld::entitiesInBox(const Vec3f& mins, const Vec3f& maxs, std::vector<int32_t>& out) const {
    collect(mins, maxs, out);
}

TraceResult ServerWorld::trace(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                               int32_t pass, int32_t mask, int32_t& hit, bool entities) const {
    TraceResult best;
    hit = kNone;
    if (world_) {
        best = world_->trace(start, end, mins, maxs, 0, mask);
        hit = best.fraction < 1 ? kWorld : kNone;
    }
    // The bounds of the whole move, for the entities near it.
    Vec3f moveMin{}, moveMax{};
    for (int i = 0; i < 3; ++i) {
        moveMin[size_t(i)] = std::min(start[size_t(i)], end[size_t(i)]) + mins[size_t(i)] - 1;
        moveMax[size_t(i)] = std::max(start[size_t(i)], end[size_t(i)]) + maxs[size_t(i)] + 1;
    }
    std::vector<int32_t> near;
    if (entities)
        collect(moveMin, moveMax, near);
    EntityShape passShape;
    const int32_t passOwner = pass >= 0 && shape_(pass, passShape) ? passShape.owner : -1;
    for (const int32_t n : near) {
        if (best.allSolid)
            break;
        EntityShape s;
        if (!shape_(n, s))
            continue;
        // Not the mover, nor what it owns or what owns it.
        if (n == pass || (pass >= 0 && (s.owner == pass || n == passOwner)))
            continue;
        if (!(s.contents & mask))
            continue;
        TraceResult t;
        if (s.bmodel && world_) {
            // Its brushes, where it stands now (turning not yet followed).
            Vec3f a = start, b = end;
            for (int i = 0; i < 3; ++i) {
                a[size_t(i)] -= s.origin[size_t(i)];
                b[size_t(i)] -= s.origin[size_t(i)];
            }
            t = world_->trace(a, b, mins, maxs, s.modelIndex, mask);
            for (int i = 0; i < 3; ++i)
                t.endPos[size_t(i)] += s.origin[size_t(i)];
        } else {
            Vec3f boxMin = s.mins, boxMax = s.maxs;
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

int32_t ServerWorld::pointContents(const Vec3f& point, int32_t pass) const {
    int32_t result = world_ ? world_->pointContents(point, 0) : 0;
    std::vector<int32_t> near;
    collect(point, point, near);
    for (const int32_t n : near) {
        EntityShape s;
        if (n == pass || !shape_(n, s))
            continue;
        if (s.bmodel && world_) {
            const Vec3f local{point[0] - s.origin[0], point[1] - s.origin[1], point[2] - s.origin[2]};
            if (world_->pointContents(local, s.modelIndex))
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

int32_t ServerWorld::packedSolid(const EntityShape& s, int32_t brushModel) {
    if (s.bmodel)
        return brushModel;
    constexpr int32_t kSolidOrBody = 1 | 0x2000000;
    if (!(s.contents & kSolidOrBody))
        return 0;
    const auto clampByte = [](float v) { return std::clamp(int32_t(v), 1, 255); };
    return (clampByte(s.maxs[2] + 32) << 16) | (clampByte(-s.mins[2]) << 8) | clampByte(s.maxs[0]);
}

} // namespace reluva::idtech3
