// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "engines/awe/formats/ac2.h"
#include "engines/awe/formats/granny_dll.h"
#include "engines/awe/render/lighting.h"
#include "engines/awe/formats/model.h"

#include <memory>
#include <string>
#include <vector>

// Drawing the game's models with the 3D renderer, for the viewer pages.

namespace reluva::awe {

// Reads a .gr2 file from the game's files.
Error openGr2(const Archive& files, const std::string& name, Gr2File& out);

// The game's stencil shadow, drawn with a model when the renderer has a
// stencil (docs/engines/awe/games/motoe/systems/shadows.md, 0x428500): each textured mesh's
// shadow first, flattened away from the scene's shadow light, setting the
// stencil to 1 where it passes the depth test and writing nothing else;
// then its body, setting it to `ref` (2 for the player, 0 for others).
struct StencilShadow {
    const Ac2Scene* scene = nullptr; // whose lights make the shadow light
    bool cast = true;                // its shadow too (MODL shadow)
    uint8_t ref = 0;
};

// A model's meshes skinned and lit for a frame (GpuModel::skinAndLight),
// work that needs no renderer, so several can be made at once; then put
// on the GPU (GpuModel::upload) into meshes kept from frame to frame, a
// character's own, so they're made once and only their vertices change.
struct SkinnedModel {
    struct Mesh {
        bool drawn = false;               // textured, or helpers asked for
        std::vector<gfx::Vertex> verts;   // lit, when lit
        std::vector<gfx::Vertex> shine;   // the specular's, when any
        bool shiny = false;
        gfx::MeshId id = 0, shineId = 0;  // on the GPU, once uploaded
    };
    std::vector<Mesh> meshes;
    const class GpuModel* owner = nullptr; // whose meshes the ids are
    gfx::Renderer3D* renderer = nullptr;   // where they are
    bool uploaded = false;                 // this frame's vertices are on the GPU

    SkinnedModel() = default;
    SkinnedModel(const SkinnedModel&) = delete;
    SkinnedModel& operator=(const SkinnedModel&) = delete;
    SkinnedModel(SkinnedModel&& o) noexcept { *this = std::move(o); }
    SkinnedModel& operator=(SkinnedModel&& o) noexcept;
    ~SkinnedModel() { release(); }
    // Its meshes off the GPU.
    void release();
};

// A model with its textures on the GPU.
class GpuModel {
public:
    GpuModel(gfx::Renderer3D& renderer, Model model);
    ~GpuModel();
    GpuModel(const GpuModel&) = delete;
    GpuModel& operator=(const GpuModel&) = delete;

    const Model& model() const { return model_; }
    // Skins the model for `pose` and draws it placed by `placement`
    // (meshes made for the frame go into `frameMeshes`, for the caller to
    // destroy after the frame). Untextured meshes (the face's bone boxes)
    // only if `helpers`. Returns the triangles drawn.
    // `brightness` scales the colours (the game's gamma ramp: 1 leaves
    // them as they are; the results are clipped). Lit by `lighting` with
    // `material` as the game lights it, per vertex; without, by a fixed
    // light from the model's front.
    // With `shadow`, the stencil shadow as above. With `skinned`, its
    // vertices made already by skinAndLight().
    size_t draw(const Pose& pose, const gfx::Mat4& placement, bool helpers, std::vector<gfx::MeshId>& frameMeshes,
                float brightness = 1, const SceneLighting* lighting = nullptr, const Material& material = {},
                const StencilShadow* shadow = nullptr, const SkinnedModel* skinned = nullptr);
    // The skinning and lighting draw() does, for `skinned` to give it
    // later (the same pose, placement, lighting and material). Touches
    // nothing but `out`.
    void skinAndLight(const Pose& pose, const gfx::Mat4& placement, bool helpers, const SceneLighting* lighting,
                      const Material& material, SkinnedModel& out) const;
    // Its vertices onto the GPU, into its meshes (made the first time).
    void upload(SkinnedModel& skinned);

private:
    void drawShadow(const ModelMesh& mesh, const Pose& pose, const gfx::Mat4& placement, const Ac2Scene& scene,
                    std::vector<gfx::MeshId>& frameMeshes);
    void shadowPass(gfx::MeshId mesh, uint32_t indexCount, const gfx::Mat4& model);

    gfx::Renderer3D& renderer_;
    Model model_;
    std::vector<gfx::TextureId> textures_;
};

// The rest pose, or an animation's pose at `time` (looping).
Pose poseAt(const Model& model, const Animation* animation, float time);

} // namespace reluva::awe
