// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/bzn.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"
#include "engines/storm3d/scene/map_scene.h"
#include "graphics/gfx3d.h"

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
 *   F: with starting units, the shroud over what the first player hasn't seen on/off
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

    /** Shows the map of that name (as listed: "bzn/fed1.bzn"), if there's one. */
    bool select(const std::string &name);
    /** Looks through this camera until a key moves the view. */
    void setCamera(const MapScene::Camera &camera) {
        _fixed = camera;
        draw();
    }

private:
    void load();
    void draw();
    MapScene::Camera orbit() const; // the camera the keys move
    void showMessage(const std::string &line);

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<std::string> _names;
    size_t _current = 0;
    size_t _focus = SIZE_MAX; // N's last object, into the map's objects
    size_t _path = SIZE_MAX;  // P's last path, into the map's paths
    gfx::Renderer3D *_renderer = nullptr;
    Surface _overlay;
    MapScene _scene;
    std::string _error;
    gfx::Vec3 _target{};
    float _distance = 1;
    float _yaw = 0.6f;
    float _pitch = 0.9f;
    bool _turning = false;
    std::optional<MapScene::Camera> _fixed; // setCamera's, until a key
    // G's: the game's overview over _target, until a key other than its own.
    struct Overview {
        float height = 450;
        float turn = 0; // radians: 0 faces +z
    };
    std::optional<Overview> _overview;
    size_t _race = SIZE_MAX; // R's race, of the four; SIZE_MAX: none
    bool _showGrid = true;
    bool _showShroud = true;
    uint64_t _lastMillis = 0;
};

} // namespace reluva::storm3d
