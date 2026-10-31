// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/model_draw.h"

#include "engines/awe/shadows.h"

#include <algorithm>
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
                      std::vector<gfx::MeshId>& frameMeshes, float brightness, const SceneLighting* lighting,
                      const Material& material, const StencilShadow* shadow) {
    std::vector<std::array<float, 3>> positions, normals;
    size_t triangles = 0;
    const bool stencil = shadow && shadow->scene && renderer_.hasStencil();
    const std::array<float, 3> light = stencil ? shadowLight(*shadow->scene) : std::array<float, 3>{};
    for (const ModelMesh& mesh : model_.meshes) {
        bool textured = false;
        for (const auto& g : mesh.groups)
            textured |= g.texture >= 0;
        if (!textured && !helpers)
            continue;
        // Skinned on the CPU into a new mesh each frame (characters have a
        // few thousand vertices).
        skin(mesh, pose, positions, normals);
        std::vector<gfx::Vertex> verts(mesh.vertices.size()), shine;
        bool shiny = false;
        if (lighting)
            shine.resize(verts.size());
        for (size_t v = 0; v < verts.size(); v++) {
            gfx::Vertex& out = verts[v];
            for (int k = 0; k < 3; k++)
                out.pos[k] = positions[v][size_t(k)], out.normal[k] = normals[v][size_t(k)];
            out.uv[0] = mesh.vertices[v].uv[0];
            out.uv[1] = mesh.vertices[v].uv[1];
            out.uv2[0] = out.uv2[1] = 0;
            out.color[0] = out.color[1] = out.color[2] = out.color[3] = 255;
            if (lighting) {
                // Lit in the world, as Direct3D lights each vertex.
                const gfx::Vec3 p = gfx::transformPoint(placement, positions[v]);
                float n[3];
                for (size_t r = 0; r < 3; r++)
                    n[r] = placement[r] * out.normal[0] + placement[4 + r] * out.normal[1] +
                           placement[8 + r] * out.normal[2];
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (len > 0)
                    n[0] /= len, n[1] /= len, n[2] /= len;
                Rgb spec{};
                const Rgb c = lightVertex(*lighting, material, p.data(), n, &spec);
                shine[v] = out;
                for (size_t k = 0; k < 3; k++) {
                    out.color[k] = uint8_t(std::lround(c[k] * 255.0f));
                    shine[v].color[k] = uint8_t(std::lround(spec[k] * 255.0f));
                    shiny |= shine[v].color[k] != 0;
                }
            }
        }
        if (stencil && shadow->cast && textured)
            drawShadow(mesh, pose, placement, positions, *shadow->scene, light, frameMeshes);
        gfx::MeshId id = 0;
        if (renderer_.createMesh(verts, mesh.indices, id))
            continue;
        frameMeshes.push_back(id);
        if (stencil) {
            renderer_.setStencilMode(gfx::Renderer3D::StencilMode::Mark);
            renderer_.setStencilReference(shadow->ref);
        }
        // The specular, which Direct3D adds after the texture
        // (D3DRS_SPECULARENABLE, 0x427160): a pass adding it over the model.
        gfx::MeshId shineId = 0;
        if (shiny && !renderer_.createMesh(shine, mesh.indices, shineId))
            frameMeshes.push_back(shineId);
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
            if (lighting) {
                call.rgbSource = gfx::ColorSource::Vertex;
            } else {
                // The light in model space, so it stays on the model's
                // front as it turns.
                call.rgbSource = gfx::ColorSource::Lighting;
                call.ambientLight = {0.45f, 0.45f, 0.45f};
                call.directedLight = {0.75f, 0.72f, 0.68f};
                const gfx::Vec3 l = gfx::transformPoint(placement, {0.4f, -0.6f, 0.7f});
                const gfx::Vec3 o = gfx::transformPoint(placement, {0, 0, 0});
                gfx::Vec3 dir{l[0] - o[0], l[1] - o[1], l[2] - o[2]};
                const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                call.lightDirection = len > 0 ? gfx::Vec3{dir[0] / len, dir[1] / len, dir[2] / len} : dir;
            }
            call.state.cull = gfx::Cull::None;
            if (material.blend) {
                call.color[3] = std::clamp(material.alpha, 0.0f, 1.0f);
                call.state.src = gfx::Blend::SrcAlpha;
                call.state.dst = gfx::Blend::OneMinusSrcAlpha;
            }
            renderer_.draw(call);
            triangles += g.indexCount / 3;
            if (shineId) {
                gfx::DrawCall add = call;
                add.mesh = shineId;
                add.texture = 0;
                add.color = {brightness, brightness, brightness, call.color[3]};
                add.state.src = material.blend ? gfx::Blend::SrcAlpha : gfx::Blend::One;
                add.state.dst = gfx::Blend::One;
                add.state.depthEqual = true;
                add.state.depthWrite = false;
                renderer_.draw(add);
            }
        }
        if (stencil)
            renderer_.setStencilMode(gfx::Renderer3D::StencilMode::None);
    }
    return triangles;
}

void GpuModel::drawShadow(const ModelMesh& mesh, const Pose& pose, const gfx::Mat4& placement,
                          const std::vector<std::array<float, 3>>& positions, const Ac2Scene& scene,
                          const std::array<float, 3>& light, std::vector<gfx::MeshId>& frameMeshes) {
    // In the world, flattened: drawn with no colour (the frame kept, blend
    // Zero/One) and no depth, setting the stencil to 1 where it passes the
    // depth test (0x428250).
    std::vector<gfx::Vertex> verts(mesh.vertices.size());
    for (gfx::Vertex& v : verts) {
        v = {};
        v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
    }
    if (mesh.rigid && !mesh.vertices.empty()) {
        // A rigid mesh (the cleaver's, the luggage's) is drawn with its
        // bone's matrix as the world, which the shadow replaces (0x4285f0):
        // its own vertices (turned to the game's axes) are flattened onto
        // the plane at the bone's height, from a light moved by the bone's
        // position. So its shadow lies about the world's origin, not under
        // the prop.
        const size_t bone = mesh.vertices[0].bones[0];
        const gfx::Mat4 world =
            bone < pose.skinning.size() ? gfx::multiply(placement, pose.skinning[bone]) : placement;
        const float origin[3] = {world[12], world[13], world[14]};
        const std::array<float, 3> moved = shadowLight(scene, origin);
        for (size_t v = 0; v < verts.size(); v++) {
            const float* q = mesh.vertices[v].position;
            const float p[3] = {q[0], q[2], -q[1]};
            const std::array<float, 3> f = flattenOntoPlane(moved, origin[1], p);
            std::copy(f.begin(), f.end(), verts[v].pos);
        }
    } else {
        // A skinned mesh is already in the world: flattened onto the floor.
        for (size_t v = 0; v < verts.size() && v < positions.size(); v++) {
            const gfx::Vec3 w = gfx::transformPoint(placement, positions[v]);
            const float p[3] = {w[0], w[1], w[2]};
            const std::array<float, 3> f = flattenOntoFloor(light, p);
            std::copy(f.begin(), f.end(), verts[v].pos);
        }
    }
    gfx::MeshId id = 0;
    if (renderer_.createMesh(verts, mesh.indices, id))
        return;
    frameMeshes.push_back(id);
    renderer_.setStencilMode(gfx::Renderer3D::StencilMode::Mark);
    renderer_.setStencilReference(1);
    gfx::DrawCall call;
    call.mesh = id;
    call.indexCount = uint32_t(mesh.indices.size());
    call.state.src = gfx::Blend::Zero;
    call.state.dst = gfx::Blend::One;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::None;
    renderer_.draw(call);
    renderer_.setStencilMode(gfx::Renderer3D::StencilMode::None);
}

Pose poseAt(const Model& model, const Animation* animation, float time) {
    if (animation && animation->duration > 0)
        time = std::fmod(time, animation->duration);
    Pose pose;
    computePose(model, animation, time, pose);
    return pose;
}

} // namespace reluva::awe
