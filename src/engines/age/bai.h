// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/vecmath.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::age {

// A city's road map for its computer-driven traffic (city/<map>.bai,
// "CAI1"): roads with their lanes and sidewalks as rows of points, one
// per point along the road, and the junctions that join them. See
// docs/formats/age/bai.md.

// One side of a road: side 0 is on the left going from the road's first
// point to its last, side 1 on the right.
struct AiRoadSide {
    // The lanes' middle lines, across the road; a one-way road has them all
    // on side 1 (and none on side 0). Each runs the way traffic keeping
    // right drives it: side 1's from the road's first point to its last,
    // side 0's from its last to its first (unlike the other rows here).
    std::vector<std::vector<Vec3>> lanes;
    std::vector<float> laneOffsets;              // each lane's distance from the road's middle line
    std::vector<std::vector<Vec3>> tracks;       // tram, then train lines
    uint16_t trams = 0, trains = 0;
    uint16_t kind = 0;                           // 0-3, meaning not known yet
    std::vector<Vec3> sidewalk, curb, outerEdge; // the sidewalk's middle and its two edges
    std::array<float, 11> widths{};              // after the lane offsets: see docs/formats/age/bai.md
};

struct AiRoadEnd {
    uint32_t junction = 0;
    uint16_t control = 0; // 0, 1 or 3: likely what the road meets there (stop sign, lights), not known yet
    uint32_t slot = 0;    // where the road is in the junction's list
    Vec3 a{}, b{};        // two points at the end, zero at most first ends
};

struct AiRoad {
    uint16_t id = 0;
    uint16_t flags = 0;               // meaning not known yet
    std::vector<uint16_t> blocks;     // the PSDL blocks it runs through
    float halfWidth = 0;              // middle line to the sidewalks' outer edge
    float speedLimit = 0;             // 15 throughout
    std::array<AiRoadSide, 2> sides{};
    std::vector<Vec3> middle;         // the middle line
    // ends[0] is the junction at the last point, ends[1] the one at the first.
    std::array<AiRoadEnd, 2> ends{};
};

struct AiJunction {
    uint16_t id = 0;
    uint16_t block = 0; // its PSDL block
    Vec3 center{};
    std::vector<uint32_t> roads; // in order round it
};

struct AiRoadMap {
    std::vector<AiRoad> roads;
    std::vector<AiJunction> junctions;
};

Error readBai(const std::vector<uint8_t>& data, AiRoadMap& out);

} // namespace reluva::age
