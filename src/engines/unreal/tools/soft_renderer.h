// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A software gfx::Renderer3D for reluva-tool: draws a page's frames into
// memory so they can be saved and looked at without a GPU (on a machine
// with none, or in a test). It does what a level's drawing needs to be
// recognisable: triangles clipped at the near plane, textured (bilinear,
// as the GPU draws them, or nearest where the texture asks; wrapping or
// clamped) and coloured as the draw call says (its
// constant, the vertices' colours, or its lighting), depth tested,
// alpha tested and blended, with the overlay on top. Not done: deforms,
// texture coordinate rows and turbulence, dynamic lights, stencil,
// mirroring, environment and vector texture coordinates.

#include "graphics/gfx3d.h"
#include "common/types.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::tools {

class SoftRenderer3D : public gfx::Renderer3D {
public:
    SoftRenderer3D(int width, int height);

    Error createTexture(int width, int height, const uint8_t *rgba, const gfx::TextureParams &params,
                        gfx::TextureId &out) override;
    void destroyTexture(gfx::TextureId id) override;
    Error updateTextures(const std::vector<gfx::TextureRegion> &regions) override;
    Error createMesh(const std::vector<gfx::Vertex> &vertices, const std::vector<uint32_t> &indices,
                     gfx::MeshId &out) override;
    Error createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t> &indices, gfx::MeshId &out) override;
    Error setMeshIndices(gfx::MeshId id, const std::vector<uint32_t> &indices) override;
    void destroyMesh(gfx::MeshId id) override;

    int width() const override { return _w; }
    int height() const override { return _h; }
    std::string deviceName() const override { return "software (reluva-tool)"; }

    void beginFrame(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, Color clear) override;
    void draw(const gfx::DrawCall &call) override;
    void setView(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye) override;
    void setDynamicLights(const std::vector<gfx::DynamicLight> &lights) override {
        _lights.assign(lights.begin(),
                       lights.begin() + std::ptrdiff_t(std::min<size_t>(lights.size(), gfx::kMaxDynamicLights)));
    }
    void setVolumeLights(const std::vector<gfx::DynamicLight> &lights) override { _volumes = lights; }
    void setViewport(int x, int y, int w, int h) override;
    void setDepthRange(float maxDepth) override { _depthRange = maxDepth; }
    void clearDepth() override;
    void clearDepthAndStencil() override;
    bool hasStencil() const override { return true; }
    void setStencilMode(StencilMode mode) override { _stencilMode = mode; }
    void setStencilLevel(uint8_t level) override { stencilLevel = level; }
    uint8_t stencilLevel = 1;
    bool capture = false;
    void setMirrored(bool) override {}
    void setGamma(float gamma) override { _gamma = gamma; }
    void endFrame(const Surface *overlay) override;
    // Every finished frame is kept (frame()), so the next one is too.
    void captureNextFrame() override { capture = true; }
    bool takeCapture(std::vector<uint8_t> &rgba) override {
        if (!capture || _frame.pixels.empty())
            return false;
        capture = false;
        rgba = _frame.pixels;
        return true;
    }

    // Whether draws are done (when only some frames are wanted, the rest
    // cost nothing).
    bool drawing = true;
    // The last finished frame (RGBA32), and how many frames there were.
    const Surface &frame() const { return _frame; }
    int frames() const { return _frames; }
    // Draw calls and triangles drawn in the last frame.
    int lastDraws() const { return _lastDraws; }
    int lastTriangles() const { return _lastTriangles; }

private:
    std::vector<gfx::DynamicLight> _lights, _volumes;
    struct Texture {
        int w = 0, h = 0;
        bool clamp = false, nearest = false;
        std::vector<uint8_t> rgba;
    };
    struct Mesh {
        gfx::MeshId vertexMesh = 0; // whose vertices (itself, unless made by createIndexedMesh)
        std::vector<gfx::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    int _w, _h;
    Surface _color, _frame;
    std::vector<float> _depth;
    std::vector<uint8_t> _stencil; // marks, cleared each frame
    StencilMode _stencilMode = StencilMode::kNone;
    std::map<gfx::TextureId, Texture> _textures;
    std::map<gfx::MeshId, Mesh> _meshes;
    uint32_t _nextId = 1;
    gfx::Mat4 _viewProj = gfx::identity();
    gfx::Vec3 _eye{};
    int _vx = 0, _vy = 0, _vw = 0, _vh = 0; // the viewport
    float _depthRange = 1, _gamma = 1;
    int _frames = 0, _draws = 0, _triangles = 0, _lastDraws = 0, _lastTriangles = 0;
};

} // namespace reluva::tools
