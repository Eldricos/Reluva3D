// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <array>
#include <string_view>

namespace reluva::boggle {
struct MeshMaterial {
    std::array<double, 4> diffuse{};
    double power = 0;
    std::array<double, 3> specular{}, emissive{};
};
struct Mesh {
    using Face = std::vector<uint32_t>;
    std::vector<std::array<double, 3>> positions, normals;
    std::vector<Face> faces, normalFaces;
    std::vector<std::array<double, 2>> texcoords;
    std::vector<MeshMaterial> materials;
    std::vector<uint32_t> faceMaterials;
};
// The observed text .x subset: Header, one Mesh, inline materials, normals,
// texture coordinates. Polygons and their winding are retained, not silently
// triangulated. Unknown constructs, binary files and nonfinite values fail.
Error readMesh(std::string_view text, Mesh& out);
} // namespace reluva::boggle
