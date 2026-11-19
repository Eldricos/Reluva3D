// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/game/road_graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace reluva::age {

namespace {

float flatDistance(const Vec3 &a, const Vec3 &b) {
    const float dx = a[0] - b[0];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

uint32_t RoadGraph::add(const Vec3 &p) {
    _nodes.push_back(p);
    _links.emplace_back();
    return uint32_t(_nodes.size() - 1);
}

void RoadGraph::join(uint32_t a, uint32_t b, float weight) {
    if (a == b)
        return;
    const float d = length(_nodes[a] - _nodes[b]) * weight;
    _links[a].push_back({b, d});
    _links[b].push_back({a, d});
}

size_t RoadGraph::penalizeBlocked(const CollisionWorld &world, float penalty) {
    size_t blocked = 0;
    for (uint32_t a = 0; a < _links.size(); ++a)
        for (auto &[b, cost] : _links[a]) {
            if (b < a)
                continue; // each join once, both ways set together
            const Vec3 from = _nodes[a] + Vec3{0, 0.8f, 0};
            const Vec3 to = _nodes[b] + Vec3{0, 0.8f, 0};
            const float span = length(to - from);
            CollisionWorld::RayHit hit;
            // A wall (not the road rising over a hump) short of the far end.
            if (span < 1 || !world.raycast(from, (to - from) * (1 / span), span - 0.5f, hit) ||
                std::fabs(hit.normal[1]) > 0.5f)
                continue;
            ++blocked;
            cost *= penalty;
            for (auto &[c, back] : _links[b])
                if (c == a)
                    back *= penalty;
        }
    return blocked;
}

size_t RoadGraph::edgeCount() const {
    size_t n = 0;
    for (const auto &l : _links)
        n += l.size();
    return n / 2;
}

void RoadGraph::build(const Psdl &psdl) {
    _nodes.clear();
    _links.clear();
    if (psdl.vertices.empty())
        return;
    const size_t last = psdl.vertices.size() - 1;
    auto vertex = [&](uint16_t i) { return psdl.vertices[std::min<size_t>(i, last)]; };
    std::vector<uint32_t> ends; // each road's first and last points
    struct Fan {
        Vec3 middle;
        float radius;
    };
    std::vector<Fan> fans;
    for (const PsdlBlock &block : psdl.blocks) {
        Vec3 fanSum{};
        std::vector<Vec3> fanPoints;
        for (const PsdlAttribute &a : block.attributes) {
            // Road strips' sections are 4 words (the curbs are the middle
            // two); divided roads' 6 after 2 (the curbs are 1 and 4).
            size_t first = 0;
            size_t stride = 0;
            size_t left = 0;
            size_t right = 0;
            if (a.type == psdlattr::kRoadStrip)
                first = 0, stride = 4, left = 1, right = 2;
            else if (a.type == psdlattr::kDividedRoad)
                first = 2, stride = 6, left = 1, right = 4;
            if (stride) {
                uint32_t previous = 0;
                bool any = false;
                for (size_t i = first; i + stride <= a.words.size(); i += stride) {
                    const Vec3 l = vertex(a.words[i + left]);
                    const Vec3 r = vertex(a.words[i + right]);
                    const uint32_t n = add((l + r) * 0.5f);
                    if (any)
                        join(previous, n);
                    else
                        ends.push_back(n);
                    previous = n;
                    any = true;
                }
                if (any)
                    ends.push_back(previous);
            } else if (a.type == psdlattr::kRoadFan) {
                for (uint16_t w : a.words) {
                    fanSum += vertex(w);
                    fanPoints.push_back(vertex(w));
                }
            }
        }
        // An intersection: its block's road fans together.
        if (!fanPoints.empty()) {
            const Vec3 middle = fanSum * (1.0f / float(fanPoints.size()));
            float radius = 0;
            for (const Vec3 &v : fanPoints)
                radius = std::max(radius, flatDistance(v, middle));
            fans.push_back({middle, radius});
        }
    }
    // Intersections: a node in the middle of each, joined to the road ends
    // at its edge.
    for (const Fan &f : fans) {
        const uint32_t n = add(f.middle);
        for (uint32_t e : ends)
            if (flatDistance(_nodes[e], f.middle) < f.radius + 4 && std::fabs(_nodes[e][1] - f.middle[1]) < 4)
                join(n, e);
    }
    // Roads continuing from one block into the next.
    for (size_t i = 0; i < ends.size(); ++i)
        for (size_t j = i + 1; j < ends.size(); ++j)
            if (length(_nodes[ends[i]] - _nodes[ends[j]]) < 3)
                join(ends[i], ends[j]);
}

void RoadGraph::build(const AiRoadMap &map) {
    _nodes.clear();
    _links.clear();
    std::vector<uint32_t> junctions;
    for (const AiJunction &j : map.junctions)
        junctions.push_back(add(j.center));
    for (const AiRoad &road : map.roads) {
        if (road.middle.empty())
            continue;
        const float weight = road.flags & 2 ? 4.0f : 1.0f;
        // A point every 20 m at most, so a place beside a long straight
        // road is near one.
        uint32_t previous = 0;
        uint32_t first = 0;
        for (size_t i = 0; i < road.middle.size(); ++i) {
            if (i == 0) {
                previous = first = add(road.middle[0]);
                continue;
            }
            const Vec3 from = road.middle[i - 1];
            const Vec3 to = road.middle[i];
            const int steps = std::max(1, int(std::ceil(length(to - from) / 20)));
            for (int k = 1; k <= steps; ++k) {
                const uint32_t n = add(from + (to - from) * (float(k) / float(steps)));
                join(previous, n, weight);
                previous = n;
            }
        }
        // ends[1] is the junction at the first point, ends[0] the last's.
        if (road.ends[1].junction < junctions.size())
            join(first, junctions[road.ends[1].junction], weight);
        if (road.ends[0].junction < junctions.size())
            join(previous, junctions[road.ends[0].junction], weight);
    }
}

bool RoadGraph::nearestNode(const Vec3 &p, Vec3 &out, float reach) const {
    const int n = nearest(p, reach);
    if (n >= 0)
        out = _nodes[size_t(n)];
    return n >= 0;
}

int RoadGraph::nearest(const Vec3 &p, float reach) const {
    int best = -1;
    float bestDistance = reach;
    for (size_t i = 0; i < _nodes.size(); ++i) {
        const float d = flatDistance(_nodes[i], p) + std::fabs(_nodes[i][1] - p[1]) * 2;
        if (d < bestDistance) {
            bestDistance = d;
            best = int(i);
        }
    }
    return best;
}

std::vector<Vec3> RoadGraph::route(const Vec3 &a, const Vec3 &b, float reach) const {
    const int from = nearest(a, reach);
    const int to = nearest(b, reach);
    if (from < 0 || to < 0 || from == to)
        return {b};
    // A* by distance.
    const size_t n = _nodes.size();
    std::vector<float> cost(n, std::numeric_limits<float>::max());
    std::vector<int> came(n, -1);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    cost[size_t(from)] = 0;
    open.push({length(_nodes[size_t(from)] - _nodes[size_t(to)]), uint32_t(from)});
    while (!open.empty()) {
        const uint32_t at = open.top().second;
        open.pop();
        if (int(at) == to)
            break;
        for (const auto &[next, d] : _links[at]) {
            const float c = cost[at] + d;
            if (c < cost[next]) {
                cost[next] = c;
                came[next] = int(at);
                open.push({c + length(_nodes[next] - _nodes[size_t(to)]), next});
            }
        }
    }
    if (came[size_t(to)] < 0)
        return {b};
    std::vector<Vec3> out{b};
    for (int at = came[size_t(to)]; at >= 0 && at != from; at = came[size_t(at)])
        out.push_back(_nodes[size_t(at)]);
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace reluva::age
