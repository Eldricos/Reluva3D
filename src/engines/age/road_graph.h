// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/psdl.h"
#include "engines/age/vecmath.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace reluva::age {

// The city's roads as a graph, built from its PSDL: points along each road
// strip's middle, joined along it, and each intersection (a road fan's
// middle) joined to the road ends that meet it. Reluva's own, for routing
// opponents between the points of their paths; the game's road maps for
// its drivers (.bai, .aimap) aren't read yet.
class RoadGraph {
public:
    void build(const Psdl& psdl);

    // From a to b along the roads: the points to drive through, ending at
    // b; just {b} if there's no road near or no way.
    std::vector<Vec3> route(const Vec3& a, const Vec3& b) const;

    size_t nodeCount() const { return nodes_.size(); }
    size_t edgeCount() const;
    const std::vector<Vec3>& nodes() const { return nodes_; }

private:
    uint32_t add(const Vec3& p);
    void join(uint32_t a, uint32_t b);
    int nearest(const Vec3& p) const;

    std::vector<Vec3> nodes_;
    std::vector<std::vector<std::pair<uint32_t, float>>> links_; // neighbour, distance
};

} // namespace reluva::age
