// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/bsp.h"
#include "engines/idtech3/shader_cache.h"

#include "common/gfx3d.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// Draws a map's surfaces (every model's: the world's and brush entities')
// with their shaders through a ShaderCache: sorted by the shaders' sort
// order, the sky box and cloud layers behind, fog passes over fogged
// surfaces. See docs/engines/idtech3.md for what's approximate or missing.
class WorldRenderer {
public:
    WorldRenderer(gfx::Renderer3D& renderer, ShaderCache& shaders) : r_(renderer), shaders_(shaders) {}
    ~WorldRenderer() { clear(); }
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // Builds the GPU data for `bsp`.
    Error load(const Bsp& bsp, const ShaderLibrary& library);
    void clear();
    // The sky, then the map's surfaces whose shaders sort in sortMin..sortMax,
    // `time` seconds into the map; call between beginFrame() and endFrame().
    // Drawing opaque surfaces, then models, then translucent surfaces keeps
    // what's behind glass right.
    void drawSky(float time) const;
    void drawSurfaces(float time, float sortMin = 0, float sortMax = 1e9f) const;

    size_t batchCount() const { return batches_.size(); }
    size_t triangleCount() const { return triangles_; }

private:
    struct Batch {
        float sort = 3;
        int material = 0;
        int lightmap = -1; // index into lightmaps_, or -1
        int fog = -1;      // index into fogs_, or -1
        uint32_t firstIndex = 0, indexCount = 0;
    };
    struct Fog {
        std::array<float, 3> color{};
        float distance = 0;
    };

    void buildSky(const ShaderDef& sky);

    gfx::Renderer3D& r_;
    ShaderCache& shaders_;
    gfx::MeshId mesh_ = 0;
    std::vector<gfx::TextureId> lightmaps_;
    std::vector<Fog> fogs_;
    std::vector<Batch> batches_; // in drawing order
    size_t triangles_ = 0;

    // The sky: a box around the camera, the far box's six sides then the
    // sky shader's stages (cloud layers) over it.
    gfx::MeshId skyMesh_ = 0;
    std::array<gfx::TextureId, 6> skySides_{};
    std::array<uint32_t, 6> skySideFirst_{}; // index ranges in skyMesh_
    uint32_t skySideCount_ = 0;
    int skyMaterial_ = -1;
    uint32_t cloudCount_ = 0; // the box without its bottom, from index 0
};

// Doubles a light colour the way the original does on loading (it's
// stored at half brightness), keeping its hue when a component would pass
// 255. Used for lightmaps, vertex colours and the light grid.
void brightenLight(uint8_t* rgb);

} // namespace reluva::idtech3
