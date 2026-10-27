// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"

#include <memory>
#include <string>
#include <vector>

// Drawing the game's models with the 3D renderer, for the viewer pages.

namespace reluva::awe {

// Reads a .gr2 file from the game's files, decompressed by the DLL.
Error openGr2(const Archive& files, const std::string& name, GrannyDll& dll, Gr2File& out);

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
    // them as they are; the results are clipped).
    size_t draw(const Pose& pose, const gfx::Mat4& placement, bool helpers, std::vector<gfx::MeshId>& frameMeshes,
                float brightness = 1);

private:
    gfx::Renderer3D& renderer_;
    Model model_;
    std::vector<gfx::TextureId> textures_;
};

// The rest pose, or an animation's pose at `time` (looping).
Pose poseAt(const Model& model, const Animation* animation, float time);

} // namespace reluva::awe
