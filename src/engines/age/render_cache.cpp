// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/render_cache.h"

#include "common/fs.h"
#include "engines/age/texture.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace reluva::age {

namespace {

constexpr uint32_t kFvfNormal = 0x10;
const char* const kLevels[] = {"H", "M", "L", "VL"};

bool isLevel(const std::string& level) {
    for (const char* l : kLevels)
        if (level == l)
            return true;
    return false;
}

} // namespace

gfx::TextureId TextureCache::get(const std::string& name, bool& translucent) {
    const std::string key = fs::toLower(name);
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        Entry e;
        Tex tex;
        if (loadTexture(files_, name, tex)) {
            missing_.push_back(name);
        } else {
            for (size_t i = 3; i < tex.image.rgba.size(); i += 4)
                if (tex.image.rgba[i] != 255) {
                    e.translucent = true;
                    break;
                }
            gfx::TextureParams params;
            params.anisotropic = true;
            if (renderer_.createTexture(tex.image.w, tex.image.h, tex.image.rgba.data(), params, e.id))
                e.id = 0;
        }
        it = textures_.emplace(key, e).first;
    }
    translucent = it->second.translucent;
    return it->second.id;
}

void TextureCache::clear() {
    for (auto& [name, e] : textures_)
        if (e.id)
            renderer_.destroyTexture(e.id);
    textures_.clear();
    missing_.clear();
}

std::vector<std::string> modelLevels(const Pkg& pkg) {
    std::vector<std::string> levels;
    for (const char* l : kLevels)
        for (const PkgMesh& m : pkg.meshes)
            if (m.level() == l) {
                levels.push_back(l);
                break;
            }
    return levels;
}

Error buildModelMesh(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                     const std::string& pkgName, const Pkg& pkg, const std::string& level, uint32_t paintJob,
                     bool lights, ModelMesh& out, const std::function<bool(const std::string& part)>& includePart,
                     bool placeParts) {
    out = ModelMesh{};
    const std::string base = pkgName.substr(0, pkgName.rfind('.'));
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 hi{-lo[0], -lo[1], -lo[2]};
    for (const PkgMesh& m : pkg.meshes) {
        // Meshes of the level, and any not named by level; the lit parts
        // only when asked for, at whatever level they're made.
        if (isLightPart(m.part()) ? !lights : isLevel(m.level()) && m.level() != level)
            continue;
        if (includePart && !includePart(m.part()))
            continue;
        // A part with a placement (wheels, lights, breakable pieces) is
        // modelled about its own centre and moved to its origin.
        Vec3 at{};
        Mtx mtx;
        std::vector<uint8_t> placement;
        if (placeParts && !m.part().empty() && !files.read(base + "_" + m.part() + ".mtx", placement) &&
            !readMtx(placement, mtx))
            at = mtx.origin;
        const bool lit = m.fvf & kFvfNormal;
        for (const PkgGroup& g : m.groups) {
            ModelDraw d;
            d.firstIndex = uint32_t(indices.size());
            d.indexCount = uint32_t(g.indices.size());
            if (const PkgShader* s = pkg.shader(paintJob, g.shader)) {
                d.color = s->diffuse;
                if (!s->texture.empty())
                    d.texture = textures.get(s->texture, d.translucent);
                // A glow: no diffuse colour, only emissive, added.
                if (s->diffuse[3] == 0 && (s->emissive[0] > 0 || s->emissive[1] > 0 || s->emissive[2] > 0)) {
                    d.glow = true;
                    d.color = {s->emissive[0], s->emissive[1], s->emissive[2], 1};
                }
            }
            d.translucent = d.glow || d.translucent || d.color[3] < 1;
            d.lit = lit && !d.glow;
            const uint32_t first = uint32_t(vertices.size());
            for (const PkgVertex& v : g.vertices) {
                gfx::Vertex o{};
                for (size_t k = 0; k < 3; ++k) {
                    o.pos[k] = v.pos[k] + at[k];
                    o.normal[k] = v.normal[k];
                    lo[k] = std::min(lo[k], o.pos[k]);
                    hi[k] = std::max(hi[k], o.pos[k]);
                }
                o.uv[0] = v.uv[0];
                o.uv[1] = v.uv[1];
                o.color[0] = o.color[1] = o.color[2] = o.color[3] = 255;
                vertices.push_back(o);
            }
            for (uint16_t i : g.indices)
                indices.push_back(first + i);
            out.draws.push_back(d);
            out.vertices += g.vertices.size();
            out.triangles += g.indices.size() / 3;
        }
    }
    if (vertices.empty())
        return Error::success();
    for (size_t k = 0; k < 3; ++k)
        out.center[k] = (lo[k] + hi[k]) / 2;
    out.radius = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                           (hi[2] - lo[2]) * (hi[2] - lo[2])) /
                 2;
    return renderer.createMesh(vertices, indices, out.mesh);
}

void destroyModelMesh(gfx::Renderer3D& renderer, ModelMesh& mesh) {
    if (mesh.mesh)
        renderer.destroyMesh(mesh.mesh);
    mesh = ModelMesh{};
}

void drawModelMesh(gfx::Renderer3D& renderer, const ModelMesh& mesh, const gfx::Mat4& model, const SceneLight& light,
                   bool translucent) {
    if (!mesh.mesh)
        return;
    for (const ModelDraw& d : mesh.draws) {
        if (d.translucent != translucent)
            continue;
        gfx::DrawCall call;
        call.mesh = mesh.mesh;
        call.firstIndex = d.firstIndex;
        call.indexCount = d.indexCount;
        call.model = model;
        call.texture = d.texture;
        call.color = d.color;
        call.rgbSource = d.lit ? gfx::ColorSource::Lighting : gfx::ColorSource::Constant;
        call.ambientLight = light.ambient;
        call.directedLight = light.directed;
        call.lightDirection = light.direction;
        if (d.glow) {
            call.state.src = call.state.dst = gfx::Blend::One;
            call.state.depthWrite = false;
            call.state.cull = gfx::Cull::None;
        } else if (translucent) {
            call.state.src = gfx::Blend::SrcAlpha;
            call.state.dst = gfx::Blend::OneMinusSrcAlpha;
            call.state.depthWrite = false;
            call.state.alphaTest = gfx::AlphaTest::Gt0;
        }
        renderer.draw(call);
    }
}

const ModelCache::Model& ModelCache::get(const std::string& name) {
    const std::string key = fs::toLower(name);
    auto it = models_.find(key);
    if (it != models_.end())
        return *it->second;
    auto model = std::make_unique<Model>();
    const std::string file = "geometry/" + name + ".pkg";
    std::vector<uint8_t> data;
    Pkg pkg;
    if (!files_.read(file, data) && !readPkg(data, pkg)) {
        for (const std::string& level : modelLevels(pkg)) {
            ModelMesh mesh;
            if (buildModelMesh(renderer_, textures_, files_, file, pkg, level, 0, false, mesh))
                break;
            model->levels.push_back(std::move(mesh));
        }
        if (model->levels.empty()) { // a model in one piece without level names
            ModelMesh mesh;
            if (!buildModelMesh(renderer_, textures_, files_, file, pkg, "", 0, false, mesh) && mesh.mesh)
                model->levels.push_back(std::move(mesh));
        }
        if (!model->levels.empty()) {
            model->ok = true;
            model->radius = model->levels.front().radius;
            model->center = model->levels.front().center;
        }
    }
    return *models_.emplace(key, std::move(model)).first->second;
}

void ModelCache::clear() {
    for (auto& [name, m] : models_)
        for (ModelMesh& mesh : m->levels)
            destroyModelMesh(renderer_, mesh);
    models_.clear();
}

} // namespace reluva::age
