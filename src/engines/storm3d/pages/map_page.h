// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/bzn.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"
#include "engines/storm3d/pages/sod_meshes.h"
#include "graphics/gfx3d.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The maps (the `.bzn` files in `bzn/`) in 3D: each object's model where the map places
 * it, over the map's background model.
 *   Left/Right: previous/next map   Home/End: first/last
 *   Up/Down: tilt   +/-: nearer/further   W/A/S/D: move over the map
 *   N: to the next object on a team (a ship or station), close up
 *   T: from straight above, as the map's picture (`bzn/<map>.bmp`) shows it
 *   P: to the next of the map's paths
 *   G: as the game looks on starting; then +/- and W/A/S/D move as the game's overview does
 *   H: the game's grid over the map on/off
 *   R: the players' starting units, as a skirmish makes them: Federation, Klingon, Romulan, Borg or none
 *   Space: turning on/off   V: the damage emitters (venting plasma and gas) on/off
 */
class MapPage : public Page {
public:
    /** Lists the maps; `files`, `sys` and `screen` must outlive it. */
    MapPage(System &sys, Surface &screen, const GameFiles &files);
    ~MapPage() override;

    const char *name() const override { return "Maps"; }
    size_t count() const override { return _names.size(); }
    bool uses3D() const override { return true; }
    void activate() override;
    void deactivate() override;
    void handleKey(uint32_t key) override;
    void update(uint64_t millis) override;

    /** A camera in the game's coordinates: where, which way, and how wide. */
    struct Camera {
        gfx::Vec3 eye{};
        gfx::Vec3 forward{0, 0, 1};
        gfx::Vec3 up{0, 1, 0};
        float fovY = 0.9f; // radians, top to bottom
    };
    /** Shows the map of that name (as listed: "bzn/fed1.bzn"), if there's one. */
    bool select(const std::string &name);
    /** Looks through this camera until a key moves the view. */
    void setCamera(const Camera &camera) {
        _fixed = camera;
        draw();
    }

    /**
     * The game's overview camera, looking at `target`: `height` above the
     * plane (kept within 10 to 700), behind the target by sqrt(200 x
     * height), facing (sin turn, cos turn) in x and z, as the game's
     * memory shows it. 450 and 0 are how it starts.
     */
    static Camera overviewOf(const gfx::Vec3 &target, float height = 450, float turn = 0);

    /**
     * How the game lights a map: its directional lights ("dlight"), each
     * its colour shining along its front axis, and its settings' colour as
     * the ambient light, all 1.75 times as bright; a map with none is lit
     * as the Models page is.
     */
    static SodMeshes::Lighting lightingOf(const BznMap &map);

    /** How an object's definition is drawn: its model, and the scale. */
    struct Look {
        std::string model; // empty: none (dont_load_SOD, or no definition)
        float scale = 1;
        // An asteroid field's (classLabel "asteroidfield"): how many
        // asteroids, and within what radius.
        int minAsteroids = 0;
        int maxAsteroids = 0;
        float fieldRadius = 0;
    };
    /**
     * The model a definition is drawn with: the definition's own name
     * unless it says `dont_load_SOD = 1`, scaled by its `scaleSOD`.
     */
    static Look lookOf(const std::string &odf, const SearchSet &files);
    /** An object's place: its transform, scaled about its position. */
    static gfx::Mat4 placeOf(const BznMap::Object &o, float scale);
    /**
     * An asteroid field's asteroids: models `master` to `master7`, as many
     * as its definition says, scattered within its radius about it, each
     * turned and scaled. The game's own choice isn't known: this one is
     * made from the object's number, so a map looks the same each time.
     */
    static std::vector<std::pair<std::string, gfx::Mat4>> asteroidsOf(const BznMap::Object &o, const Look &look);

    /** A unit a skirmish starts a player with: its definition, and where. */
    struct StartUnit {
        std::string odf;
        gfx::Vec3 at{};
    };
    /**
     * Where a skirmish makes each player's starting units, as the game
     * does (0x440e47): about the first point of the map's path
     * `Camera_<n>`, the player's, on a spiral, one turn each 100 units
     * further out, the first 100 out, a twelfth of a turn apart, in the
     * order of `units`. The game passes over a place where the unit
     * doesn't fit; this doesn't, so units may overlap where the map has
     * something in the way.
     */
    static std::vector<StartUnit> startUnitsOf(const BznMap &map, const std::vector<std::string> &units);
    /**
     * A race's starting units in a skirmish (its `standardUnits1` to `9`):
     * `race` its definition, "fed", "kling", "rom" or "borg".
     */
    static std::vector<std::string> standardUnitsOf(const std::string &race, const SearchSet &files);
    /**
     * A colour of `teamcolor.odf`, by its name there ("white", or a race's,
     * "federation"); none if it isn't there.
     */
    static std::optional<gfx::Vec3> teamColourOf(const std::string &name, const SearchSet &files);

private:
    void load();
    void draw();
    void makeGrid();
    void freeGrid();
    Camera orbit() const; // the camera the keys move
    void showMessage(const std::string &line);

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<std::string> _names;
    size_t _current = 0;
    size_t _focus = SIZE_MAX; // N's last object, into _map.objects
    size_t _path = SIZE_MAX;  // P's last path, into _map.paths
    gfx::Renderer3D *_renderer = nullptr;
    Surface _overlay;
    SodMeshes _meshes;
    BznMap _map;
    std::string _error;
    std::map<std::string, Look> _looks; // by lower-case definition
    // What's drawn: each object's model and place; the background's.
    std::vector<std::pair<const SodMeshes::Built *, gfx::Mat4>> _placed;
    const SodMeshes::Built *_background = nullptr;
    size_t _drawn = 0;      // objects drawn (an asteroid field's asteroids count as one)
    size_t _unmodelled = 0; // objects whose model isn't there
    size_t _effects = 0;    // objects whose model has nothing drawn: only emitters (not drawn yet)
    gfx::Vec3 _target{};
    float _distance = 1;
    float _yaw = 0.6f;
    float _pitch = 0.9f;
    bool _turning = false;
    std::optional<Camera> _fixed; // setCamera's, until a key
    // G's: the game's overview over _target, until a key other than its own.
    struct Overview {
        float height = 450;
        float turn = 0; // radians: 0 faces +z
    };
    std::optional<Overview> _overview;
    size_t _race = SIZE_MAX; // R's race, of the four; SIZE_MAX: none
    size_t _starting = 0;    // starting units drawn
    // The grid on the plane y = 0: one quad over the map's area, a line
    // each 100 units in its repeating texture.
    gfx::MeshId _gridMesh = 0;
    gfx::TextureId _gridTexture = 0;
    bool _showGrid = true;
    uint64_t _lastMillis = 0;
};

} // namespace reluva::storm3d
