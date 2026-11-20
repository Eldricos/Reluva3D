// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "engines/age/render/city_mesh.h"
#include "engines/age/formats/instances.h"
#include "engines/age/formats/psdl.h"
#include "engines/age/formats/pvs.h"
#include "engines/age/render/render_cache.h"

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

/** A city the menus offer: tune/<name>.cinfo's MapName and LocalizedName. */
struct CityInfo {
    std::string map;  // the files' stem: "sf"
    std::string name; // "San Francisco"
};

/** Every city whose .cinfo and .psdl are there. */
std::vector<CityInfo> findCities(const SearchSet &files);

/**
 * A city ready to draw: its streets and buildings (PSDL), the models its
 * .inst places, its sky dome and one of its 16 lightings. Shared by the
 * pages that show a city.
 */
class CityScene {
public:
    /** Nothing loaded yet; `renderer` and `files` must outlive the scene. */
    CityScene(gfx::Renderer3D &renderer, const SearchSet &files);
    /** Frees the city's meshes and textures from the renderer. */
    ~CityScene();
    CityScene(const CityScene &) = delete;
    CityScene &operator=(const CityScene &) = delete;

    /**
     * Loads city/<map>.psdl and what goes with it, in place of the city
     * loaded before, lit as the lighting set last.
     * @param map The files' stem, as CityInfo::map: "sf".
     * @return A failure only when the .psdl doesn't read; the rest that
     *         doesn't is left out and said in notes().
     */
    Error load(const std::string &map);

    /**
     * 0-15: time (morning, noon, evening, night) x 4 + weather (clear,
     * cloudy, foggy, rainy), as .lt00-.lt15 and the sky dome's paint jobs.
     * Taken modulo 16.
     */
    void setLighting(int index);
    /** The lighting set, 0-15 (see setLighting). */
    int lighting() const { return _lighting; }
    /** The lighting's weather and time: "foggy night". */
    std::string lightingName() const;
    /** The light the models are lit by, from the lighting's .lt file. */
    const SceneLight &light() const { return _light; }
    /** Lamps lit: in the evening and at night, and in fog and rain. */
    bool lampsLit() const { return _lighting / 4 >= 2 || _lighting % 4 >= 2; }
    /**
     * The warning lights the landmarks' xrefs mark (`sp_light_red_f`,
     * `sp_light_white_f`: a tiny untextured triangle each), as flares
     * while the lamps are lit (pkg.md).
     */
    void drawBeacons(const FlareDrawer &flares, const Vec3 &eye) const;
    /** How many warning lights the city has. */
    size_t beacons() const { return _beacons.size(); }
    /** Whether the lighting is one of the night's, 12-15. */
    bool night() const { return _lighting / 4 == 3; }

    /**
     * Draws the sky round `eye`, the city, then (if `models`) the placed
     * models in view; returns how many models were drawn. Between
     * Renderer3D::beginFrame and endFrame. With visibility on, only the
     * blocks the city's .cpvs says can be seen from the eye's block (and
     * the models in them) are drawn. `again`: a second view in the same
     * frame (a mirror), with the blocks chosen for the first.
     */
    size_t draw(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, bool models, bool again = false);
    /**
     * Brightens the city's opaque surfaces drawn by draw() with `lights`
     * (the nearest gfx::kMaxDynamicLights to `eye`): headlights on the road.
     */
    void drawDynamicLights(std::vector<gfx::DynamicLight> lights, const gfx::Vec3 &eye);
    /**
     * Stops drawing the city's triangles `drop` says to (what lies across
     * a drawbridge's gap); for good, until the next load().
     */
    void hide(const std::function<bool(const Vec3 &a, const Vec3 &b, const Vec3 &c)> &drop);

    /**
     * Culling by the city's visibility file (on by default, where there is
     * one).
     */
    void setVisibility(bool on) { _visibilityOn = on; }
    /**
     * The Graphics Options' textured sky; without it, the fog's colour
     * behind the city.
     */
    void setSkyShown(bool on) { _skyShown = on; }
    /**
     * Cloud Shadows (the Graphics Options): 0 none, 1 low (still), 2 high
     * (drifting with the wind by day). At night, the night tile's pools.
     */
    void setCloudShadows(int level) { _cloudShadows = level; }
    /** The Graphics Options' model distance: placed models further away (or past 900 m) aren't drawn. */
    void setModelDistance(float metres) { _modelDistance = metres; }
    /**
     * The Graphics Options' model detail: 1 the finest, a level chosen as
     * if the models were 1/detail further away (detail at least 0.1).
     */
    void setModelDetail(float detail) { _detail = detail; }
    /** Whether the drawing is culled: visibility on and the city's .cpvs read. */
    bool visibility() const { return _visibilityOn && !_pvs.empty(); }
    /** The block the eye was last in (0: none, or visibility off). */
    size_t viewBlock() const { return _viewBlock; }
    /** How many blocks were drawn from viewBlock(). */
    size_t blocksDrawn() const { return _blocksDrawn; }
    /**
     * The block `p` stands in (on or over its ground, up to 30 m above its
     * highest point), or 0.
     */
    size_t blockAt(const Vec3 &p) const;

    /** What was loaded and what wasn't, a line each, for the pages' info. */
    const std::vector<std::string> &notes() const { return _notes; }
    /** The city's streets and buildings as read, for the collision world. */
    const Psdl &psdl() const { return _psdl; }
    /** The city's triangles as drawn: the PSDL's, with its water and decals. */
    const CityMesh &mesh() const { return _mesh; }
    /** The models the city's .inst places. */
    const std::vector<Instance> &instances() const { return _instances; }
    /** How many models are placed: the instances' that loaded and the landmarks' xrefs. */
    size_t placedCount() const { return _placed.size(); }
    /** The textures the city is drawn with, for what else is drawn in it to share. */
    TextureCache &textures() { return *_textures; }
    /** The models the city is drawn with, for what else is drawn in it to share. */
    ModelCache &models() { return *_models; }

    // On the road nearest the city's middle, at its height, and the turn
    // (about Y) that faces along it.
    Vec3 start{};
    float startYaw = 0;

private:
    struct Batch {
        uint32_t firstIndex;
        uint32_t indexCount;
        gfx::TextureId texture;
        bool translucent;
    };
    struct Placed {
        const ModelCache::Model *model;
        gfx::Mat4 matrix;
        gfx::Vec3 center; // of its bounds, in the world
        float radius;
        uint16_t block;
    };
    struct BlockBounds {
        float minX;
        float minZ;
        float maxX;
        float maxZ;
        float minY;
        float maxY;
        std::vector<std::pair<float, float>> outline; // x, z
    };

    struct CityLights { // as SceneLight's defaults until a .lt is read
        gfx::Vec3 ambient{0.55f, 0.55f, 0.55f};
        std::array<gfx::Vec3, 3> direction{gfx::Vec3{-0.4f, 0.8f, -0.45f}}; // the key light, then the two fills
        std::array<gfx::Vec3, 3> color{gfx::Vec3{0.6f, 0.6f, 0.6f}};
    };
    struct Fog {
        gfx::Vec3 color;
        float start;
        float end;
    };

    void unload();
    // The city's vertex colours for the lighting, and its mesh again.
    void relightCity();
    // The index buffer again: the triangles of the blocks seen from
    // _viewBlock (all of them for 0).
    void rebuildIndices();

    gfx::Renderer3D &_renderer;
    const SearchSet &_files;
    std::unique_ptr<TextureCache> _textures;
    std::unique_ptr<ModelCache> _models;
    std::string _map;
    Psdl _psdl;
    CityMesh _mesh;
    std::vector<Instance> _instances;
    std::vector<std::string> _notes;
    gfx::MeshId _cityMesh = 0;
    std::vector<Batch> _batches;
    std::vector<Placed> _placed;
    struct Beacon {
        Vec3 at{};
        std::array<float, 3> color{};
    };
    std::vector<Beacon> _beacons;
    Pvs _pvs;
    std::vector<BlockBounds> _blockBounds;
    bool _visibilityOn = true;
    void drawCloudShadows();
    bool _skyShown = true;
    int _cloudShadows = 2;
    float _modelDistance = 900;
    float _detail = 1;
    size_t _viewBlock = 0;
    size_t _blocksDrawn = 0;
    Vec3 _detailFrom{1e9f, 0, 0};                    // where the roads' detail was last chosen from
    std::chrono::steady_clock::time_point _loadedAt; // the animations' clock starts
    CityLights _cityLights;
    std::vector<Fog> _fogs;
    std::string _sky;
    ModelMesh _skyMesh;
    int _lighting = 4;
    SceneLight _light;
};

/** A city instance's matrix (three axes and a position) as a 4x4. */
gfx::Mat4 instanceMatrix(const std::array<float, 12> &m);

} // namespace reluva::age
