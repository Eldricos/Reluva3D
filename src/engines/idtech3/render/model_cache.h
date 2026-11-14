// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/formats/ghoul2.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/render/shader_cache.h"

#include "common/archive.h"
#include "graphics/gfx3d.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace reluva::idtech3 {

using Skin = std::map<std::string, std::string>; // surface name (lower case) -> shader

// Angles in degrees as the games give them ("pitch yaw roll", pitch down
// positive) to the axes they turn a model to: forward, left, up.
std::array<Vec3, 3> anglesToAxes(const Vec3 &angles);
// A placement: the axes as columns, then the origin.
gfx::Mat4 placement(const Vec3 &origin, const std::array<Vec3, 3> &axes);
// Where a tag puts what's attached to it, in the tagged model's space.
gfx::Mat4 tagPlacement(const Md3Tag &tag);

// MD3 models loaded for the GPU, each once: one mesh holding every frame
// of every surface, drawn through a ShaderCache.
class ModelCache {
public:
    ModelCache(gfx::Renderer3D &renderer, ShaderCache &shaders, const SearchSet &files)
        : _r(renderer), _shaders(shaders), _files(files) {}
    ~ModelCache() { clear(); }
    ModelCache(const ModelCache &) = delete;
    ModelCache &operator=(const ModelCache &) = delete;

    void clear();
    // The model at `path`, loaded on first use; -1 (with the reason in
    // `error`, if given) when it can't be.
    int load(const std::string &path, std::string *error = nullptr);
    // Models' levels of detail: an .md3's lower-detail versions beside it
    // (name_1.md3, name_2.md3), an .mdr's own. Each is loaded with the
    // model; level 0 is the model itself.
    int lodCount(int id) const { return 1 + int(_models[size_t(id)].lods.size()); }
    // The id to draw for `level` (past the last, the last).
    int lod(int id, int level) const {
        const auto &lods = _models[size_t(id)].lods;
        return level <= 0 || lods.empty() ? id : lods[size_t(std::min(level, int(lods.size()))) - 1];
    }
    // How far the model reaches from its origin in `frame`, as the
    // original measures it for choosing a level: the length of the
    // farthest corner of its box.
    float radius(int id, int frame) const;
    // The model detail setting (r_lodBias): how many levels further down
    // to draw than the distance says.
    void setLodBias(int bias) { _lodBias = bias; }
    int lodBias() const { return _lodBias; }
    const Md3Model &model(int id) const { return _models[size_t(id)].md3; }
    // Its Ghoul2 mesh, if it is one (its skeleton's name, surfaces and their flags).
    const GlmModel *ghoul2(int id) const { return _models[size_t(id)].glm.get(); }
    // A skin file's contents, read on first use (empty if missing).
    const Skin &skin(const std::string &path);

    // Draws `frame` (wrapped to the model's frames) placed by `transform`
    // and lit by `light`; a skin, if given, picks surfaces' shaders, and a
    // shader named in `shader` replaces them all. `entity`: the colour
    // rgbGen and alphaGen entity ask for. `oldFrame` and `backLerp`: the
    // frame it's on its way from and how far back towards it (0: none).
    // `sortMin`..`sortMax`: only surfaces whose shader sorts in that range,
    // so translucent ones can be drawn after the rest of a scene.
    void draw(int id, int frame, const gfx::Mat4 &transform, const PointLight &light, float time,
              const Skin *skin = nullptr, float sortMin = 0, float sortMax = 1e9f, const std::string &shader = {},
              const std::array<float, 4> *entity = nullptr, int oldFrame = 0, float backLerp = 0);
    // Adds to `out` the sorts of the shaders draw() would draw model `id`
    // with (the same `skin` and `shader`).
    void addSorts(int id, const Skin *skin, const std::string &shader, std::vector<float> &out);

private:
    int _lodBias = 0;
    struct Surface {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int vertexCount = 0;
        int material = -1; // from the model's own shader name, once first needed
    };
    struct Loaded {
        Md3Model md3;
        gfx::MeshId mesh = 0;
        std::vector<Surface> surfaces;
        std::vector<int> lods; // the lower levels' ids, from level 1
        // A Ghoul2 mesh (shared by its levels), its skeleton (nullptr:
        // "*default", one still bone) and, for each of md3's surfaces, the
        // mesh's: posed for each draw, md3 holding frame 0's pose.
        std::shared_ptr<const GlmModel> glm;
        const GlaSkeleton *skeleton = nullptr;
        int level = 0;
        std::vector<size_t> glmSurfaces;
        // The materials chosen for each skin and shader drawn with, surface
        // by surface (models are drawn many times a frame).
        std::map<std::pair<const Skin *, std::string>, std::vector<int>> chosen;
    };
    // The material surface `i` of model `id` is drawn with.
    int surfaceMaterial(int id, size_t i, const Skin *skin, const std::string &shader);
    int chooseMaterial(int id, size_t i, const Skin *skin, const std::string &shader);
    // One level of a model, from `data`; -1 with the reason in `error`.
    int add(const std::string &path, const std::vector<uint8_t> &data, int level, std::string *error);
    // A Ghoul2 mesh's level as an Md3Model posed at frame 0, which draw()
    // poses again; false with the reason in `error`.
    bool addGhoul2(const std::shared_ptr<const GlmModel> &glm, int level, Loaded &m, std::string &error);
    // A skeleton by its name without ".gla", read on first use; nullptr if
    // it can't be.
    const GlaSkeleton *skeleton(const std::string &name);
    // A Ghoul2 model's surfaces posed for a frame, in a mesh laid out as its
    // own (frame 0's), to draw once and destroy; 0 if it can't be made.
    gfx::MeshId poseGhoul2(const Loaded &m, int frame, int oldFrame, float backLerp);

    gfx::Renderer3D &_r;
    ShaderCache &_shaders;
    const SearchSet &_files;
    std::vector<Loaded> _models;
    std::unordered_map<std::string, int> _byPath; // normalized; -1 for failures
    std::unordered_map<std::string, Skin> _skins;
    std::unordered_map<std::string, std::unique_ptr<GlaSkeleton>> _skeletons; // normalized; nullptr for failures
};

} // namespace reluva::idtech3
