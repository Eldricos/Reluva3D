// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/render/model_cache.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3 {

namespace {

constexpr float kPi = 3.14159265358979f;

} // namespace

std::array<Vec3, 3> anglesToAxes(const Vec3 &angles) {
    const float p = angles[0] * kPi / 180;
    const float y = angles[1] * kPi / 180;
    const float r = angles[2] * kPi / 180;
    const float sp = std::sin(p);
    const float cp = std::cos(p);
    const float sy = std::sin(y);
    const float cy = std::cos(y);
    const float sr = std::sin(r);
    const float cr = std::cos(r);
    const Vec3 forward{cp * cy, cp * sy, -sp};
    const Vec3 right{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    const Vec3 up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    return {forward, Vec3{-right[0], -right[1], -right[2]}, up};
}

gfx::Mat4 placement(const Vec3 &origin, const std::array<Vec3, 3> &axes) {
    gfx::Mat4 m = gfx::identity();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            m[size_t(c * 4 + r)] = axes[size_t(c)][size_t(r)];
    m[12] = origin[0];
    m[13] = origin[1];
    m[14] = origin[2];
    return m;
}

gfx::Mat4 tagPlacement(const Md3Tag &tag) {
    return placement(tag.origin, tag.axis);
}

void ModelCache::clear() {
    for (Loaded &m : _models)
        if (m.mesh)
            _r.destroyMesh(m.mesh);
    _models.clear();
    _byPath.clear();
    _skins.clear();
    _skeletons.clear();
}

const Skin &ModelCache::skin(const std::string &path) {
    const std::string key = normalizeArchiveName(path);
    if (auto it = _skins.find(key); it != _skins.end())
        return it->second;
    std::vector<uint8_t> data;
    Skin s;
    if (!_files.read(path, data))
        s = parseSkin(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
    return _skins[key] = std::move(s);
}

int ModelCache::load(const std::string &path, std::string *error) {
    const std::string key = normalizeArchiveName(path);
    if (auto it = _byPath.find(key); it != _byPath.end()) {
        if (it->second < 0 && error)
            *error = path + " couldn't be loaded";
        return it->second;
    }
    std::vector<uint8_t> data;
    std::string why;
    Error err = _files.read(path, data);
    const int id = err ? -1 : add(path, data, 0, &why);
    if (id < 0) {
        if (error)
            *error = err ? err.message : why;
        _byPath[key] = -1;
        return -1;
    }
    _byPath[key] = id;
    // The lower levels of detail: an MDR's own, or an MD3's files beside it.
    std::vector<int> lods;
    if (data.size() >= 148 && std::memcmp(data.data(), "2LGM", 4) == 0) {
        int32_t count = 0;
        std::memcpy(&count, &data[144], 4);
        for (int level = 1; level < std::min(count, 8); ++level)
            if (const int lodId = add(path, data, level, nullptr); lodId >= 0)
                lods.push_back(lodId);
    } else if (data.size() >= 88 && std::memcmp(data.data(), "RDM5", 4) == 0) {
        int32_t count = 0;
        std::memcpy(&count, &data[84], 4);
        for (int level = 1; level < std::min(count, 8); ++level)
            if (const int lodId = add(path, data, level, nullptr); lodId >= 0)
                lods.push_back(lodId);
    } else if (const std::string lower = fs::toLower(path);
               lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".md3") == 0) {
        for (int level = 1; level <= 2; ++level) {
            std::vector<uint8_t> lodData;
            const std::string lodPath = path.substr(0, path.size() - 4) + "_" + std::to_string(level) + ".md3";
            const int lodId = _files.read(lodPath, lodData) ? -1 : add(lodPath, lodData, 0, nullptr);
            if (lodId < 0)
                break;
            lods.push_back(lodId);
        }
    }
    _models[size_t(id)].lods = std::move(lods);
    return id;
}

float ModelCache::radius(int id, int frame) const {
    const Md3Model &md3 = _models[size_t(id)].md3;
    if (md3.frames.empty())
        return 0;
    const int frames = int(md3.frames.size());
    const Md3Frame &f = md3.frames[size_t(((frame % frames) + frames) % frames)];
    float r2 = 0;
    for (size_t k = 0; k < 3; ++k) {
        const float c = std::max(std::fabs(f.mins[k]), std::fabs(f.maxs[k]));
        r2 += c * c;
    }
    return std::sqrt(r2);
}

int ModelCache::add(const std::string &path, const std::vector<uint8_t> &data, int level, std::string *error) {
    Loaded m;
    Error err;
    if (data.size() >= 4 && std::memcmp(data.data(), "2LGM", 4) == 0) {
        auto glm = std::make_shared<GlmModel>();
        std::string why;
        err = parseGlm(data, *glm);
        if (!err && !addGhoul2(glm, level, m, why))
            err = Error::fail(path + ": " + why);
    } else {
        err = parseModel(data, m.md3, level);
    }

    // Every surface's frames one after another; indices point into frame
    // 0 and a draw's vertex offset picks the frame.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    if (!err) {
        for (const Md3Surface &s : m.md3.surfaces) {
            Surface out;
            out.firstIndex = uint32_t(indices.size());
            out.indexCount = uint32_t(s.indices.size());
            out.vertexCount = s.vertexCount;
            // The surface's own shader is looked up when first drawn
            // without a skin naming another, so unused ones aren't loaded.
            const uint32_t base = uint32_t(vertices.size());
            for (size_t i = 0; i < s.vertices.size(); ++i) {
                gfx::Vertex v{};
                for (int k = 0; k < 3; ++k) {
                    v.pos[k] = s.vertices[i].pos[size_t(k)];
                    v.normal[k] = s.vertices[i].normal[size_t(k)];
                }
                const auto &st = s.st[i % size_t(s.vertexCount)];
                v.uv[0] = st[0];
                v.uv[1] = st[1];
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
                vertices.push_back(v);
            }
            for (uint32_t i : s.indices)
                indices.push_back(base + i);
            m.surfaces.push_back(out);
        }
        if (indices.empty())
            err = Error::fail(path + " has no triangles");
        else
            err = _r.createMesh(vertices, indices, m.mesh);
    }
    if (err) {
        if (error)
            *error = err.message;
        return -1;
    }
    _models.push_back(std::move(m));
    return static_cast<int>(_models.size()) - 1;
}

const GlaSkeleton *ModelCache::skeleton(const std::string &name) {
    const std::string key = normalizeArchiveName(name);
    if (auto it = _skeletons.find(key); it != _skeletons.end())
        return it->second.get();
    std::vector<uint8_t> data;
    auto gla = std::make_unique<GlaSkeleton>();
    if (_files.read(name + ".gla", data) || parseGla(data, *gla))
        gla.reset();
    return (_skeletons[key] = std::move(gla)).get();
}

bool ModelCache::addGhoul2(const std::shared_ptr<const GlmModel> &glm, int level, Loaded &m, std::string &error) {
    const GlaSkeleton *skel = nullptr;
    if (normalizeArchiveName(glm->animName) != "*default") {
        skel = skeleton(glm->animName);
        if (!skel) {
            error = "its skeleton " + glm->animName + ".gla can't be read";
            return false;
        }
        if (int(skel->bones.size()) != glm->boneCount) {
            error = "its skeleton " + glm->animName + ".gla has another number of bones";
            return false;
        }
    }
    if (level >= int(glm->lods.size())) {
        error = "it has no level of detail " + std::to_string(level);
        return false;
    }
    m.glm = glm;
    m.skeleton = skel;
    m.level = level;
    m.md3.name = glm->name;
    // Only the surfaces with no flags are drawn: not those off until the
    // game turns them on, nor bolt points.
    std::vector<Bone34> bones;
    poseBones(skel, 0, 0, 0, bones);
    Md3Frame bounds;
    bounds.mins = {1e30f, 1e30f, 1e30f};
    bounds.maxs = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i < glm->surfaces.size(); ++i) {
        const GlmSurfaceInfo &info = glm->surfaces[i];
        const GlmSurface &in = glm->lods[size_t(level)][i];
        if (info.flags != 0 || in.indices.empty())
            continue;
        Md3Surface out;
        out.name = info.name;
        out.shaders = {info.shader};
        out.indices = in.indices;
        out.st = in.st;
        out.vertexCount = int(in.vertices.size());
        skinSurface(in, bones, out.vertices);
        for (const Md3Vertex &v : out.vertices)
            for (size_t k = 0; k < 3; ++k) {
                bounds.mins[k] = std::min(bounds.mins[k], v.pos[k]);
                bounds.maxs[k] = std::max(bounds.maxs[k], v.pos[k]);
            }
        m.md3.surfaces.push_back(std::move(out));
        m.glmSurfaces.push_back(i);
    }
    // Every frame bounded as the first: enough to pick a level of detail.
    float r2 = 0;
    for (size_t k = 0; k < 3; ++k) {
        const float c = std::max(std::fabs(bounds.mins[k]), std::fabs(bounds.maxs[k]));
        r2 += c * c;
    }
    bounds.radius = std::sqrt(r2);
    m.md3.frames.assign(size_t(skel ? skel->frameCount : 1), bounds);
    return true;
}

gfx::MeshId ModelCache::poseGhoul2(const Loaded &m, int frame, int oldFrame, float backLerp) {
    std::vector<Bone34> bones;
    poseBones(m.skeleton, frame, oldFrame, backLerp, bones);
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Md3Vertex> posed;
    for (size_t i = 0; i < m.glmSurfaces.size(); ++i) {
        const GlmSurface &in = m.glm->lods[size_t(m.level)][m.glmSurfaces[i]];
        skinSurface(in, bones, posed);
        const auto base = uint32_t(vertices.size());
        for (size_t v = 0; v < posed.size(); ++v) {
            gfx::Vertex g{};
            const float len =
                std::sqrt(posed[v].normal[0] * posed[v].normal[0] + posed[v].normal[1] * posed[v].normal[1] +
                          posed[v].normal[2] * posed[v].normal[2]);
            for (size_t k = 0; k < 3; ++k) {
                g.pos[k] = posed[v].pos[k];
                g.normal[k] = len > 0 ? posed[v].normal[k] / len : 0;
            }
            g.uv[0] = in.st[v][0];
            g.uv[1] = in.st[v][1];
            g.color[0] = g.color[1] = g.color[2] = g.color[3] = 255;
            vertices.push_back(g);
        }
        for (uint32_t idx : in.indices)
            indices.push_back(base + idx);
    }
    gfx::MeshId mesh = 0;
    if (_r.createMesh(vertices, indices, mesh))
        return 0;
    return mesh;
}

int ModelCache::surfaceMaterial(int id, size_t i, const Skin *skin, const std::string &shader) {
    Loaded &m = _models[size_t(id)];
    std::vector<int> &chosen = m.chosen[{skin, shader}];
    if (chosen.empty()) {
        chosen.resize(m.surfaces.size());
        for (size_t k = 0; k < chosen.size(); ++k)
            chosen[k] = chooseMaterial(id, k, skin, shader);
    }
    return chosen[i];
}

int ModelCache::chooseMaterial(int id, size_t i, const Skin *skin, const std::string &shader) {
    Loaded &m = _models[size_t(id)];
    int material = shader.empty() ? -1 : _shaders.material(shader, Lighting::kDiffuse);
    if (material < 0 && skin)
        if (auto it = skin->find(fs::toLower(m.md3.surfaces[i].name)); it != skin->end())
            material = _shaders.material(it->second, Lighting::kDiffuse);
    if (material >= 0)
        return material;
    Surface &s = m.surfaces[i];
    if (s.material < 0) {
        const auto &names = m.md3.surfaces[i].shaders;
        s.material = _shaders.material(names.empty() ? std::string() : names.front(), Lighting::kDiffuse);
    }
    return s.material;
}

void ModelCache::addSorts(int id, const Skin *skin, const std::string &shader, std::vector<float> &out) {
    if (id < 0)
        return;
    for (size_t i = 0; i < _models[size_t(id)].surfaces.size(); ++i)
        out.push_back(_shaders.get(surfaceMaterial(id, i, skin, shader)).sort);
}

void ModelCache::draw(int id, int frame, const gfx::Mat4 &transform, const PointLight &light, float time,
                      const Skin *skin, float sortMin, float sortMax, const std::string &shader,
                      const std::array<float, 4> *entity, int oldFrame, float backLerp) {
    if (id < 0)
        return;
    const Loaded &m = _models[size_t(id)];
    const int frames = int(m.md3.frames.size());
    frame = ((frame % frames) + frames) % frames;
    oldFrame = ((oldFrame % frames) + frames) % frames;
    if (oldFrame == frame)
        backLerp = 0;
    // A Ghoul2 model is posed now, into a mesh of its own for this draw.
    const gfx::MeshId posed = m.glm ? poseGhoul2(m, frame, oldFrame, backLerp) : 0;
    if (m.glm && !posed)
        return;
    for (size_t i = 0; i < m.surfaces.size(); ++i) {
        const Surface &s = m.surfaces[i];
        const int material = surfaceMaterial(id, i, skin, shader);
        const ShaderCache::Material &mat = _shaders.get(material);
        if (mat.skip || mat.sort < sortMin || mat.sort > sortMax)
            continue;
        gfx::DrawCall call;
        call.mesh = m.mesh;
        call.firstIndex = s.firstIndex;
        call.indexCount = s.indexCount;
        call.vertexOffset = frame * s.vertexCount;
        call.oldVertexOffset = oldFrame * s.vertexCount;
        call.backLerp = std::clamp(backLerp, 0.0f, 1.0f);
        if (posed) {
            call.mesh = posed;
            call.vertexOffset = call.oldVertexOffset = 0;
            call.backLerp = 0;
        }
        call.model = transform;
        for (int c = 0; c < 3; ++c) {
            call.ambientLight[size_t(c)] = light.ambient[size_t(c)] / 255;
            call.directedLight[size_t(c)] = light.directed[size_t(c)] / 255;
            call.lightDirection[size_t(c)] = light.direction[size_t(c)];
        }
        _shaders.draw(material, call, 0, time, false, entity);
    }
    if (posed)
        _r.destroyMesh(posed);
}

} // namespace reluva::idtech3
