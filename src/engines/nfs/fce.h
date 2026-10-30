// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/image.h"
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
        uint32_t flags = 0; // the k* below (fce.md, Triangles)
        std::array<float, 3> u{}, v{};
        static constexpr uint32_t kNoReflection = 0x1, kStrongReflection = 0x2, kTwoSided = 0x4, kUnlit = 0x8;
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

// Where a corner falls on the reflection's sphere map, from its normal
// turned into the camera's space (x right, y down, z ahead), as nfs3.exe
// works it out (0x49db70): u, v = 0.5 + (x, y) * 0.707106 / sqrt(2 (1 + |z|)).
std::array<float, 2> reflectionUv(float x, float y, float z);

// The texture pages of a GameData/Render/pc model (cone.art, copN.art):
// one image each, in page order (fce.md).
Error parseArt(const std::vector<uint8_t>& data, std::vector<image::Image>& pages);

} // namespace reluva::nfs
