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

// nfs3.exe's colour conversions (0x49c880, 0x49ca50), every value 0-255
// and the hue a full turn in 256, in its integer steps.
std::array<int, 3> rgbToHsv(const std::array<int, 3>& rgb);
std::array<int, 3> hsvToRgb(const std::array<int, 3>& hsv);
// A car skin's rows as the game's loader takes them (0x49d320 ->
// 0x49cd00): in the order the file stores them, from the start of the
// pixel data, whatever the header's origin bit; texture row 0 is the
// first row stored. A decoded TGA (top row first) whose descriptor has
// bit 5 clear (stored from the bottom, as every car00.tga but Knoc's)
// is turned back over. Before November 2026 Reluva drew the skins the
// other way up: the Corvette without its tail lights.
void skinRowsAsStored(const std::vector<uint8_t>& tga, image::Image& img);

// A car skin's texel as the car loader makes it (0x49d320 -> 0x49cea0 ->
// 0x49cd00, each texel to 1:5:5:5): alpha 0 transparent; alpha 1-127
// recoloured with the paint, 128-254 with the paint's secondary colour,
// 255 kept (fce.md, The skin's paint). Recolouring (0x49cc50): t the
// texel's blue in 5 bits x 8, over 255 - the colour's fourth value
// unless that is 255; the value min(trunc(brightness x t), 255); then
// the colour's hue and saturation at that value (hsvToRgb), to 5 bits.
// RGBA in and out; a null colour keeps those texels as they are.
std::array<uint8_t, 4> skinTexel(const std::array<uint8_t, 4>& rgba, const Model::Colour* paint,
                                 const Model::Colour* secondary);
// The secondary colour going with a paint (0x49d240): that of the
// model's primary colour nearest to it, by the RGB of the two in HSV
// (hsvToRgb) and the distance between them; 0 without colours.
size_t secondaryFor(const Model& model, const std::array<int, 3>& paintHsv);
// The model's primary colour nearest a paint in HSV (as secondaryFor), so
// that its fourth value goes with the paint the game keeps (+0x8A8).
size_t primaryFor(const Model& model, const std::array<int, 3>& paintHsv);

// The reflection's two corner colours for a car (0x4ba350, kept by
// 0x49d800 at 0x55e6dc and 0x55e6e0; fce.md, Lighting), ARGB, from the
// car's shading colour (carShadeColour, carLitBy), the sky's colour (the
// sky's top, the clear colour 0x6ff9f8; black where no sky is drawn), the
// value of the car's paint in HSV (+0x8B0) and coverDepth() (frd.h).
// Under cover the sky is mixed towards the car's colour, 32 / 256 of it
// a slice to the cover's end, and both colours are dimmed 16 / 256 a
// slice. Each takes the sky's hue; the normal one a 96 / 256 of its
// saturation, the strong one half; their value and alpha 128 + the sky's
// value x the car's / 512, at least 127, the normal one less a quarter of
// the paint's value; both then x (256 - 16 n) / 256 (255 / 256 in the open).
struct CarReflection {
    uint32_t normal = 0x7f7f7f7f, strong = 0xffffffff;
};
CarReflection carReflection(uint32_t carColour, const std::array<int, 3>& sky, int paintValue, int coverDepth);
// The frame a car's normals (in the track) are turned into for the
// sphere map (0x49d780, 0x49de20): the car's view from the camera, yaw
// atan2(dx, dz) - 1/2 turn and pitch atan2(sqrt(dx^2 + dz^2), dy) - 1/4
// turn of d = camera - car, without roll; its x, y and z axes in the
// track. Then reflectionUv(n . x, n . y, n . z).
std::array<std::array<float, 3>, 3> reflectionFrame(const std::array<float, 3>& toCamera);

// A car light's flare on the wet road (0x491bc0, in the weather, for
// the T and H glows whose dummy's fifth letter isn't N): glw4 of sfx.fsh
// on the road under the light, from the light towards the camera on the
// ground. d is the light less the camera on the ground, L its length and
// L' = L x (the way the light faces . -d / L); the quad is size / 2 wide
// either side of the light, and 0.75 L' long (at most 30), or as long as
// it is half wide when that is more; its colour the glow's x 166 / 256.
// corners: on the ground (x, z) in the order the picture's corners
// (0, 0), (1, 0), (1, 1), (0, 1) take; the game sets each 0.125 above the
// road polygon there. depth: L', the game's sorting depth.
struct RainFlare {
    bool shown = false;
    std::array<std::array<float, 2>, 4> corners{};
    uint32_t colour = 0;
    float depth = 0;
};
RainFlare rainFlare(const std::array<float, 3>& light, const std::array<float, 3>& camera,
                    const std::array<float, 3>& facing, float size, uint32_t glowColour);
// A car's shadow (0x4b81a0, drawn with each car at every detail): the
// four bottom corners of its box (0x4b7f80: -+half width, -half height,
// +-half length turned into the track, front left, front right, back
// right, back left), each put on the road (the height 0x4968a0 gives on
// the car's road frame; trackpos_build's road instead when it is up to
// 0.25 above that). By day out of the weather and the mirror (`cast`)
// each corner is pushed along its two edges (to the corners before and
// after it, 0x4b7a30's table) away from the light: e the unit edge from
// the neighbour to the corner, the corner moved by e x -(e . L) x the
// car's half height when e . L < 0. Then each is raised 0.125 (0x53ffc0).
// Drawn with sfx.fsh's shad (0x8b4898) in 0x6FFFFFFF, blended by source
// alpha over the road, its corners texel centre to texel centre.
std::array<std::array<float, 3>, 4> carShadowQuad(const std::array<std::array<float, 3>, 4>& onRoad,
                                                  const std::array<float, 3>& light, float halfHeight, bool cast);
constexpr uint32_t kCarShadowColour = 0x6fffffffu;

// A headlight's beam in the fog (0x492980, for each H dummy while the fog
// is over 30 %): a cone of glw1 of sfx.fsh from a ring of radius 0.1 at
// the lamp, in the colour, to one of 0.75 six units ahead along the car's
// forward, in black; n quads (n = 12 x (1 - D / 100), 3-12; none past
// D = 100, the distance to the camera), its seam turned from the camera
// by 1/2 x (-1/2 - (r + u) / 2) turns (r, u: the way to the camera along
// the car's right and up). Looking into it (its forward . the way to the
// camera f over 1 - w, w = 5 atan2(0.75, 6) in turns) dims the colour by
// (f - 1 + w) / w. v runs from -scroll (far) to 1 - scroll (near), u
// from 0 to 1 round the ring. Points in the track's coordinates.
struct FogBeam {
    bool shown = false;
    std::vector<std::array<float, 3>> nearRing, farRing; // n + 1 points each
    uint32_t colour = 0;                                   // the near ring's
};
FogBeam fogBeam(const std::array<float, 3>& lamp, const std::array<float, 3>& right, const std::array<float, 3>& up,
                const std::array<float, 3>& forward, const std::array<float, 3>& camera, uint32_t colour);
// The beam's colour (0x4b8740): white, alpha 228 x min((fog - 30) / 30,
// 1), half that on low beams; nothing with the fog at 30 or less.
uint32_t fogBeamColour(float fogPercent, int beams);

// The texture pages of a GameData/Render/pc model (cone.art, copN.art):
// one image each, in page order (fce.md).
Error parseArt(const std::vector<uint8_t>& data, std::vector<image::Image>& pages);

} // namespace reluva::nfs
