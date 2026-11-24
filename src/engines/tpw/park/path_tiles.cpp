// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Each texture's sides and notches read off the textures themselves, where
// their grass is: docs/engines/tpw/games/simcoaster/systems/paths.md.

#include "engines/tpw/park/path_tiles.h"

#include <vector>

namespace reluva::tpw {

namespace {

constexpr uint8_t kSides = kPathN | kPathE | kPathS | kPathW;

// A texture as drawn unturned: the sides its path runs out of, and the
// corners with a notch of grass. In kPathTextures' order.
struct Pattern {
    uint8_t sides;
    uint8_t notches;
};
constexpr std::array<Pattern, kPathTextures.size()> kPatterns{{
    {0, 0},                                          // squ1
    {kPathN, 0},                                     // end1
    {kPathN | kPathS, 0},                            // str1
    {kPathN | kPathS, 0},                            // str2
    {kPathN | kPathS, 0},                            // pathcnr
    {kPathN | kPathE, 0},                            // cnr1
    {kPathN | kPathE, kPathNE},                      // cnr2
    {kPathN | kPathE | kPathS, 0},                   // edg1
    {kPathN | kPathE | kPathS, 0},                   // edg2
    {kPathN | kPathE | kPathS, kPathSE},             // tju3
    {kPathN | kPathW | kPathS, kPathSW},             // tju4
    {kPathN | kPathE | kPathS, kPathNE | kPathSE},   // tju1
    {kSides, 0},                                     // ctr1
    {kSides, kPathSW},                               // icn1
    {kSides, kPathSW | kPathSE},                     // tju2
    {kSides, kPathNE | kPathSW},                     // icn2
    {kSides, kPathNW | kPathNE | kPathSE},           // xrd2
    {kSides, kPathNW | kPathNE | kPathSE | kPathSW}, // xrd1
}};

// Turned a quarter clockwise: north to east and on round; the corners too.
uint8_t turned(uint8_t bits) {
    const uint8_t sides = bits & kSides;
    const uint8_t corners = bits >> 4;
    return uint8_t((((sides << 1) | (sides >> 3)) & kSides) | ((((corners << 1) | (corners >> 3)) & 0xF) << 4));
}

} // namespace

PathTile pathTileFor(uint8_t neighbours, uint32_t roll) {
    const uint8_t sides = neighbours & kSides;
    // A corner is notched where both its sides are path but it isn't.
    uint8_t notches = 0;
    const std::array<std::array<uint8_t, 3>, 4> corners{{
        {kPathNE, kPathN, kPathE},
        {kPathSE, kPathS, kPathE},
        {kPathSW, kPathS, kPathW},
        {kPathNW, kPathN, kPathW},
    }};
    for (const auto &[corner, a, b] : corners)
        if ((sides & a) && (sides & b) && !(neighbours & corner))
            notches |= corner;
    std::vector<PathTile> fits;
    for (size_t t = 0; t < kPatterns.size(); ++t) {
        Pattern p = kPatterns[t];
        for (int turns = 0; turns < 4; ++turns) {
            if (p.sides == sides && p.notches == notches) {
                fits.push_back({t, turns});
                break;
            }
            p = {turned(p.sides), turned(p.notches)};
        }
    }
    return fits.empty() ? PathTile{} : fits[roll % fits.size()];
}

} // namespace reluva::tpw
