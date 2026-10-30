// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/render_cache.h"

#include "common/fs.h"
#include "engines/age/texture.h"
#include "engines/age/vecmath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

gfx::TextureId TextureCache::create(const Tex& tex, bool& translucent) {
    translucent = false;
    for (size_t i = 3; i < tex.image.rgba.size(); i += 4)
        if (tex.image.rgba[i] != 255) {
            translucent = true;
            break;
        }
    gfx::TextureParams params;
    params.anisotropic = true;
    // Flag bit 0: kept to its edges, not repeated (cars' bodies and the
    // like; tex.md).
    params.clamp = tex.flags & texflag::Clamp;
    gfx::TextureId id = 0;
    if (renderer_.createTexture(tex.image.w, tex.image.h, tex.image.rgba.data(), params, id))
        return 0;
    return id;
}

gfx::TextureId TextureCache::get(const std::string& name, bool& translucent, bool optional) {
    const std::string key = fs::toLower(name);
    auto it = textures_.find(key);
    if (it == textures_.end()) {
        Entry e;
        // An animation: the name, or one of its frames ("s_thames-0009").
        std::string base = key;
        size_t named = 0;
        if (base.size() > 5 && base[base.size() - 5] == '-' &&
            std::all_of(base.end() - 4, base.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            named = size_t(std::stoul(base.substr(base.size() - 4)));
            base.resize(base.size() - 5);
        }
        std::vector<uint8_t> movie;
        Tex tex;
        if (!files_.read("tune/" + base + ".movie", movie)) {
            Animation a;
            const std::string text(movie.begin(), movie.end());
            if (const size_t at = text.find("rate"); at != std::string::npos)
                a.rate = std::max(1.0f, std::strtof(text.c_str() + at + 4, nullptr));
            for (int f = 1; f < 1000; ++f) {
                char frame[16];
                std::snprintf(frame, sizeof frame, "-%04d", f);
                if (loadTexture(files_, base + frame, tex))
                    break;
                bool t;
                const gfx::TextureId id = create(tex, t);
                e.translucent = e.translucent || t;
                a.frames.push_back(id);
                frameIds_.push_back(id);
            }
            if (!a.frames.empty()) {
                a.first = named ? (named - 1) % a.frames.size() : 0;
                e.id = a.frames[a.first];
                animations_[e.id] = a;
            }
        }
        if (!e.id) {
            if (loadTexture(files_, name, tex)) {
                if (!optional)
                    missing_.push_back(name);
            } else {
                e.id = create(tex, e.translucent);
                e.width = tex.image.w;
                e.height = tex.image.h;
            }
        }
        it = textures_.emplace(key, e).first;
    }
    translucent = it->second.translucent;
    return it->second.id;
}

bool TextureCache::size(const std::string& name, int& width, int& height) const {
    const auto it = textures_.find(fs::toLower(name));
    if (it == textures_.end() || !it->second.id)
        return false;
    width = it->second.width;
    height = it->second.height;
    return true;
}

gfx::TextureId TextureCache::frame(gfx::TextureId id) const {
    const auto it = animations_.find(id);
    if (it == animations_.end())
        return id;
    const Animation& a = it->second;
    return a.frames[(a.first + size_t(std::max(0.0f, time_) * a.rate)) % a.frames.size()];
}

void TextureCache::clear() {
    for (auto& [name, e] : textures_)
        if (e.id && !animations_.count(e.id))
            renderer_.destroyTexture(e.id);
    for (gfx::TextureId id : frameIds_)
        if (id)
            renderer_.destroyTexture(id);
    textures_.clear();
    animations_.clear();
    frameIds_.clear();
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
        // A prop's break pieces (BREAK01...) are drawn on their own once it
        // comes apart (props.h), not with the whole.
        if (!includePart && m.part().rfind("BREAK", 0) == 0)
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
                d.ambient = {s->ambient[0], s->ambient[1], s->ambient[2]};
                d.emissive = {s->emissive[0], s->emissive[1], s->emissive[2]};
                d.textureName = s->texture;
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
            Vec3 groupLo{1e30f, 1e30f, 1e30f}, groupHi{-1e30f, -1e30f, -1e30f};
            for (const PkgVertex& v : g.vertices) {
                gfx::Vertex o{};
                for (size_t k = 0; k < 3; ++k) {
                    o.pos[k] = v.pos[k] + at[k];
                    o.normal[k] = v.normal[k];
                    lo[k] = std::min(lo[k], o.pos[k]);
                    hi[k] = std::max(hi[k], o.pos[k]);
                    groupLo[k] = std::min(groupLo[k], o.pos[k]);
                    groupHi[k] = std::max(groupHi[k], o.pos[k]);
                }
                o.uv[0] = v.uv[0];
                o.uv[1] = v.uv[1];
                o.color[0] = o.color[1] = o.color[2] = o.color[3] = 255;
                vertices.push_back(o);
            }
            for (uint16_t i : g.indices)
                indices.push_back(first + i);
            if (!g.vertices.empty())
                d.center = (groupLo + groupHi) * 0.5f;
            out.draws.push_back(d);
            out.vertices += g.vertices.size();
            out.triangles += g.indices.size() / 3;
        }
    }
    if (vertices.empty())
        return Error::success();
    out.low = lo;
    out.high = hi;
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
                   bool translucent, const TextureCache* animate) {
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
        call.texture = animate ? animate->frame(d.texture) : d.texture;
        call.color = d.color;
        call.rgbSource = d.lit ? gfx::ColorSource::Lighting : gfx::ColorSource::Constant;
        call.ambientLight = light.ambient;
        call.directedLight = light.directed;
        call.lightDirection = light.direction;
        call.fillLight = light.fill;
        call.fillDirection = light.fillDirection;
        // Its shader's colours lit as Direct3D lights a material (most of
        // the game's give an ambient colour of 0.9 whatever the diffuse).
        call.materialLit = d.lit;
        call.materialAmbient = d.ambient;
        call.materialEmissive = d.emissive;
        applyFog(call, light, d.glow);
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

void applyFog(gfx::DrawCall& call, const SceneLight& light, bool additive) {
    if (light.fogEnd <= 0)
        return;
    call.fogColor = additive ? gfx::Vec3{} : light.fogColor;
    call.fogNear = light.fogStart;
    call.fogFar = light.fogEnd;
}

const ModelCache::Model& ModelCache::get(const std::string& name, uint32_t paintJob) {
    const std::string key = fs::toLower(name) + (paintJob ? "#" + std::to_string(paintJob) : "");
    auto it = models_.find(key);
    if (it != models_.end())
        return *it->second;
    auto model = std::make_unique<Model>();
    const std::string file = "geometry/" + name + ".pkg";
    std::vector<uint8_t> data;
    Pkg pkg;
    if (!files_.read(file, data) && !readPkg(data, pkg)) {
        model->paintJobs = std::max(1u, pkg.paintJobs);
        model->xrefs = pkg.xrefs;
        paintJob %= model->paintJobs;
        for (const std::string& level : modelLevels(pkg)) {
            ModelMesh mesh;
            if (buildModelMesh(renderer_, textures_, files_, file, pkg, level, paintJob, false, mesh))
                break;
            model->levels.push_back(std::move(mesh));
        }
        if (model->levels.empty()) { // a model in one piece without level names
            ModelMesh mesh;
            if (!buildModelMesh(renderer_, textures_, files_, file, pkg, "", paintJob, false, mesh) && mesh.mesh)
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

const ModelMesh& ModelCache::part(const std::string& name, const std::string& part, uint32_t paintJob) {
    const std::string key = fs::toLower(name) + "/" + part + "#" + std::to_string(paintJob);
    auto it = parts_.find(key);
    if (it != parts_.end())
        return *it->second;
    auto mesh = std::make_unique<ModelMesh>();
    const std::string file = "geometry/" + name + ".pkg";
    std::vector<uint8_t> data;
    Pkg pkg;
    if (!files_.read(file, data) && !readPkg(data, pkg)) {
        // Its finest level (a dashboard's parts); lit parts at whatever level.
        const auto levels = modelLevels(pkg);
        buildModelMesh(renderer_, textures_, files_, file, pkg, levels.empty() ? "" : levels.front(),
                       paintJob % std::max(1u, pkg.paintJobs), true, *mesh,
                       [&](const std::string& p) { return p == part; });
    }
    return *parts_.emplace(key, std::move(mesh)).first->second;
}

bool ModelCache::partOrigin(const std::string& name, const std::string& part, Vec3& out) {
    const std::string key = fs::toLower(name) + "/" + part;
    auto it = origins_.find(key);
    if (it == origins_.end()) {
        std::vector<uint8_t> data;
        Mtx mtx;
        const bool ok = !files_.read("geometry/" + name + "_" + part + ".mtx", data) && !readMtx(data, mtx);
        it = origins_.emplace(key, std::make_pair(ok, ok ? mtx.origin : Vec3{})).first;
    }
    out = it->second.second;
    return it->second.first;
}

void ModelCache::clear() {
    for (auto& [name, m] : models_)
        for (ModelMesh& mesh : m->levels)
            destroyModelMesh(renderer_, mesh);
    models_.clear();
    for (auto& [name, m] : parts_)
        destroyModelMesh(renderer_, *m);
    parts_.clear();
    origins_.clear();
}

FlareDrawer::FlareDrawer(gfx::Renderer3D& renderer, TextureCache& textures) : renderer_(renderer) {
    // A unit square in X and Y, drawn turned to face the camera.
    std::vector<gfx::Vertex> v(4);
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for (size_t i = 0; i < 4; ++i) {
        v[i].pos[0] = corners[i][0];
        v[i].pos[1] = corners[i][1];
        v[i].uv[0] = corners[i][0] + 0.5f;
        v[i].uv[1] = 0.5f - corners[i][1];
        v[i].normal[2] = 1;
        v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
    }
    renderer.createMesh(v, {0, 1, 2, 0, 2, 3}, quad_);
    bool translucent = false;
    texture_ = textures.get("lt_glow", translucent);
}

FlareDrawer::~FlareDrawer() {
    if (quad_)
        renderer_.destroyMesh(quad_);
}

void FlareDrawer::draw(const Vec3& at, float size, const std::array<float, 3>& color, const Vec3& eye) const {
    if (!quad_ || size <= 0)
        return;
    Vec3 toEye = eye - at;
    const float d = length(toEye);
    if (d < 0.1f)
        return;
    toEye = toEye * (1 / d);
    Vec3 right = cross(Vec3{0, 1, 0}, toEye);
    right = length(right) > 1e-3f ? normalized(right) : Vec3{1, 0, 0};
    const Vec3 up = cross(toEye, right);
    // A little towards the camera, so the lamp's own glass doesn't cover it.
    const Mat3 axes{right * size, up * size, toEye};
    const Vec3 p = at + toEye * std::min(0.3f, d * 0.5f);
    gfx::DrawCall call;
    call.mesh = quad_;
    call.indexCount = 6;
    call.model = {axes.x[0], axes.x[1], axes.x[2], 0, axes.y[0], axes.y[1], axes.y[2], 0,
                  axes.z[0], axes.z[1], axes.z[2], 0, p[0], p[1], p[2], 1};
    call.texture = texture_;
    call.color = {color[0], color[1], color[2], 1};
    call.state.src = call.state.dst = gfx::Blend::One;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::None;
    renderer_.draw(call);
}

ViewFrustum::ViewFrustum(const gfx::Mat4& viewProj) {
    const float* m = viewProj.data();
    auto row = [&](int r) { return std::array<float, 4>{m[r], m[4 + r], m[8 + r], m[12 + r]}; };
    const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    for (size_t k = 0; k < 4; ++k) {
        planes_[0][k] = r3[k] + r0[k];
        planes_[1][k] = r3[k] - r0[k];
        planes_[2][k] = r3[k] + r1[k];
        planes_[3][k] = r3[k] - r1[k];
        planes_[4][k] = r2[k];
    }
    for (auto& p : planes_) {
        const float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& c : p)
            c /= len;
    }
}

bool ViewFrustum::visible(const gfx::Vec3& c, float radius) const {
    for (const auto& p : planes_)
        if (p[0] * c[0] + p[1] * c[1] + p[2] * c[2] + p[3] < -radius)
            return false;
    return true;
}

} // namespace reluva::age
