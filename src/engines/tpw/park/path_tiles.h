// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace reluva::tpw {

/**
 * Which way a path cell's neighbours lie, as bits: its four sides, then
 * its four corners. Directions are a texture's: north its top row, east
 * its right-hand column; on the park's grid north is y − 1, east x + 1.
 */
enum PathNeighbour : uint8_t {
    kPathN = 1,
    kPathE = 2,
    kPathS = 4,
    kPathW = 8,
    kPathNE = 16,
    kPathSE = 32,
    kPathSW = 64,
    kPathNW = 128,
};

/** The world's path textures (`terrain/pathtex/`), by name without their ending. */
constexpr std::array<const char *, 18> kPathTextures{
    "ns_squ1", "ns_end1", "ns_str1", "ns_str2", "ns_pathcnr", "ns_cnr1", "ns_cnr2", "ns_edg1", "ns_edg2",
    "ns_tju3", "ns_tju4", "ns_tju1", "ns_ctr1", "ns_icn1",    "ns_tju2", "ns_icn2", "ns_xrd2", "ns_xrd1",
};

/** A path cell's texture (an index of kPathTextures) and how many quarter turns clockwise it's drawn turned. */
struct PathTile {
    size_t texture = 0;
    int turns = 0;
};

/**
 * The texture a path cell is drawn with, by which of its neighbours are
 * path: the textures are a cell each, grass where the path stops, with a
 * notch of grass in a corner whose two sides are path but whose diagonal
 * isn't. Where several textures fit (the straights, the plain three-way),
 * `roll` picks one. See docs/engines/tpw/games/simcoaster/systems/paths.md.
 */
PathTile pathTileFor(uint8_t neighbours, uint32_t roll);

} // namespace reluva::tpw
