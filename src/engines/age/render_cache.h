// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The GPU side of models and textures, shared by the 3D pages: textures
// uploaded once by name, and a model's level of detail uploaded as one
// mesh with a draw per group.

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/pkg.h"
#include "engines/age/texture.h"

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
    // Animated ones (water: tune/<name>.movie, frames <name>-0001...)
    // have all their frames loaded; get() gives the one named, or the first.
    gfx::TextureId get(const std::string& name, bool& translucent);
    void clear();
    const std::vector<std::string>& missing() const { return missing_; }

    // Seconds into the animations, and the frame a texture shows then (the
    // texture itself if it isn't animated).
    void setTime(float seconds) { time_ = seconds; }
    gfx::TextureId frame(gfx::TextureId id) const;

private:
    struct Entry {
        gfx::TextureId id = 0;
        bool translucent = false;
    };
    struct Animation {
        std::vector<gfx::TextureId> frames;
        float rate = 15; // frames a second
        size_t first = 0;
    };
    gfx::TextureId create(const Tex& tex, bool& translucent);

    gfx::Renderer3D& renderer_;
    const SearchSet& files_;
    std::map<std::string, Entry> textures_; // by lower-case name
    std::map<gfx::TextureId, Animation> animations_;
    std::vector<gfx::TextureId> frameIds_; // every animation frame, to destroy
    std::vector<std::string> missing_;
    float time_ = 0;
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
    Vec3 low{}, high{}; // the bounds
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
    // Fog: none before fogStart metres, all fogColor from fogEnd (0: none).
    gfx::Vec3 fogColor{};
    float fogStart = 0, fogEnd = 0;
};

// The light's fog on a draw: mixed in towards the fog colour, or (for an
// additive draw: glows, flares) faded out.
void applyFog(gfx::DrawCall& call, const SceneLight& light, bool additive = false);

// Draws a model's opaque draws (`translucent` false) or its translucent
// and glowing ones (true), placed by `model`, in the light's fog.
// With `animate`, animated textures show their current frame.
void drawModelMesh(gfx::Renderer3D& renderer, const ModelMesh& mesh, const gfx::Mat4& model, const SceneLight& light,
                   bool translucent, const TextureCache* animate = nullptr);

// The planes of a view (left, right, bottom, top, near), to leave out
// what's outside it.
class ViewFrustum {
public:
    explicit ViewFrustum(const gfx::Mat4& viewProj);
    bool visible(const gfx::Vec3& center, float radius) const;

private:
    std::array<std::array<float, 4>, 5> planes_{};
};

// Models by name for a city, every level uploaded the first time it's
// asked for (for each paint job asked for), with no lit parts.
class ModelCache {
public:
    struct Model {
        bool ok = false;
        std::vector<ModelMesh> levels; // finest first
        float radius = 0;              // the finest level's
        Vec3 center{};
        uint32_t paintJobs = 1;
    };

    ModelCache(gfx::Renderer3D& renderer, TextureCache& textures, const SearchSet& files)
        : renderer_(renderer), textures_(textures), files_(files) {}
    ~ModelCache() { clear(); }
    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    // geometry/<name>.pkg
    const Model& get(const std::string& name, uint32_t paintJob = 0);
    // One of its lit parts (a traffic light's REDGLOWDAY_H, a car's
    // HLIGHT_L...), placed by its .mtx; empty if it hasn't one.
    const ModelMesh& part(const std::string& name, const std::string& part, uint32_t paintJob = 0);
    // A part's .mtx origin (where a car's HEADLIGHT0 square is); false
    // if it hasn't one.
    bool partOrigin(const std::string& name, const std::string& part, Vec3& out);
    void clear();

private:
    gfx::Renderer3D& renderer_;
    TextureCache& textures_;
    const SearchSet& files_;
    std::map<std::string, std::unique_ptr<Model>> models_;
    std::map<std::string, std::unique_ptr<ModelMesh>> parts_;
    std::map<std::string, std::pair<bool, Vec3>> origins_;
};

// Glows drawn as soft discs facing the camera (texture/lt_glow.tga), as
// the game's lamps shine: headlights, sirens.
class FlareDrawer {
public:
    explicit FlareDrawer(gfx::Renderer3D& renderer, TextureCache& textures);
    ~FlareDrawer();
    FlareDrawer(const FlareDrawer&) = delete;
    FlareDrawer& operator=(const FlareDrawer&) = delete;
    // `size` metres across, `color` 1 = full, added to what's behind.
    void draw(const Vec3& at, float size, const std::array<float, 3>& color, const Vec3& eye) const;

private:
    gfx::Renderer3D& renderer_;
    gfx::MeshId quad_ = 0;
    gfx::TextureId texture_ = 0;
};

} // namespace reluva::age
