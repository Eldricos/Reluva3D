// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

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
    std::string name;
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

} // namespace reluva::age
