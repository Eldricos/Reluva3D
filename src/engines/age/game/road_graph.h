// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/age/formats/bai.h"
#include "engines/age/game/collision.h"
#include "engines/age/formats/psdl.h"
#include "engines/age/vecmath.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace reluva::age {

/**
 * The city's roads as a graph, Reluva's own, for routing its drivers
 * (opponents between the points of their paths, the police) along the
 * roads: built from the PSDL's streets or from the .bai road map.
 */
class RoadGraph {
public:
    /**
     * From the PSDL: points along each road strip's middle, joined along it,
     * each intersection (a block's road fans' middle) joined to the road
     * ends at its edge, and road ends within 3 m joined.
     */
    void build(const Psdl &psdl);
    /**
     * From the road map instead (`city/<map>.bai`): every road's middle line
     * to the junctions at its ends; the roads not for traffic (flag 2: the
     * Underground, some ramps and alleys) cost four times their length.
     */
    void build(const AiRoadMap &map);
    /**
     * Joins whose straight line runs into a wall (the city's collision, at
     * a car's height) cost `penalty` times as much: built from the roads'
     * ends, a few join through a building's corner. Not taken away, so
     * the roads stay joined where there's no other way.
     * @return How many joins were blocked.
     */
    size_t penalizeBlocked(const CollisionWorld &world, float penalty = 20);
    /**
     * The road point nearest `p` within `reach` metres (a height apart
     * counting twice); false if there's none.
     */
    bool nearestNode(const Vec3 &p, Vec3 &out, float reach = 60) const;

    /**
     * From a to b along the roads: the points to drive through, ending at
     * b; just {b} if there's no road near or no way.
     * `reach`: how far from a road a and b may be (60 m by default).
     */
    std::vector<Vec3> route(const Vec3 &a, const Vec3 &b, float reach = 60) const;

    /** How many points and joins it has, and the points (for the viewer). */
    ///@{
    size_t nodeCount() const { return _nodes.size(); }
    size_t edgeCount() const;
    const std::vector<Vec3> &nodes() const { return _nodes; }
    ///@}

private:
    uint32_t add(const Vec3 &p);
    void join(uint32_t a, uint32_t b, float weight = 1);
    int nearest(const Vec3 &p, float reach = 60) const;

    std::vector<Vec3> _nodes;
    std::vector<std::vector<std::pair<uint32_t, float>>> _links; // neighbour, distance
};

} // namespace reluva::age
