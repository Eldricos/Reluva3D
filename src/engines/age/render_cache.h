// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The GPU side of models and textures, shared by the 3D pages: textures
// uploaded once by name, and a model's level of detail uploaded as one
// mesh with a draw per group.

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/pkg.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

class TextureCache {
public:
    TextureCache(gfx::Renderer3D& renderer, const SearchSet& files) : renderer_(renderer), files_(files) {}
    ~TextureCache() { clear(); }
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // A shader's texture by name (see loadTexture); 0 if it can't be
    // found, which is remembered in missing(). `translucent` says whether
    // any of its pixels aren't opaque.
    gfx::TextureId get(const std::string& name, bool& translucent);
    void clear();
    const std::vector<std::string>& missing() const { return missing_; }

private:
    struct Entry {
        gfx::TextureId id = 0;
        bool translucent = false;
    };
    gfx::Renderer3D& renderer_;
    const SearchSet& files_;
    std::map<std::string, Entry> textures_; // by lower-case name
    std::vector<std::string> missing_;
};

// One draw of a model: a group's triangles with its shader.
struct ModelDraw {
    uint32_t firstIndex = 0, indexCount = 0;
    gfx::TextureId texture = 0;
    std::array<float, 4> color{1, 1, 1, 1};
    bool translucent = false;
    bool lit = true;   // by the light (the mesh has normals), else as coloured
    bool glow = false; // added to what's behind (a light's halo)
};

struct ModelMesh {
    gfx::MeshId mesh = 0;
    std::vector<ModelDraw> draws;
    size_t vertices = 0, triangles = 0;
    Vec3 center{}; // of the bounds
    float radius = 0;
};

// The levels of detail a model has, finest first ("H", "M", "L", "VL").
std::vector<std::string> modelLevels(const Pkg& pkg);

// Uploads the meshes of one level of `pkg` (and those not named by level;
// the lit parts, as isLightPart says, only if `lights`), each part placed
// by its geometry/<model>_<part>.mtx unless `placeParts` is false (to
// place them when drawing, as a car's wheels). `pkgName` is the file it
// came from. `includePart`, if given, picks the parts.
Error buildModelMesh(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files,
                     const std::string& pkgName, const Pkg& pkg, const std::string& level, uint32_t paintJob,
                     bool lights, ModelMesh& out,
                     const std::function<bool(const std::string& part)>& includePart = nullptr,
                     bool placeParts = true);
void destroyModelMesh(gfx::Renderer3D& renderer, ModelMesh& mesh);

// The light a scene is drawn with.
struct SceneLight {
    gfx::Vec3 ambient{0.55f, 0.55f, 0.55f};
    gfx::Vec3 directed{0.6f, 0.6f, 0.6f};
    gfx::Vec3 direction{-0.4f, 0.8f, -0.45f}; // towards the light
};

// Draws a model's opaque draws (`translucent` false) or its translucent
// and glowing ones (true), placed by `model`.
void drawModelMesh(gfx::Renderer3D& renderer, const ModelMesh& mesh, const gfx::Mat4& model, const SceneLight& light,
                   bool translucent);

// Models by name for a city, every level uploaded the first time it's
// asked for, with the first paint job and no lit parts.
class ModelCache {
public:
    struct Model {
        bool ok = false;
        std::vector<ModelMesh> levels; // finest first
        float radius = 0;              // the finest level's
        Vec3 center{};
    };

    ModelCache(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files)
        : renderer_(renderer), textures_(textures), files_(files) {}
    ~ModelCache() { clear(); }
    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    // geometry/<name>.pkg
    const Model& get(const std::string& name);
    void clear();

private:
    gfx::Renderer3D& renderer_;
    TextureCache& textures_;
    const SearchSet& files_;
    std::map<std::string, std::unique_ptr<Model>> models_;
};

} // namespace reluva::age
