// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/shader.h"

#include "common/archive.h"
#include "common/gfx3d.h"

#include <deque>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

// Draws a map with gfx::Renderer3D the way the original renderer draws its
// shaders: every stage a pass with its blend, depth and alpha test, colours
// and alpha from rgbGen/alphaGen, texture coordinates from tcGen and tcMod,
// animMap frames, deformVertexes, sorted by the shaders' sort order, with
// the sky box and cloud layers behind and fog passes over fogged surfaces.
// Surfaces without a script get the original's implicit shaders. See
// docs/engines/idtech3.md for what's approximate or missing.
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
    // Issues the draw calls for `time` seconds into the map; call between
    // beginFrame() and endFrame().
    void draw(float time) const;

    size_t batchCount() const { return batches_.size(); }
    size_t textureCount() const { return images_.size(); }
    int missingImages() const { return missing_; }
    size_t triangleCount() const { return triangles_; }
    // Features met while loading that aren't drawn (e.g. "deformVertexes
    // autosprite"), with how many shaders use each.
    const std::map<std::string, int>& unsupported() const { return unsupported_; }

private:
    // One stage, resolved: textures loaded, states decided.
    struct Stage {
        std::vector<gfx::TextureId> frames; // empty: the surface's lightmap, or white
        float fps = 0;
        bool lightmap = false; // the batch's lightmap (white without one)
        gfx::DrawState state;
        const ShaderStage* def = nullptr; // for what changes with time
        gfx::TexGen texGen = gfx::TexGen::Uv;
    };
    // How the surfaces of one shader are drawn, with or without a lightmap.
    struct Material {
        bool skip = false;
        float sort = 3;
        std::vector<Stage> stages;
        std::array<gfx::Deform, 2> deforms{};
    };
    struct Batch {
        int material = 0;  // index into materials_
        int lightmap = -1; // index into lightmaps_, or -1
        int fog = -1;      // index into fogs_, or -1
        uint32_t firstIndex = 0, indexCount = 0;
    };
    struct Fog {
        std::array<float, 3> color{};
        float distance = 0;
    };

    int material(int shader, bool lightmapped, const Bsp& bsp, const SearchSet& files, const ShaderLibrary& shaders);
    Material makeMaterial(const std::string& name, bool lightmapped, const SearchSet& files,
                          const ShaderLibrary& shaders);
    Stage makeStage(const ShaderStage& def, const ShaderDef* shader, const SearchSet& files);
    gfx::TextureId image(const std::string& name, bool clamp, bool mipmaps, const SearchSet& files);
    void buildSky(const ShaderDef& sky, const SearchSet& files);
    // `sky`: cloud layers, drawn around the camera behind everything.
    void drawStages(const Material& m, gfx::MeshId mesh, uint32_t first, uint32_t count, int lightmap, bool sky,
                    float time) const;
    void note(const std::string& feature) { ++unsupported_[feature]; }

    gfx::Renderer3D& r_;
    gfx::MeshId mesh_ = 0;
    std::vector<Material> materials_;
    std::map<std::pair<int, bool>, int> materialIndex_; // (BSP shader, lightmapped) -> materials_
    std::deque<ShaderDef> implicit_; // the original's shaders for surfaces without a script (stable addresses)
    std::vector<gfx::TextureId> lightmaps_;
    std::vector<Fog> fogs_;
    std::vector<Batch> batches_; // in drawing order
    std::unordered_map<std::string, gfx::TextureId> images_; // by normalized name and flags
    gfx::TextureId missingTexture_ = 0;
    gfx::TextureId whiteTexture_ = 0;
    int missing_ = 0;
    size_t triangles_ = 0;
    std::map<std::string, int> unsupported_;

    // The sky: a box around the camera, the far box's six sides then the
    // sky shader's stages (cloud layers) over it.
    gfx::MeshId skyMesh_ = 0;
    std::array<gfx::TextureId, 6> skySides_{};
    std::array<uint32_t, 6> skySideFirst_{}; // index ranges in skyMesh_
    uint32_t skySideCount_ = 0;
    int skyMaterial_ = -1;
    uint32_t cloudFirst_ = 0, cloudCount_ = 0; // the box without its bottom
};

} // namespace reluva::idtech3
