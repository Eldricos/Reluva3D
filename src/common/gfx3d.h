// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::gfx {

// GPU 3D rendering for engines that need it, behind an interface any
// backend can implement (System::init3D). Engines upload textures and
// static meshes once, then each frame set a camera, issue draw calls and
// finish with an optional 2D overlay on top.
//
// Conventions: right-handed world, matrices column-major (m[col * 4 + row])
// and applied as M * v. Clip space has +Y up and depth 0 (near) to 1 (far),
// which perspective() produces. Front faces are counter-clockwise on
// screen.

using Mat4 = std::array<float, 16>;
using Vec3 = std::array<float, 3>;

Mat4 identity();
Mat4 multiply(const Mat4& a, const Mat4& b); // a * b
// fovY in radians; depth 0 at zNear, 1 at zFar.
Mat4 perspective(float fovY, float aspect, float zNear, float zFar);
// A view matrix looking from `eye` along `forward`, `up` roughly up.
Mat4 lookAlong(const Vec3& eye, const Vec3& forward, const Vec3& up);
// The point after M * (x, y, z, 1) and the divide by w.
Vec3 transformPoint(const Mat4& m, const Vec3& p);

struct Vertex {
    float pos[3];
    float uv[2];    // first texture coordinates
    float uv2[2];   // second set, e.g. lightmap coordinates
    float normal[3];
    uint8_t color[4]; // RGBA, 255 = 1.0
};

using TextureId = uint32_t; // 0 = none
using MeshId = uint32_t;    // 0 = none

struct TextureParams {
    bool clamp = false;   // clamp coordinates to the edge instead of repeating
    bool mipmaps = true;  // generate mipmaps and filter between them
    bool nearest = false; // nearest-neighbour instead of linear filtering
};

// Blend factors for result = src * srcFactor + dst * dstFactor.
enum class Blend : uint8_t {
    Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
    SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha,
};
enum class Cull : uint8_t { None, Back, Front };
// Discard fragments by their alpha: none, alpha > 0, alpha < 0.5, alpha >= 0.5.
enum class AlphaTest : uint8_t { None, Gt0, Lt128, Ge128 };

struct DrawState {
    Blend src = Blend::One, dst = Blend::Zero; // One/Zero: opaque
    bool depthTest = true;  // passes when nearer or equal
    bool depthEqual = false; // with depthTest: passes only when equal, for extra passes over drawn geometry
    bool depthWrite = true;
    bool depthBias = false; // pulled towards the camera, for decals on walls
    Cull cull = Cull::Back;
    AlphaTest alphaTest = AlphaTest::None;

    bool operator==(const DrawState&) const = default;
};

// A periodic function of time: base + amplitude * f(phase + time * frequency),
// f with period 1 and values in -1..1 (sawtooths 0..1).
enum class WaveFunc : uint8_t { Sin, Triangle, Square, Sawtooth, InverseSawtooth, Noise };
struct Wave {
    WaveFunc func = WaveFunc::Sin;
    float base = 0, amplitude = 0, phase = 0, frequency = 0;
};
// The wave's value at `time` seconds, as the GPU computes it too.
float evaluate(const Wave& wave, float time);

// Where texture coordinates come from: the first or second set, a
// reflection of the camera off the surface (environment mapping), or the
// position's dot products with two vectors.
enum class TexGen : uint8_t { Uv, Uv2, Environment, Vector };
// Where colour and alpha come from (times DrawCall::color).
enum class ColorSource : uint8_t { Constant, Vertex, OneMinusVertex };

// Moves vertices on the GPU each frame.
struct Deform {
    enum class Type : uint8_t { None, Wave, Bulge, Move } type = Type::None;
    // Wave: along the normal by the wave's value, its phase offset by
    // (x + y + z) * spread. Move: by `vector` times the wave's value.
    Wave wave;
    float spread = 0;
    Vec3 vector{};
    // Bulge: along the normal by height * sin(u * width + time * speed).
    float width = 0, height = 0, speed = 0;
};

struct DrawCall {
    MeshId mesh = 0;
    uint32_t firstIndex = 0, indexCount = 0;
    TextureId texture = 0; // 0: white
    TexGen texGen = TexGen::Uv;
    Vec3 texVector0{}, texVector1{}; // TexGen::Vector's two vectors
    // Texture coordinates go through rows (pre) then a turbulence, then
    // rows (post): each pair of rows maps (u, v) to (row0 . (u, v, 1),
    // row1 . (u, v, 1)). Turbulence adds amplitude * sin(2 pi (p + phase))
    // with p = (x + z) / 1024 to u and p = y / 1024 to v.
    std::array<float, 3> preRow0{1, 0, 0}, preRow1{0, 1, 0};
    float turbAmplitude = 0, turbPhase = 0;
    std::array<float, 3> postRow0{1, 0, 0}, postRow1{0, 1, 0};
    // The fragment is texture * colour, the colour's RGB and alpha each
    // from the constant, the vertex colour, or one minus it, times the
    // constant. Components may exceed 1 (results are clamped).
    std::array<float, 4> color{1, 1, 1, 1};
    ColorSource rgbSource = ColorSource::Constant, alphaSource = ColorSource::Constant;
    // > 0: the fragment's alpha is instead clamp(distance from the camera
    // / fogDistance), for blending a fog colour over drawn geometry.
    float fogDistance = 0;
    std::array<Deform, 2> deforms{};
    float time = 0;              // seconds, for the deforms
    bool followCamera = false;   // positions are relative to the camera (skies)
    DrawState state;
};

class Renderer3D {
public:
    virtual ~Renderer3D() = default;

    // RGBA pixels, rows top to bottom.
    virtual Error createTexture(int width, int height, const uint8_t* rgba, const TextureParams& params,
                                TextureId& out) = 0;
    virtual void destroyTexture(TextureId id) = 0;
    // Indices refer to `vertices`; every three make a triangle.
    virtual Error createMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                             MeshId& out) = 0;
    virtual void destroyMesh(MeshId id) = 0;

    // The size given to System::init3D: the frame's pixels.
    virtual int width() const = 0;
    virtual int height() const = 0;
    // The GPU and driver doing the work, for diagnostics.
    virtual std::string deviceName() const = 0;

    // A frame: begin, any number of draws (in order), end. `viewProj` maps
    // world positions to clip space; `eye` is the camera's position.
    virtual void beginFrame(const Mat4& viewProj, const Vec3& eye, Color clear) = 0;
    virtual void draw(const DrawCall& call) = 0;
    // Presents the frame, with `overlay` (RGBA32, the frame's size, blended
    // by its alpha) on top if given.
    virtual void endFrame(const Surface* overlay) = 0;
};

} // namespace reluva::gfx
