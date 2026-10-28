// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/age/pkg.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// A model's collision shape (bound/<model>_BOUND.bnd): text, as
//
//   version: 1.01
//   verts: 16
//   ...
//   v  0.94 0.24 -1.97
//   mtl default {
//       elasticity: 0.1
//       friction: 0.5
//       ...
//   }
//   quad 0 1 2 3 0      (four vertices, then the material)
//   tri 4 5 6 0
//
// See docs/formats/age/bnd.md.
struct BoundMaterial {
    std::string name; // also a city physics material's (grass, water...: materials.h)
    float elasticity = 0.1f, friction = 0.5f;
};

struct BoundPolygon {
    std::array<uint32_t, 4> vertices{};
    uint32_t count = 3; // 3 or 4
    uint32_t material = 0;
};

struct Bound {
    std::vector<Vec3> vertices;
    std::vector<BoundMaterial> materials;
    std::vector<BoundPolygon> polygons;
};

Error readBound(std::string_view text, Bound& out);

// The same in binary (bound/<model>_BOUND.bbnd, the 324 beside a .bnd):
// a version byte (1), counts of vertices, materials and polygons, the
// vertices, 104-byte materials (name, elasticity, friction, effect,
// sound) and 10-byte polygons (four vertices, the last 0 for a triangle,
// then the material).
Error readBinaryBound(const std::vector<uint8_t>& data, Bound& out);

// A model's bound: its .bnd, else its .bbnd. Fails if it has neither.
Error loadBound(const SearchSet& files, const std::string& model, Bound& out);

} // namespace reluva::age
