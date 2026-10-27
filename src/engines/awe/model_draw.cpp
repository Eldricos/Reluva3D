// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/model_draw.h"

#include <cmath>

namespace reluva::awe {

Error openGr2(const Archive& files, const std::string& name, Gr2File& out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    return out.open(data);
}

GpuModel::GpuModel(gfx::Renderer3D& renderer, Model model) : renderer_(renderer), model_(std::move(model)) {
    for (const ModelTexture& t : model_.textures) {
        gfx::TextureId id = 0;
        if (!t.rgba.empty())
            renderer_.createTexture(t.width, t.height, t.rgba.data(), {}, id);
        textures_.push_back(id);
    }
}

GpuModel::~GpuModel() {
    for (gfx::TextureId t : textures_)
        if (t)
            renderer_.destroyTexture(t);
}

size_t GpuModel::draw(const Pose& pose, const gfx::Mat4& placement, bool helpers,
                      std::vector<gfx::MeshId>& frameMeshes, float brightness) {
    std::vector<std::array<float, 3>> positions, normals;
    size_t triangles = 0;
    for (const ModelMesh& mesh : model_.meshes) {
        bool textured = false;
        for (const auto& g : mesh.groups)
            textured |= g.texture >= 0;
        if (!textured && !helpers)
            continue;
        // Skinned on the CPU into a new mesh each frame (characters have a
        // few thousand vertices).
        skin(mesh, pose, positions, normals);
        std::vector<gfx::Vertex> verts(mesh.vertices.size());
        for (size_t v = 0; v < verts.size(); v++) {
            gfx::Vertex& out = verts[v];
            for (int k = 0; k < 3; k++)
                out.pos[k] = positions[v][size_t(k)], out.normal[k] = normals[v][size_t(k)];
            out.uv[0] = mesh.vertices[v].uv[0];
            out.uv[1] = mesh.vertices[v].uv[1];
            out.uv2[0] = out.uv2[1] = 0;
            out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
        }
        gfx::MeshId id = 0;
        if (renderer_.createMesh(verts, mesh.indices, id))
            continue;
        frameMeshes.push_back(id);
        // The light in model space, so it stays on the model's front as it turns.
        for (const auto& g : mesh.groups) {
            gfx::DrawCall call;
            call.mesh = id;
            call.model = placement;
            call.firstIndex = g.firstIndex;
            call.indexCount = g.indexCount;
            call.texture = g.texture >= 0 && size_t(g.texture) < textures_.size() ? textures_[size_t(g.texture)] : 0;
            if (!call.texture)
                call.color = {0.8f, 0.4f, 0.8f, 1};
            for (int k = 0; k < 3; k++)
                call.color[size_t(k)] *= brightness;
            call.rgbSource = gfx::ColorSource::Lighting;
            call.ambientLight = {0.45f, 0.45f, 0.45f};
            call.directedLight = {0.75f, 0.72f, 0.68f};
            const gfx::Vec3 l = gfx::transformPoint(placement, {0.4f, -0.6f, 0.7f});
            const gfx::Vec3 o = gfx::transformPoint(placement, {0, 0, 0});
            gfx::Vec3 dir{l[0] - o[0], l[1] - o[1], l[2] - o[2]};
            const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            call.lightDirection = len > 0 ? gfx::Vec3{dir[0] / len, dir[1] / len, dir[2] / len} : dir;
            call.state.cull = gfx::Cull::None;
            renderer_.draw(call);
            triangles += g.indexCount / 3;
        }
    }
    return triangles;
}

Pose poseAt(const Model& model, const Animation* animation, float time) {
    if (animation && animation->duration > 0)
        time = std::fmod(time, animation->duration);
    Pose pose;
    computePose(model, animation, time, pose);
    return pose;
}

} // namespace reluva::awe
