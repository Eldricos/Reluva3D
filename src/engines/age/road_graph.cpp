// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/road_graph.h"

#include <algorithm>
#include <limits>
#include <queue>

namespace reluva::age {

namespace {

constexpr float kNear = 60; // metres from a road a route may start or end

float flatDistance(const Vec3& a, const Vec3& b) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

uint32_t RoadGraph::add(const Vec3& p) {
    nodes_.push_back(p);
    links_.emplace_back();
    return uint32_t(nodes_.size() - 1);
}

void RoadGraph::join(uint32_t a, uint32_t b) {
    if (a == b)
        return;
    const float d = length(nodes_[a] - nodes_[b]);
    links_[a].push_back({b, d});
    links_[b].push_back({a, d});
}

size_t RoadGraph::edgeCount() const {
    size_t n = 0;
    for (const auto& l : links_)
        n += l.size();
    return n / 2;
}

void RoadGraph::build(const Psdl& psdl) {
    nodes_.clear();
    links_.clear();
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
    for (const PsdlBlock& block : psdl.blocks) {
        Vec3 fanSum{};
        std::vector<Vec3> fanPoints;
        for (const PsdlAttribute& a : block.attributes) {
            // Road strips' sections are 4 words (the curbs are the middle
            // two); divided roads' 6 after 2 (the curbs are 1 and 4).
            size_t first = 0, stride = 0, left = 0, right = 0;
            if (a.type == psdlattr::RoadStrip)
                first = 0, stride = 4, left = 1, right = 2;
            else if (a.type == psdlattr::DividedRoad)
                first = 2, stride = 6, left = 1, right = 4;
            if (stride) {
                uint32_t previous = 0;
                bool any = false;
                for (size_t i = first; i + stride <= a.words.size(); i += stride) {
                    const Vec3 l = vertex(a.words[i + left]), r = vertex(a.words[i + right]);
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
            } else if (a.type == psdlattr::RoadFan) {
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
            for (const Vec3& v : fanPoints)
                radius = std::max(radius, flatDistance(v, middle));
            fans.push_back({middle, radius});
        }
    }
    // Intersections: a node in the middle of each, joined to the road ends
    // at its edge.
    for (const Fan& f : fans) {
        const uint32_t n = add(f.middle);
        for (uint32_t e : ends)
            if (flatDistance(nodes_[e], f.middle) < f.radius + 4 && std::fabs(nodes_[e][1] - f.middle[1]) < 4)
                join(n, e);
    }
    // Roads continuing from one block into the next.
    for (size_t i = 0; i < ends.size(); ++i)
        for (size_t j = i + 1; j < ends.size(); ++j)
            if (length(nodes_[ends[i]] - nodes_[ends[j]]) < 3)
                join(ends[i], ends[j]);
}

int RoadGraph::nearest(const Vec3& p) const {
    int best = -1;
    float bestDistance = kNear;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const float d = flatDistance(nodes_[i], p) + std::fabs(nodes_[i][1] - p[1]) * 2;
        if (d < bestDistance) {
            bestDistance = d;
            best = int(i);
        }
    }
    return best;
}

std::vector<Vec3> RoadGraph::route(const Vec3& a, const Vec3& b) const {
    const int from = nearest(a), to = nearest(b);
    if (from < 0 || to < 0 || from == to)
        return {b};
    // A* by distance.
    const size_t n = nodes_.size();
    std::vector<float> cost(n, std::numeric_limits<float>::max());
    std::vector<int> came(n, -1);
    using Item = std::pair<float, uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    cost[size_t(from)] = 0;
    open.push({length(nodes_[size_t(from)] - nodes_[size_t(to)]), uint32_t(from)});
    while (!open.empty()) {
        const uint32_t at = open.top().second;
        open.pop();
        if (int(at) == to)
            break;
        for (const auto& [next, d] : links_[at]) {
            const float c = cost[at] + d;
            if (c < cost[next]) {
                cost[next] = c;
                came[next] = int(at);
                open.push({c + length(nodes_[next] - nodes_[size_t(to)]), next});
            }
        }
    }
    if (came[size_t(to)] < 0)
        return {b};
    std::vector<Vec3> out{b};
    for (int at = came[size_t(to)]; at >= 0 && at != from; at = came[size_t(at)])
        out.push_back(nodes_[size_t(at)]);
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace reluva::age
