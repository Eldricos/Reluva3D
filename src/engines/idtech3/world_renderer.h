// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/shader.h"

#include "common/archive.h"
#include "common/gfx3d.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// Draws a map's surfaces (every model's: the world's and brush entities')
// with gfx::Renderer3D: textures from the shaders' first image stage, times
// the lightmap (or the vertex colours of surfaces without one), translucent
// shaders blended after the rest. Sky and nodraw surfaces are skipped. Far
// from every shader feature yet; see docs/engines/idtech3.md.
class WorldRenderer {
public:
    explicit WorldRenderer(gfx::Renderer3D& renderer) : r_(renderer) {}
    ~WorldRenderer() { clear(); }
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Builds the GPU data for `bsp`. Images that can't be found or decoded
    // are drawn grey and counted in missingImages().
    Error load(const Bsp& bsp, const SearchSet& files, const ShaderLibrary& shaders);
    void clear();
    // Issues the draw calls; call between beginFrame() and endFrame().
    void draw() const;

    size_t batchCount() const { return batches_.size(); }
    size_t textureCount() const { return images_.size(); }
    int missingImages() const { return missing_; }
    size_t triangleCount() const { return triangles_; }

private:
    // How one BSP shader is drawn.
    struct Material {
        bool skip = false;
        gfx::TextureId texture = 0;
        bool translucent = false;
        gfx::Blend src = gfx::Blend::One, dst = gfx::Blend::Zero;
        bool depthWrite = true;
        gfx::Cull cull = gfx::Cull::Front;
        gfx::AlphaTest alphaTest = gfx::AlphaTest::None;
        bool lightmapped = true; // multiplied by the lightmap when the surface has one
    };
    struct Batch {
        int material = 0;
        int lightmap = -1; // index into lightmaps_, or -1: vertex colours
        uint32_t firstIndex = 0, indexCount = 0;
    };

    Material makeMaterial(const std::string& shaderName, const SearchSet& files, const ShaderLibrary& shaders);
    gfx::TextureId image(const std::string& name, bool clamp, const SearchSet& files);

    gfx::Renderer3D& r_;
    gfx::MeshId mesh_ = 0;
    std::vector<Material> materials_; // one per BSP shader
    std::vector<gfx::TextureId> lightmaps_;
    std::vector<Batch> batches_; // opaque first, then translucent
    std::unordered_map<std::string, gfx::TextureId> images_; // by normalized name
    gfx::TextureId missingTexture_ = 0;
    int missing_ = 0;
    size_t triangles_ = 0;
};

} // namespace reluva::idtech3
