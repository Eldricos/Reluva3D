// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::nfs {

// An .fce model (cars, traffic, police, the front end's track models), as
// docs/formats/nfs/fce.md describes. Coordinates are as stored: metres,
// +y up, +z the car's front, +x its right (a left-handed space).
struct Model {
    using Vec3 = std::array<float, 3>;
    struct Triangle {
        uint32_t texturePage = 0; // 0: car00.tga, 1: car01.tga...
        std::array<uint32_t, 3> vertices{}; // into the model's vertices (not the part's)
        uint32_t flags = 0;
        std::array<float, 3> u{}, v{};
    };
    struct Part {
        std::string name; // "high body", "left front wheel"...
        Vec3 position{};  // added to the part's vertices
        uint32_t firstVertex = 0, vertexCount = 0;
        uint32_t firstTriangle = 0, triangleCount = 0;
    };
    struct Dummy {
        std::string name; // "HFLO": headlight, front left...
        Vec3 position{};
    };
    // A paint colour: hue, saturation, brightness, and a fourth value.
    struct Colour {
        std::array<uint32_t, 4> hsbx{};
    };

    Vec3 halfSize{};
    std::vector<Vec3> vertices, normals; // relative to their part's position
    std::vector<Triangle> triangles;
    std::vector<Part> parts;
    std::vector<Dummy> dummies;
    std::vector<Colour> primaryColours, secondaryColours;
};

Error parseModel(const std::vector<uint8_t>& data, Model& out);

} // namespace reluva::nfs
