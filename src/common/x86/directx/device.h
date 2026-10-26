// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Direct3D 7's device and vertex buffers: the fixed-function pipeline
// (transform, lighting, clipping, texture stages, fog, alpha and depth
// tests, blending), drawn in software into DirectDraw surfaces. Constants
// are the DirectX 7 SDK's (d3dtypes.h).

#pragma once

#include "common/x86/directx/internal.h"

#include <array>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace reluva::x86::dx {

using Matrix = std::array<float, 16>; // row-major, row vectors (D3DMATRIX)
Matrix identity();
Matrix multiply(const Matrix& a, const Matrix& b);

struct Color {
    float r = 0, g = 0, b = 0, a = 0;
};

// D3DLIGHT7
struct Light {
    uint32_t type = 3; // POINT 1, SPOT 2, DIRECTIONAL 3
    Color diffuse, specular, ambient;
    float position[3]{}, direction[3]{0, 0, 1};
    float range = 0, falloff = 0, att0 = 0, att1 = 0, att2 = 0, theta = 0, phi = 0;
    bool enabled = false;
};

// D3DMATERIAL7
struct Material {
    Color diffuse, ambient, specular, emissive;
    float power = 0;
};

// A vertex on its way through the pipeline.
struct ClipVertex {
    float x, y, z, w;     // clip space; after projection, screen x, y, z and 1/w
    Color diffuse, specular; // 0..1; specular's alpha is the fog factor
    float tex[8][2];
};

// A texture's pixels decoded to 0xAARRGGBB, one array per mip level, kept
// until the surface changes.
struct DecodedTexture {
    struct Level {
        int width, height;
        std::vector<uint32_t> argb;
        std::vector<uint8_t> keyed; // 1 where the colour key matches
    };
    std::vector<Level> levels;
    std::vector<uint64_t> versions; // each level's surface's, when decoded
    std::weak_ptr<Surface> owner;
};

class Device;
struct VertexLayout;

class VertexBuffer : public ComObject {
public:
    uint32_t fvf = 0, count = 0, memory = 0, caps = 0;
    DirectX::State* s = nullptr;
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override;
    const ComInterface* query(const std::string& iid) override;
    ~VertexBuffer() override;
};

class Device : public ComObject {
public:
    DirectX::State* s = nullptr;
    std::shared_ptr<Surface> target, depth;
    std::array<uint32_t, 256> renderStates{};
    std::array<std::array<uint32_t, 32>, 8> stageStates{};
    std::array<std::shared_ptr<Surface>, 8> textures;
    std::map<uint32_t, Matrix> transforms; // by D3DTRANSFORMSTATETYPE
    struct Viewport {
        int x = 0, y = 0, width = 0, height = 0;
        float minZ = 0, maxZ = 1;
    } viewport;
    Material material;
    std::map<uint32_t, Light> lights;
    std::vector<uint32_t> clipStatus = std::vector<uint32_t>(6, 0);

    // State blocks: what a block sets, replayed by ApplyStateBlock.
    struct StateBlock {
        std::map<uint32_t, uint32_t> renderStates;
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> stageStates;
        std::map<uint32_t, std::shared_ptr<Surface>> textures;
        std::map<uint32_t, Matrix> transforms;
        std::optional<Viewport> viewport;
        std::optional<Material> material;
        std::map<uint32_t, Light> lights;
    };
    std::map<uint32_t, StateBlock> blocks;
    std::optional<StateBlock> recording;
    uint32_t nextBlock = 1;

    std::unordered_map<const Surface*, DecodedTexture> decoded;

    Device(DirectX::State* state, std::shared_ptr<Surface> renderTarget);
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override;
    const ComInterface* query(const std::string& iid) override;

    // Draws vertices in the program's memory (FVF layout, `stride` apart);
    // `indices` are 16-bit, or none.
    void draw(uint32_t primitive, uint32_t fvf, uint32_t vertices, uint32_t stride, uint32_t vertexCount,
              uint32_t indices, uint32_t indexCount);
    void clear(uint32_t count, uint32_t rects, uint32_t flags, uint32_t color, float z);

private:
    void setRenderState(uint32_t state, uint32_t value);
    void transformAndLight(const VertexLayout& layout, uint32_t vertices, uint32_t stride, uint32_t count,
                           std::vector<ClipVertex>& out);
    void triangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c);
    struct Raster;
    void rasterize(Raster& r, const ClipVertex& a, const ClipVertex& b, const ClipVertex& c);
    void line(const ClipVertex& a, const ClipVertex& b);
    void point(const ClipVertex& a);
    ClipVertex project(const ClipVertex& v) const;
    // One pixel: depth test, texture stages, alpha test, fog, blending.
    // `attributes` are the vertex attributes there (diffuse, specular, then
    // each stage's u and v).
    void pixel(Raster& r, int x, int y, float z, float eyeW, const float* attributes);
    bool depthTest(int x, int y, float z, uint8_t*& zAt, uint32_t& zNew);
    void shade(const Raster& r, int x, int y, float z, float eyeW, const float* attributes, uint8_t* zAt,
               uint32_t zNew);
    void finish(int x, int y, float z, float eyeW, const float* attributes, Color current, uint8_t* zAt,
                uint32_t zNew);
    const DecodedTexture* texture(int stage);
    // A texel of a stage's texture at u, v (0xffffffff without one); with
    // `keyed`, whether it's keyed out by the colour key.
    uint32_t sample(const Raster& r, int stage, float u, float v, bool* keyed);
    // Each stage's texture as it's sampled for the triangle being drawn
    // (its mip level, by `lod`), set by prepareSamplers.
    struct Sampler {
        const uint32_t* argb = nullptr; // none: no texture
        const uint8_t* keyed = nullptr;
        int width = 0, height = 0;
        float fw = 0, fh = 0;
        bool linear = false;
        bool wrapPow2 = false; // both ways wrap, and the sizes are powers of two
        bool clampBoth = false; // both ways clamp
        uint32_t addressU = 0, addressV = 0, border = 0;
    };
    // What a thread drawing needs of its own: the triangle's mip levels
    // and samplers, and which rows are its (groups of 8, every `bands`th
    // from `band`).
    struct Raster {
        float lod[8]{}; // the mip level each stage samples
        Sampler samplers[8];
        int band = 0, bands = 1;
    };
    Raster main_; // the drawing thread's
    void prepareSamplers(Raster& r);
    // A draw's triangles, projected, drawn by rows on several threads.
    std::vector<std::array<ClipVertex, 3>> batch_;
    bool batching_ = false;
    void drawBatch();
    bool projected = false; // the vertices being drawn came transformed (XYZRHW)
    int stages = 0;         // texture stages in use
    const DecodedTexture* bound[8]{};

    // What the pixels of a draw go through, taken from the states once per
    // draw.
    struct StageConfig {
        uint32_t colorOp, colorArg1, colorArg2, alphaOp, alphaArg1, alphaArg2;
        uint32_t addressU, addressV, border, magFilter, minFilter, mipFilter;
        // For the quick path (PixelConfig::simpleStages): the arguments as
        // sources (0 diffuse, 1 current, 2 texture, 3 factor, 4 specular),
        // and whether the texture is one.
        uint8_t source[4];
        bool usesTexture;
    };
    struct PixelConfig {
        bool zTest = false, zWrite = false, alphaTest = false, blend = false, fog = false, colorKey = false,
             specular = false, flat = false;
        uint32_t zFunc = 0, alphaFunc = 0, srcBlend = 0, destBlend = 0, fogTableMode = 0;
        float alphaRef = 0, fogStart = 0, fogEnd = 0, fogDensity = 0;
        bool fogByZ = false; // table fog by depth, not by the eye's distance
        Color fogColor, factor;
        StageConfig stage[8];
        // Every stage SELECTARG1, SELECTARG2 or MODULATE (alpha also
        // DISABLE) of plain arguments: they're combined the quick way.
        bool simpleStages = false;
        enum class Target { Generic, Rgb565, Rgb888 } target = Target::Generic;
        double zMax = 0;
        uint32_t zBytes = 0, zMask = 0;
        // Where the target and the depth buffer are, for this draw.
        uint8_t* targetAt = nullptr;
        uint8_t* depthAt = nullptr;
        int targetWidth = 0, targetHeight = 0, targetPitch = 0, depthWidth = 0, depthHeight = 0, depthPitch = 0;
        uint32_t targetBytes = 0;
    } cfg_;
    void configure();
};

// D3DFVF layout of one vertex.
struct VertexLayout {
    int position = 0;  // floats: 3 (XYZ), 4 (XYZRHW), 3 + blend weights
    bool rhw = false, normal = false, reserved = false, diffuse = false, specular = false;
    int texSets = 0;
    int texSize[8]{};
    int normalAt = -1, diffuseAt = -1, specularAt = -1, texAt[8]{};
    int size = 0; // bytes
    explicit VertexLayout(uint32_t fvf);
};

// The D3DDEVICEDESC7 we describe ourselves with.
void writeDeviceDesc(Cpu& c, uint32_t p, const std::string& iid);

} // namespace reluva::x86::dx
