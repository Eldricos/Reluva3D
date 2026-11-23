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

private:
    void load();
    void draw();
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
    uint64_t _lastMillis = 0;
};

} // namespace reluva::storm3d
