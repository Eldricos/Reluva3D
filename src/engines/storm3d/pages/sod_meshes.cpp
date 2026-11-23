// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/sod_meshes.h"

#include "common/text.h"
#include "engines/storm3d/pages/model_page.h"
#include "image/image.h"

#include <algorithm>
#include <cmath>

namespace reluva::storm3d {

void SodMeshes::clear() {
    if (_renderer) {
        for (const gfx::MeshId m : _meshes)
            _renderer->destroyMesh(m);
        for (const auto &[name, id] : _textures)
            if (id)
                _renderer->destroyTexture(id);
    }
    _meshes.clear();
    _textures.clear();
    _models.clear();
}

gfx::TextureId SodMeshes::texture(const std::string &name) {
    const std::string key = text::toLower(name);
    if (const auto it = _textures.find(key); it != _textures.end())
        return it->second;
    gfx::TextureId id = 0;
    // The full-colour texture first, as the Images page shows them.
    for (const char *folder : {"Textures/RGB/", "Textures/Index8/"}) {
        std::vector<uint8_t> data;
        image::Image img;
        if (_files.read(folder + name + ".tga", data) || image::decode(data, img))
            continue;
        if (_renderer->createTexture(img.w, img.h, img.rgba.data(), gfx::TextureParams{}, id))
            id = 0;
        break;
    }
    _textures[key] = id;
    return id;
}

SodMeshes::Built SodMeshes::build(const SodModel &model, int level) {
    Built built;
    if (!_renderer)
        return built;
    const std::vector<gfx::Mat4> world = ModelPage::worldMatrices(model);
    for (size_t i = 0; i < model.nodes.size(); ++i) {
        const SodModel::Node &node = model.nodes[i];
        if (node.mesh < 0 || (level >= 0 && !ModelPage::shownAtLevel(model, i, level)))
            continue;
        const SodModel::Mesh &mesh = model.meshes[size_t(node.mesh)];
        // Normals by vertex, the faces' around it added up, for smooth
        // shading.
        std::vector<gfx::Vec3> normals(mesh.vertices.size(), gfx::Vec3{0, 0, 0});
        for (const SodModel::Group &g : mesh.groups)
            for (const SodModel::Face &f : g.faces) {
                const auto &a = mesh.vertices[f.vertex[0]];
                const auto &b = mesh.vertices[f.vertex[1]];
                const auto &c = mesh.vertices[f.vertex[2]];
                const gfx::Vec3 u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const gfx::Vec3 v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                const gfx::Vec3 n{u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
                for (const uint16_t k : f.vertex)
                    for (int d = 0; d < 3; ++d)
                        normals[k][size_t(d)] += n[size_t(d)];
            }
        std::vector<gfx::Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Part> parts;
        for (const SodModel::Group &g : mesh.groups) {
            Part part;
            part.firstIndex = uint32_t(indices.size());
            for (const SodModel::Face &f : g.faces)
                for (int k = 0; k < 3; ++k) {
                    gfx::Vertex v{};
                    const auto &p = mesh.vertices[f.vertex[size_t(k)]];
                    const auto &uv = mesh.uvs[f.uv[size_t(k)]];
                    gfx::Vec3 n = normals[f.vertex[size_t(k)]];
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    for (int d = 0; d < 3; ++d) {
                        v.pos[d] = p[size_t(d)];
                        v.normal[d] = len > 0 ? n[size_t(d)] / len : 0;
                    }
                    v.uv[0] = uv[0];
                    v.uv[1] = uv[1];
                    std::fill(std::begin(v.color), std::end(v.color), uint8_t(255));
                    indices.push_back(uint32_t(vertices.size()));
                    vertices.push_back(v);
                    const gfx::Vec3 at = gfx::transformPoint(world[i], {p[0], p[1], p[2]});
                    for (int d = 0; d < 3; ++d) {
                        built.lo[size_t(d)] = std::min(built.lo[size_t(d)], at[size_t(d)]);
                        built.hi[size_t(d)] = std::max(built.hi[size_t(d)], at[size_t(d)]);
                    }
                }
            part.indexCount = uint32_t(indices.size()) - part.firstIndex;
            for (const SodModel::LightingMaterial &m : model.materials)
                if (m.name == g.material)
                    part.colour = {std::min(1.0f, m.diffuse[0]), std::min(1.0f, m.diffuse[1]),
                                   std::min(1.0f, m.diffuse[2]), 1};
            parts.push_back(part);
        }
        if (indices.empty())
            continue;
        gfx::MeshId id = 0;
        if (_renderer->createMesh(vertices, indices, id))
            continue;
        _meshes.push_back(id);
        const gfx::TextureId tex = texture(mesh.texture);
        for (Part &part : parts) {
            part.mesh = id;
            part.texture = tex;
            part.world = world[i];
            part.drawMode = mesh.drawMode;
            built.parts.push_back(part);
        }
    }
    return built;
}

const SodMeshes::Built *SodMeshes::model(const std::string &name) {
    const std::string key = text::toLower(name);
    if (const auto it = _models.find(key); it != _models.end())
        return it->second ? &*it->second : nullptr;
    std::optional<Built> &built = _models[key];
    std::vector<uint8_t> data;
    SodModel model;
    if (_files.read("SOD/" + name + ".sod", data) || model.parse(data))
        return nullptr;
    const std::vector<int> levels = ModelPage::detailLevels(model);
    // The higher a level's number, the more detailed it is.
    built = build(model, levels.empty() ? -1 : levels.back());
    return &*built;
}

void SodMeshes::drawPart(const Part &part, const gfx::Mat4 &place, bool behind) const {
    const bool additive = part.drawMode == "additive" || part.drawMode == "wormhole";
    gfx::DrawCall call;
    call.mesh = part.mesh;
    call.firstIndex = part.firstIndex;
    call.indexCount = part.indexCount;
    call.model = gfx::multiply(place, part.world);
    call.texture = part.texture;
    call.color = part.colour;
    // Which way the faces wind isn't known yet: both sides drawn.
    call.state.cull = gfx::Cull::kNone;
    if (additive) {
        call.state.src = gfx::Blend::kOne;
        call.state.dst = gfx::Blend::kOne;
        call.state.depthWrite = false;
    } else {
        call.rgbSource = gfx::ColorSource::kLighting;
        call.ambientLight = {0.35f, 0.35f, 0.38f};
        call.directedLight = {0.8f, 0.8f, 0.75f};
        call.lightDirection = {0.42f, 0.74f, 0.52f};
        if (part.drawMode == "alphathreshold")
            call.state.alphaTest = gfx::AlphaTest::kGe128;
    }
    if (behind) {
        call.state.depthTest = false;
        call.state.depthWrite = false;
    }
    _renderer->draw(call);
}

void SodMeshes::draw(const std::vector<Part> &parts, const gfx::Mat4 &place, bool behind) const {
    draw({{&parts, place}}, behind);
}

void SodMeshes::draw(const std::vector<std::pair<const std::vector<Part> *, gfx::Mat4>> &batch, bool behind) const {
    if (!_renderer)
        return;
    // Opaque parts first, then the additive ones over them.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto &[parts, place] : batch)
            for (const Part &part : *parts)
                if ((part.drawMode == "additive" || part.drawMode == "wormhole") == (pass == 1))
                    drawPart(part, place, behind);
}

} // namespace reluva::storm3d
