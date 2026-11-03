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

// How nfs3.exe lights a car model (fce.md, Lighting). The car's colour is
// the track's shading where it is (carShadeColour), and from it each
// vertex gets base + (top - base) x max(0, n . L) (0x49da30), its normal
// turned into the track with the car, L the light's direction
// (carLightDirection); base is a third of the colour's red and green and
// half its blue, top twice the colour, at most 255 (0x49d8f0).
struct CarShade {
    std::array<int, 3> base{}, top{}; // r, g, b
};
CarShade carShade(uint32_t colour); // 0xAARRGGBB, alpha ignored
uint32_t carVertexColour(const CarShade& shade, float normalDotLight); // 0xFFRRGGBB
// The direction the light comes from, in the track's coordinates, from
// the horizon's rotation in degrees (0x478340, kept at 0x55d2a8): its
// sine and cosine (of rotation x 65536 / 360, the game's 16.16 table) for
// x and z and 0.5 for y, made a unit vector.
std::array<float, 3> carLightDirection(int rotationDegrees);
// The track's shading under a car (0x4206f0): each channel the mean
// (truncated) of the four corners of the road polygon it is on, alpha
// 255; FF7F7F7F without one. The corners' colours as the game keeps
// them, already scaled by the horizon's ambient light (0x419660).
uint32_t carShadeColour(const std::array<uint32_t, 4>& corners);
uint32_t ambientScaled(uint32_t colour, const std::array<int, 3>& ambientPercent); // 0x419660

// A car's light glows (0x4b8740): each dummy (fce.md) by its name's
// letters: the first H (a headlight), T (a tail light) or S (a roof
// light); the second F or R, facing forward or back; the third L or R,
// a roof light's side; the fourth O or E, the headlight that flashes
// first or second with the siren on. Drawn as a glw3 glow facing the
// camera (0x492370) in `colour` (ARGB), `size` units from its centre to
// its corners, pulled `nearer` units towards the camera for the depth
// test (0.75 for the roof lights; the others at the body's depth).
struct CarGlow {
    bool shown = false;
    uint32_t colour = 0;
    float size = 0, nearer = 0;
};
// How bright a car's glows are by its shading colour (0x4b87a0):
// 1 - 0.3 x (r + g + b) / 765, at least 0.7, at most 1.
float carGlowBrightness(uint32_t colour);
// facing: the car's forward row . the unit vector from the light to the
// camera. beams: the car's light state (0-2); braking +0x509; siren: a
// police car with its siren on (+0x200 bit 0x20, +0x734); step the game's
// step count (64 a second); brightness carGlowBrightness().
CarGlow carLightGlow(const std::string& dummy, float facing, int beams, bool braking, bool siren, int step,
                     float brightness);

// The texture pages of a GameData/Render/pc model (cone.art, copN.art):
// one image each, in page order (fce.md).
Error parseArt(const std::vector<uint8_t>& data, std::vector<image::Image>& pages);

} // namespace reluva::nfs
