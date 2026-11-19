// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The GPU side of models and textures, shared by the 3D pages: textures
// uploaded once by name, and a model's level of detail uploaded as one
// mesh with a draw per group.

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "engines/age/formats/pkg.h"
#include "engines/age/formats/texture.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

/** Textures by name, read and uploaded the first time each is asked for. */
class TextureCache {
public:
    /** Nothing loaded yet; `renderer` and `files` must outlive the cache. */
    TextureCache(gfx::Renderer3D &renderer, const SearchSet &files) : _renderer(renderer), _files(files) {}
    ~TextureCache() { clear(); }
    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;

    /**
     * A shader's texture by name (see loadTexture); 0 if it can't be
     * found, which is remembered in missing(). `translucent` says whether
     * any of its pixels aren't opaque.
     * Animated ones (water: tune/<name>.movie, frames <name>-0001...)
     * have all their frames loaded; get() gives the one named, or the first.
     * A texture not found is 0, and listed as missing unless `optional`.
     */
    gfx::TextureId get(const std::string &name, bool &translucent, bool optional = false);
    /** Frees every texture from the renderer and forgets the missing ones. */
    void clear();
    /** A texture's size in pixels (after get()); false if it isn't loaded. */
    bool size(const std::string &name, int &width, int &height) const;
    /** The textures not found, but for those asked for as `optional`. */
    const std::vector<std::string> &missing() const { return _missing; }
    /**
     * Textures loaded from now on are halved this many times (the Texture
     * Quality), but for those flagged texflag::FullDetail and down to
     * 16 pixels at least.
     */
    void setHalvings(int n) { _halvings = n; }

    /** Seconds into the animations, for frame(). */
    void setTime(float seconds) { _time = seconds; }
    /** The frame a texture shows at the time set (the texture itself if it isn't animated). */
    gfx::TextureId frame(gfx::TextureId id) const;

private:
    struct Entry {
        gfx::TextureId id = 0;
        bool translucent = false;
        int width = 0;
        int height = 0;
    };
    struct Animation {
        std::vector<gfx::TextureId> frames;
        float rate = 15; // frames a second
        size_t first = 0;
    };
    gfx::TextureId create(const Tex &tex, bool &translucent);

    gfx::Renderer3D &_renderer;
    const SearchSet &_files;
    std::map<std::string, Entry> _textures; // by lower-case name
    std::map<gfx::TextureId, Animation> _animations;
    std::vector<gfx::TextureId> _frameIds; // every animation frame, to destroy
    std::vector<std::string> _missing;
    float _time = 0;
    int _halvings = 0;
};

/** One draw of a model: a group's triangles with its shader. */
struct ModelDraw {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    gfx::TextureId texture = 0;
    std::array<float, 4> color{1, 1, 1, 1}; // its shader's diffuse colour
    // and ambient and emissive ones
    Vec3 ambient{1, 1, 1};
    Vec3 emissive{};
    bool translucent = false;
    bool lit = true;         // by the light (the mesh has normals), else as coloured
    bool glow = false;       // added to what's behind (a light's halo)
    float shininess = 0;     // its shader's: a car's body paint has 0.5
    std::string textureName; // its shader's
    Vec3 center{};           // of its vertices' bounds
};

/** A model's level of detail, uploaded as one mesh with a draw per group. */
struct ModelMesh {
    gfx::MeshId mesh = 0;
    std::vector<ModelDraw> draws;
    size_t vertices = 0;
    size_t triangles = 0;
    Vec3 center{}; // of the bounds
    float radius = 0;
    // the bounds
    Vec3 low{};
    Vec3 high{};
};

/** The levels of detail a model has, finest first ("H", "M", "L", "VL"). */
std::vector<std::string> modelLevels(const Pkg &pkg);

/**
 * Uploads the meshes of one level of `pkg` (and those not named by level;
 * the lit parts, as isLightPart says, only if `lights`), each part placed
 * by its geometry/<model>_<part>.mtx unless `placeParts` is false (to
 * place them when drawing, as a car's wheels). `pkgName` is the file it
 * came from. `includePart`, if given, picks the parts.
 */
Error buildModelMesh(gfx::Renderer3D &renderer, TextureCache &textures, const SearchSet &files,
                     const std::string &pkgName, const Pkg &pkg, const std::string &level, uint32_t paintJob,
                     bool lights, ModelMesh &out,
                     const std::function<bool(const std::string &part)> &includePart = nullptr, bool placeParts = true);
/** Frees the mesh from the renderer and empties it. */
void destroyModelMesh(gfx::Renderer3D &renderer, ModelMesh &mesh);

/** The light a scene is drawn with. */
struct SceneLight {
    gfx::Vec3 ambient{0.55f, 0.55f, 0.55f};
    gfx::Vec3 directed{0.6f, 0.6f, 0.6f};
    gfx::Vec3 direction{-0.4f, 0.8f, -0.45f}; // towards the light
    // The lighting file's two fill lights (none by default).
    std::array<gfx::Vec3, 2> fill{};
    std::array<gfx::Vec3, 2> fillDirection{};
    // Fog: none before fogStart metres, all fogColor from fogEnd (0: none).
    gfx::Vec3 fogColor{};
    float fogStart = 0;
    float fogEnd = 0;
};

/**
 * The light's fog on a draw: mixed in towards the fog colour, or (for an
 * additive draw: glows, flares) faded out.
 */
void applyFog(gfx::DrawCall &call, const SceneLight &light, bool additive = false);

/**
 * Draws a model's opaque draws (`translucent` false) or its translucent
 * and glowing ones (true), placed by `model`, in the light's fog.
 * With `animate`, animated textures show their current frame.
 */
void drawModelMesh(gfx::Renderer3D &renderer, const ModelMesh &mesh, const gfx::Mat4 &model, const SceneLight &light,
                   bool translucent, const TextureCache *animate = nullptr);

/**
 * A car's reflections: the sphere map `map` (texture/refl_dc.tga in the
 * cities, refl_showroom.tga in the garage) added over its shiny draws (a
 * shader's shininess above 0), `strength` times their shininess, drawn
 * after them where they are.
 */
void drawModelReflection(gfx::Renderer3D &renderer, const ModelMesh &mesh, const gfx::Mat4 &model,
                         const SceneLight &light, gfx::TextureId map, float strength);

/**
 * The planes of a view (left, right, bottom, top, near), to leave out
 * what's outside it.
 */
class ViewFrustum {
public:
    /** The planes of the view `viewProj` draws. */
    explicit ViewFrustum(const gfx::Mat4 &viewProj);
    /** Whether any of the sphere may be in view (it isn't wholly outside a plane). */
    bool visible(const gfx::Vec3 &center, float radius) const;

private:
    std::array<std::array<float, 4>, 5> _planes{};
};

/**
 * Models by name for a city, every level uploaded the first time it's
 * asked for (for each paint job asked for), with no lit parts.
 */
class ModelCache {
public:
    /** A model as loaded: ok false if its .pkg didn't read or had no mesh. */
    struct Model {
        bool ok = false;
        std::vector<ModelMesh> levels; // finest first
        float radius = 0;              // the finest level's
        Vec3 center{};
        uint32_t paintJobs = 1;
        std::vector<PkgXref> xrefs; // models placed inside it
    };

    /** Nothing loaded yet; the arguments must outlive the cache. */
    ModelCache(gfx::Renderer3D &renderer, TextureCache &textures, const SearchSet &files)
        : _renderer(renderer), _textures(textures), _files(files) {}
    ~ModelCache() { clear(); }
    ModelCache(const ModelCache &) = delete;
    ModelCache &operator=(const ModelCache &) = delete;

    /**
     * A model, geometry/<name>.pkg, loaded the first time it's asked for; a
     * reference that holds until clear().
     * @param paintJob Taken modulo the model's paint jobs.
     */
    const Model &get(const std::string &name, uint32_t paintJob = 0);
    /**
     * One of its lit parts (a traffic light's REDGLOWDAY_H, a car's
     * HLIGHT_L...), placed by its .mtx; empty if it hasn't one.
     */
    const ModelMesh &part(const std::string &name, const std::string &part, uint32_t paintJob = 0);
    /**
     * A part's .mtx origin (where a car's HEADLIGHT0 square is); false
     * if it hasn't one.
     */
    bool partOrigin(const std::string &name, const std::string &part, Vec3 &out);
    /** Frees every model and part from the renderer. */
    void clear();

private:
    gfx::Renderer3D &_renderer;
    TextureCache &_textures;
    const SearchSet &_files;
    std::map<std::string, std::unique_ptr<Model>> _models;
    std::map<std::string, std::unique_ptr<ModelMesh>> _parts;
    std::map<std::string, std::pair<bool, Vec3>> _origins;
};

/**
 * Glows drawn as soft discs facing the camera (texture/lt_glow.tga), as
 * the game's lamps shine: headlights, sirens.
 */
class FlareDrawer {
public:
    /** Uploads the disc's square and loads its texture; `renderer` must outlive this. */
    explicit FlareDrawer(gfx::Renderer3D &renderer, TextureCache &textures);
    ~FlareDrawer();
    FlareDrawer(const FlareDrawer &) = delete;
    FlareDrawer &operator=(const FlareDrawer &) = delete;
    /** `size` metres across, `color` 1 = full, added to what's behind. */
    void draw(const Vec3 &at, float size, const std::array<float, 3> &color, const Vec3 &eye) const;

private:
    gfx::Renderer3D &_renderer;
    gfx::MeshId _quad = 0;
    gfx::TextureId _texture = 0;
};

} // namespace reluva::age
