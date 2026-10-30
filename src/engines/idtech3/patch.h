// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

// Turns a patch surface's control points into an even grid of triangles
// (collision uses this; drawing uses the adaptive grids below). The grid is
// width x height control points, every 3x3 block (sharing its edges) a
// biquadratic Bezier patch; see docs/engines/idtech3/formats/bsp.md. Each block is
// cut into `level` x `level` quads. Every vertex attribute is
// interpolated. Triangles are wound like the map's polygons: clockwise
// seen from the side the normals face.
//
// Appends to `vertices`; `indices` refer to `vertices` (offset included).
void tessellatePatch(const BspVertex* control, int width, int height, int level, std::vector<BspVertex>& vertices,
                     std::vector<uint32_t>& indices);

// How much a patch bends: the farthest any row or column of three
// control points has its middle one off the line between its ends, in
// units. The curve passes half that far off the line, so cut into n
// pieces it strays from them by about bend / (2 n^2).
float patchBend(const BspVertex* control, int width, int height);
// The cuts per 3x3 block for a patch to stray from round by at most
// `error` units, 1 to 16; the original draws with r_subdivisions as the
// error (4 by default).
int patchLevel(const BspVertex* control, int width, int height, float error);

// A patch cut as the original's renderer cuts it, for drawing: points on
// the curve, width x height of them, row by row.
//
// - Each run of three control points (a column of them across the
//   patch) is halved, again and again, while the curve's midpoint strays
//   from the line between its ends by more than `error` units on any row;
//   then the same across the other way.
// - A middle column that's straight on every row (under 0.1 units) is
//   left out.
// - At most 65 points each way.
// - Normals come from the grid's shape (its neighbouring points), turned
//   to face the way the map's own normals do.
struct PatchGrid {
    int width = 0, height = 0;
    std::vector<BspVertex> points;
    // The group of patches that must be cut alike so they meet: the
    // centre and size of its bounds, as the map stores them.
    std::array<float, 3> groupOrigin{};
    float groupRadius = 0;
};
PatchGrid subdividePatch(const BspVertex* control, int width, int height, float error);

// Closes the cracks between patches of a group: where a patch's edge has
// a point another's matching edge lacks (both ends of that edge's piece
// shared, within 0.1 units), a column or row is added to the other
// through it. Returns how many were added.
int stitchPatches(std::vector<PatchGrid>& grids);

// Every surface's grid (empty for surfaces that aren't patches), cut with
// `error` and stitched.
std::vector<PatchGrid> buildPatchGrids(const Bsp& bsp, float error);

// Appends the grid's triangles, wound like the map's polygons (clockwise
// seen from the side the normals face); `indices` refer to `vertices`.
void triangulateGrid(const PatchGrid& grid, std::vector<BspVertex>& vertices, std::vector<uint32_t>& indices);

} // namespace reluva::idtech3
