// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/model_cache.h"

#include "common/fs.h"

#include <cmath>

namespace reluva::idtech3 {

namespace {

constexpr float kPi = 3.14159265358979f;

} // namespace

std::array<Vec3, 3> anglesToAxes(const Vec3& angles) {
    const float p = angles[0] * kPi / 180, y = angles[1] * kPi / 180, r = angles[2] * kPi / 180;
    const float sp = std::sin(p), cp = std::cos(p), sy = std::sin(y), cy = std::cos(y), sr = std::sin(r),
                cr = std::cos(r);
    const Vec3 forward{cp * cy, cp * sy, -sp};
    const Vec3 right{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
    const Vec3 up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
    return {forward, Vec3{-right[0], -right[1], -right[2]}, up};
}

gfx::Mat4 placement(const Vec3& origin, const std::array<Vec3, 3>& axes) {
    gfx::Mat4 m = gfx::identity();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            m[size_t(c * 4 + r)] = axes[size_t(c)][size_t(r)];
    m[12] = origin[0];
    m[13] = origin[1];
    m[14] = origin[2];
    return m;
}

gfx::Mat4 tagPlacement(const Md3Tag& tag) { return placement(tag.origin, tag.axis); }

void ModelCache::clear() {
    for (Loaded& m : models_)
        if (m.mesh)
            r_.destroyMesh(m.mesh);
    models_.clear();
    byPath_.clear();
    skins_.clear();
}

const Skin& ModelCache::skin(const std::string& path) {
    const std::string key = normalizeArchiveName(path);
    if (auto it = skins_.find(key); it != skins_.end())
        return it->second;
    std::vector<uint8_t> data;
    Skin s;
    if (!files_.read(path, data))
        s = parseSkin(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
    return skins_[key] = std::move(s);
}

int ModelCache::load(const std::string& path, std::string* error) {
    const std::string key = normalizeArchiveName(path);
    if (auto it = byPath_.find(key); it != byPath_.end()) {
        if (it->second < 0 && error)
            *error = path + " couldn't be loaded";
        return it->second;
    }
    Loaded m;
    std::vector<uint8_t> data;
    Error err = files_.read(path, data);
    if (!err)
        err = parseMd3(data, m.md3);

    // Every surface's frames one after another; indices point into frame
    // 0 and a draw's vertex offset picks the frame.
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    if (!err) {
        for (const Md3Surface& s : m.md3.surfaces) {
            Surface out;
            out.firstIndex = uint32_t(indices.size());
            out.indexCount = uint32_t(s.indices.size());
            out.vertexCount = s.vertexCount;
            out.material = shaders_.material(s.shaders.empty() ? std::string() : s.shaders.front(), Lighting::Diffuse);
            const uint32_t base = uint32_t(vertices.size());
            for (size_t i = 0; i < s.vertices.size(); ++i) {
                gfx::Vertex v{};
                for (int k = 0; k < 3; ++k) {
                    v.pos[k] = s.vertices[i].pos[size_t(k)];
                    v.normal[k] = s.vertices[i].normal[size_t(k)];
                }
                const auto& st = s.st[i % size_t(s.vertexCount)];
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
            err = r_.createMesh(vertices, indices, m.mesh);
    }
    if (err) {
        if (error)
            *error = err.message;
        byPath_[key] = -1;
        return -1;
    }
    models_.push_back(std::move(m));
    const int id = static_cast<int>(models_.size()) - 1;
    byPath_[key] = id;
    return id;
}

void ModelCache::draw(int id, int frame, const gfx::Mat4& transform, const PointLight& light, float time,
                      const Skin* skin, float sortMin, float sortMax) {
    if (id < 0)
        return;
    const Loaded& m = models_[size_t(id)];
    const int frames = int(m.md3.frames.size());
    frame = ((frame % frames) + frames) % frames;
    for (size_t i = 0; i < m.surfaces.size(); ++i) {
        const Surface& s = m.surfaces[i];
        int material = s.material;
        if (skin)
            if (auto it = skin->find(fs::toLower(m.md3.surfaces[i].name)); it != skin->end())
                material = shaders_.material(it->second, Lighting::Diffuse);
        const ShaderCache::Material& mat = shaders_.get(material);
        if (mat.skip || mat.sort < sortMin || mat.sort > sortMax)
            continue;
        gfx::DrawCall call;
        call.mesh = m.mesh;
        call.firstIndex = s.firstIndex;
        call.indexCount = s.indexCount;
        call.vertexOffset = frame * s.vertexCount;
        call.model = transform;
        for (int c = 0; c < 3; ++c) {
            call.ambientLight[size_t(c)] = light.ambient[size_t(c)] / 255;
            call.directedLight[size_t(c)] = light.directed[size_t(c)] / 255;
            call.lightDirection[size_t(c)] = light.direction[size_t(c)];
        }
        shaders_.draw(material, call, 0, time);
    }
}

} // namespace reluva::idtech3
