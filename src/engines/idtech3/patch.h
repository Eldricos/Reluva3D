// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"

#include <cstdint>
#include <vector>

namespace reluva::idtech3 {

// Turns a patch surface's control points into triangles. The grid is
// width x height control points, every 3x3 block (sharing its edges) a
// biquadratic Bezier patch; see docs/formats/idtech3/bsp.md. Each block is
// cut into `level` x `level` quads. Every vertex attribute is
// interpolated. Triangles are wound like the map's polygons: clockwise
// seen from the side the normals face.
//
// Appends to `vertices`; `indices` refer to `vertices` (offset included).
void tessellatePatch(const BspVertex* control, int width, int height, int level, std::vector<BspVertex>& vertices,
                     std::vector<uint32_t>& indices);

} // namespace reluva::idtech3
