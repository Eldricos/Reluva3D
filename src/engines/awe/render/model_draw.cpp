// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/render/model_draw.h"

#include "engines/awe/render/shadows.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

Error openGr2(const Archive &files, const std::string &name, Gr2File &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    return out.open(data);
}

GpuModel::GpuModel(gfx::Renderer3D &renderer, Model model) : _renderer(renderer), _model(std::move(model)) {
    for (const ModelTexture &t : _model.textures) {
        gfx::TextureId id = 0;
        // Filtered linearly, the MIP levels too (0x4273bb), but only those
        // the file has: with one, none (0x427de2 makes the texture with
        // the file's count). Only the first is decoded, so more would be
        // made here.
        gfx::TextureParams params;
        params.mipmaps = t.levels > 1;
        if (!t.rgba.empty())
            _renderer.createTexture(t.width, t.height, t.rgba.data(), params, id);
        _textures.push_back(id);
    }
}

GpuModel::~GpuModel() {
    for (gfx::TextureId t : _textures)
        if (t)
            _renderer.destroyTexture(t);
}

void GpuModel::skinAndLight(const Pose &pose, const gfx::Mat4 &placement, bool helpers, const SceneLighting *lighting,
                            const Material &material, SkinnedModel &skinned) const {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    if (skinned.owner != this) {
        skinned.release();
        skinned.owner = this;
    }
    skinned.uploaded = false;
    skinned.meshes.resize(_model.meshes.size());
    for (size_t m = 0; m < _model.meshes.size(); m++) {
        const ModelMesh &mesh = _model.meshes[m];
        SkinnedModel::Mesh &result = skinned.meshes[m];
        result.drawn = result.shiny = false;
        bool textured = false;
        for (const auto &g : mesh.groups)
            textured |= g.texture >= 0;
        if (!textured && !helpers)
            continue;
        result.drawn = true;
        result.shine.clear();
        // Skinned on the CPU into a new mesh each frame (characters have a
        // few thousand vertices).
        skin(mesh, pose, positions, normals);
        std::vector<gfx::Vertex> &verts = result.verts;
        std::vector<gfx::Vertex> &shine = result.shine;
        verts.resize(mesh.vertices.size());
        bool shiny = false;
        if (lighting)
            shine.resize(verts.size());
        for (size_t v = 0; v < verts.size(); v++) {
            gfx::Vertex &out = verts[v];
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
        result.shiny = shiny;
    }
}

SkinnedModel &SkinnedModel::operator=(SkinnedModel &&o) noexcept {
    if (this != &o) {
        release();
        meshes = std::move(o.meshes);
        owner = o.owner, renderer = o.renderer, uploaded = o.uploaded;
        o.meshes.clear();
        o.owner = nullptr, o.renderer = nullptr, o.uploaded = false;
    }
    return *this;
}

void SkinnedModel::release() {
    if (renderer)
        for (Mesh &m : meshes) {
            if (m.id)
                renderer->destroyMesh(m.id);
            if (m.shineId)
                renderer->destroyMesh(m.shineId);
            m.id = m.shineId = 0;
        }
    renderer = nullptr;
    uploaded = false;
}

void GpuModel::upload(SkinnedModel &skinned) {
    if (skinned.owner != this || skinned.meshes.size() != _model.meshes.size())
        return;
    if (skinned.renderer && skinned.renderer != &_renderer)
        skinned.release();
    skinned.renderer = &_renderer;
    auto put = [&](gfx::MeshId &id, const std::vector<gfx::Vertex> &verts, const std::vector<uint32_t> &indices) {
        if (id && _renderer.setMeshVertices(id, verts)) {
            _renderer.destroyMesh(id);
            id = 0;
        }
        if (!id && _renderer.createMesh(verts, indices, id))
            id = 0;
    };
    for (size_t m = 0; m < _model.meshes.size(); m++) {
        SkinnedModel::Mesh &made = skinned.meshes[m];
        if (!made.drawn || made.verts.empty())
            continue;
        put(made.id, made.verts, _model.meshes[m].indices);
        if (made.shiny)
            put(made.shineId, made.shine, _model.meshes[m].indices);
    }
    skinned.uploaded = true;
}

size_t GpuModel::draw(const Pose &pose, const gfx::Mat4 &placement, bool helpers, std::vector<gfx::MeshId> &frameMeshes,
                      float brightness, const SceneLighting *lighting, const Material &material,
                      const StencilShadow *shadow, const SkinnedModel *skinned) {
    SkinnedModel own;
    if (!skinned || skinned->owner != this || skinned->meshes.size() != _model.meshes.size()) {
        skinAndLight(pose, placement, helpers, lighting, material, own);
        skinned = &own;
    }
    // Uploaded already, into meshes kept: else made for this frame alone.
    const bool kept = skinned->uploaded;
    size_t triangles = 0;
    const bool stencil = shadow && shadow->scene && _renderer.hasStencil();
    const std::array<float, 3> light = stencil ? shadowLight(*shadow->scene) : std::array<float, 3>{};
    for (size_t m = 0; m < _model.meshes.size(); m++) {
        const ModelMesh &mesh = _model.meshes[m];
        const SkinnedModel::Mesh &made = skinned->meshes[m];
        if (!made.drawn)
            continue;
        bool textured = false;
        for (const auto &g : mesh.groups)
            textured |= g.texture >= 0;
        const std::vector<gfx::Vertex> &verts = made.verts;
        const std::vector<gfx::Vertex> &shine = made.shine;
        const bool shiny = made.shiny;
        gfx::MeshId id = kept ? made.id : 0;
        if (kept && !id)
            continue;
        if (!kept) {
            if (_renderer.createMesh(verts, mesh.indices, id))
                continue;
            frameMeshes.push_back(id);
        }
        if (stencil && shadow->cast && textured) {
            if (mesh.rigid && !mesh.vertices.empty()) {
                drawShadow(mesh, pose, placement, *shadow->scene, frameMeshes);
            } else {
                // A skinned mesh is already in the world: the body's own
                // vertices, through D3DXMatrixShadow onto the floor (y = 0)
                // as the game's world matrix.
                gfx::Mat4 flat{};
                flat[0] = flat[10] = flat[15] = light[1];
                flat[4] = -light[0], flat[6] = -light[2], flat[7] = -1;
                shadowPass(id, uint32_t(mesh.indices.size()), gfx::multiply(flat, placement));
            }
        }
        if (stencil) {
            _renderer.setStencilMode(gfx::Renderer3D::StencilMode::kMark);
            _renderer.setStencilReference(shadow->ref);
        }
        // The specular, which Direct3D adds after the texture
        // (D3DRS_SPECULARENABLE, 0x427160): a pass adding it over the model.
        gfx::MeshId shineId = kept && shiny ? made.shineId : 0;
        if (!kept && shiny && !_renderer.createMesh(shine, mesh.indices, shineId))
            frameMeshes.push_back(shineId);
        for (const auto &g : mesh.groups) {
            gfx::DrawCall call;
            call.mesh = id;
            call.model = placement;
            call.firstIndex = g.firstIndex;
            call.indexCount = g.indexCount;
            call.texture = g.texture >= 0 && size_t(g.texture) < _textures.size() ? _textures[size_t(g.texture)] : 0;
            if (!call.texture)
                call.color = {0.8f, 0.4f, 0.8f, 1};
            for (int k = 0; k < 3; k++)
                call.color[size_t(k)] *= brightness;
            if (lighting) {
                call.rgbSource = gfx::ColorSource::kVertex;
            } else {
                // The light in model space, so it stays on the model's
                // front as it turns.
                call.rgbSource = gfx::ColorSource::kLighting;
                call.ambientLight = {0.45f, 0.45f, 0.45f};
                call.directedLight = {0.75f, 0.72f, 0.68f};
                const gfx::Vec3 l = gfx::transformPoint(placement, {0.4f, -0.6f, 0.7f});
                const gfx::Vec3 o = gfx::transformPoint(placement, {0, 0, 0});
                gfx::Vec3 dir{l[0] - o[0], l[1] - o[1], l[2] - o[2]};
                const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                call.lightDirection = len > 0 ? gfx::Vec3{dir[0] / len, dir[1] / len, dir[2] / len} : dir;
            }
            call.state.cull = gfx::Cull::kNone;
            if (material.blend) {
                call.color[3] = std::clamp(material.alpha, 0.0f, 1.0f);
                call.state.src = gfx::Blend::kSrcAlpha;
                call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
            }
            _renderer.draw(call);
            triangles += g.indexCount / 3;
            if (shineId) {
                gfx::DrawCall add = call;
                add.mesh = shineId;
                add.texture = 0;
                add.color = {brightness, brightness, brightness, call.color[3]};
                add.state.src = material.blend ? gfx::Blend::kSrcAlpha : gfx::Blend::kOne;
                add.state.dst = gfx::Blend::kOne;
                add.state.depthEqual = true;
                add.state.depthWrite = false;
                _renderer.draw(add);
            }
        }
        if (stencil)
            _renderer.setStencilMode(gfx::Renderer3D::StencilMode::kNone);
    }
    return triangles;
}

void GpuModel::drawShadow(const ModelMesh &mesh, const Pose &pose, const gfx::Mat4 &placement, const Ac2Scene &scene,
                          std::vector<gfx::MeshId> &frameMeshes) {
    // A rigid mesh (the cleaver's, the luggage's) is drawn with its bone's
    // matrix as the world, which the shadow replaces (0x4285f0): its own
    // vertices (turned to the game's axes) are flattened onto the plane at
    // the bone's height, from a light moved by the bone's position. So its
    // shadow lies about the world's origin, not under the prop.
    std::vector<gfx::Vertex> verts(mesh.vertices.size());
    const size_t bone = mesh.vertices[0].bones[0];
    const gfx::Mat4 world = bone < pose.skinning.size() ? gfx::multiply(placement, pose.skinning[bone]) : placement;
    const float origin[3] = {world[12], world[13], world[14]};
    const std::array<float, 3> moved = shadowLight(scene, origin);
    for (size_t v = 0; v < verts.size(); v++) {
        verts[v] = {};
        verts[v].color[0] = verts[v].color[1] = verts[v].color[2] = verts[v].color[3] = 255;
        const float *q = mesh.vertices[v].position;
        const float p[3] = {q[0], q[2], -q[1]};
        const std::array<float, 3> f = flattenOntoPlane(moved, origin[1], p);
        std::copy(f.begin(), f.end(), verts[v].pos);
    }
    gfx::MeshId id = 0;
    if (_renderer.createMesh(verts, mesh.indices, id))
        return;
    frameMeshes.push_back(id);
    shadowPass(id, uint32_t(mesh.indices.size()), gfx::identity());
}

void GpuModel::shadowPass(gfx::MeshId mesh, uint32_t indexCount, const gfx::Mat4 &model) {
    // No colour (the frame kept, blend Zero/One) and no depth: the stencil
    // set to 1 where it passes the depth test (0x428250).
    _renderer.setStencilMode(gfx::Renderer3D::StencilMode::kMark);
    _renderer.setStencilReference(1);
    gfx::DrawCall call;
    call.mesh = mesh;
    call.indexCount = indexCount;
    call.model = model;
    call.state.src = gfx::Blend::kZero;
    call.state.dst = gfx::Blend::kOne;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::kNone;
    _renderer.draw(call);
    _renderer.setStencilMode(gfx::Renderer3D::StencilMode::kNone);
}

Pose poseAt(const Model &model, const Animation *animation, float time) {
    if (animation && animation->duration > 0)
        time = std::fmod(time, animation->duration);
    Pose pose;
    computePose(model, animation, time, pose);
    return pose;
}

} // namespace reluva::awe
