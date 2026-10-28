// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/pkg.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace reluva::age {

// A city's streets and buildings (.psdl, "PSD0"): shared vertices and
// heights, and blocks, each a list of attributes (road strips, sidewalks,
// building fronts, roofs...) the geometry is generated from. See
// docs/formats/age/psdl.md.

namespace psdlattr {
constexpr uint8_t RoadStrip = 0;     // sections of 4: sidewalk, road, road, sidewalk
constexpr uint8_t SidewalkStrip = 1; // pairs: inner (road side), outer
constexpr uint8_t RectStrip = 2;     // pairs
constexpr uint8_t Sliver = 3;        // top height, texture scale, left, right
constexpr uint8_t Crosswalk = 4;     // 4 vertices
constexpr uint8_t RoadFan = 5;       // a triangle fan of road (intersections)
constexpr uint8_t Fan = 6;           // a triangle fan (ground, plazas)
constexpr uint8_t FacadeBound = 7;   // angle, height, left, right: not drawn
constexpr uint8_t DividedRoad = 8;   // two words, then sections of 6
constexpr uint8_t Tunnel = 9;        // flags and two heights (and an outline's edges): walls, railings, ceilings
constexpr uint8_t Texture = 10;      // sets the texture for what follows
constexpr uint8_t Facade = 11;       // bottom, top, u repeat, v repeat, left, right
constexpr uint8_t Roof = 12;         // height, then a triangle fan
} // namespace psdlattr

struct PsdlAttribute {
    uint8_t type = 0;
    uint8_t subtype = 0;
    uint16_t count = 0; // the subtype, or the count read after the header when that's 0
    std::vector<uint16_t> words;
};

struct PsdlBlock {
    // The block's outline: each vertex and the block beyond that edge (0: none).
    std::vector<std::pair<uint16_t, uint16_t>> perimeter;
    std::vector<PsdlAttribute> attributes;
    uint8_t flags = 0;    // what it is: see psdlblock
    uint8_t extra = 0;    // a road block's street furniture rule (proprules.csv n01...), 0 none; 205 in block 0
};

// A block's flags (psdl.md).
namespace psdlblock {
constexpr uint8_t Covered = 0x2;  // under a roof: a tunnel, the Underground
constexpr uint8_t Plain = 0x4;    // no road: ground, buildings (the blocks before Psdl::firstRoadBlock)
constexpr uint8_t Road = 0x8;     // part of a road between junctions
constexpr uint8_t Junction = 0x10; // a junction (as the .bai road map's junctions)
} // namespace psdlblock

// A road between two junctions, from the data after the city's bounds:
// one for each road of the .bai road map, in the same order.
struct PsdlRoad {
    uint16_t flags = 0, extra = 0;         // meanings not known
    uint8_t lanes[2] = {0, 0};             // each side's: the .bai's side 1, then side 0
    std::vector<float> lanePositions;      // across the road, 0-1, one for each lane
    uint8_t ends[2] = {0, 0};              // a byte for each end, bits 0 and 7 used; meaning not known
    uint16_t vertices[8] = {};             // four near each end; how they're used isn't known
    std::vector<int32_t> blocks;           // its blocks, negative in 23 of San Francisco's roads
};

struct Psdl {
    std::vector<Vec3> vertices;
    std::vector<float> heights;
    std::vector<std::string> textures; // a texture attribute's index 1 is textures[0]
    std::vector<PsdlBlock> blocks;     // blocks[0] is the unused block 0
    uint32_t firstRoadBlock = 0;       // the plain blocks (no roads) come before it
    Vec3 min{}, max{}, center{};
    float radius = 0;
    std::vector<PsdlRoad> roads;

    // The texture an index names, or "" for none.
    std::string texture(uint32_t index) const;
};

Error readPsdl(const std::vector<uint8_t>& data, Psdl& out);

// Each attribute's length in words after its header (and count), by the
// rules in psdl.md: how many vertices and values it takes.
size_t psdlAttributeLength(uint8_t type, uint16_t count);

} // namespace reluva::age
