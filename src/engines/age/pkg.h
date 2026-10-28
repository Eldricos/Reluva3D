// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// An Angel Game Engine model (.pkg, "PKG3"): named meshes (a car's BODY_H,
// WHL0_M..., a building's H, M, L, VL), the shaders they draw with, and
// models placed in it. See docs/formats/age/pkg.md.

using Vec3 = std::array<float, 3>;

struct PkgVertex {
    Vec3 pos{};
    Vec3 normal{}; // zero when the mesh has none
    float uv[2] = {};
};

// Triangles drawn with one shader.
struct PkgGroup {
    uint32_t shader = 0; // index within a paint job's shaders
    std::vector<PkgVertex> vertices;
    std::vector<uint16_t> indices; // every three a triangle
};

struct PkgMesh {
    std::string name; // e.g. "BODY_H"
    uint32_t fvf = 0; // Direct3D's flexible vertex format the vertices were stored in
    std::vector<PkgGroup> groups;

    // "BODY_H" -> part "BODY", level "H"; a mesh named just "H" has part "".
    std::string part() const;
    std::string level() const;
};

struct PkgShader {
    std::string texture; // empty: none
    // Colours (R, G, B, A), 1 = full. Shaders with byte colours store
    // diffuse, ambient and emissive; those with float colours all four.
    // Glows (a light's halo) have no diffuse or ambient, only emissive.
    std::array<float, 4> diffuse{1, 1, 1, 1}, ambient{1, 1, 1, 1}, specular{}, emissive{};
    float shininess = 0;
};

// A model placed inside this one (a light, a door), by name.
struct PkgXref {
    std::string name;
    std::array<float, 12> matrix{}; // three axes, then the position
};

struct Pkg {
    std::vector<PkgMesh> meshes;
    // shaders[job * shadersPerJob + group.shader]: each paint job (a car's
    // colours) has its own set.
    uint32_t paintJobs = 0, shadersPerJob = 0;
    std::vector<PkgShader> shaders;
    bool hasOffset = false;
    Vec3 offset{};
    std::vector<PkgXref> xrefs;

    const PkgMesh* mesh(std::string_view name) const;
    const PkgShader* shader(uint32_t paintJob, uint32_t index) const;
};

Error readPkg(const std::vector<uint8_t>& data, Pkg& out);

// Whether a model's part is one the game shows only when it's lit: a
// car's lamps and their glows (HEADLIGHT0, HLIGHT, TLIGHT, BLIGHT,
// RLIGHT, SLIGHT0, SIREN0, SRN0), a traffic light's (REDGLOWDAY...) and a
// crossing's signs (WALK_NIGHT...).
bool isLightPart(const std::string& part);

// The box round a model's body at its finest level: its unnamed meshes (or
// BODY's) at H if it has them, else M, else L. False if it has none.
bool bodyBounds(const Pkg& pkg, Vec3& low, Vec3& high);

// A part's placement (.mtx, next to the model: <model>_<part>.mtx).
struct Mtx {
    Vec3 min{}, max{}; // the part's bounds as modelled
    Vec3 pivot{};      // what it turns about (needles, the steering wheel)
    Vec3 origin{};     // where it goes in the model
};

Error readMtx(const std::vector<uint8_t>& data, Mtx& out);

} // namespace reluva::age
