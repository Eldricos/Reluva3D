// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/scene/map_scene.h"
#include "graphics/gfx3d.h"

#include <cstdint>
#include <set>
#include <string>

namespace reluva::storm3d {

/**
 * A mission in play: its map drawn as the game draws it, seen through
 * the game's overview camera, which the player moves with the keys
 * `Input.map` gives it. Nothing moves on the map yet. See
 * docs/engines/storm3d/games/armada/systems/play.md.
 */
class MissionView {
public:
    /** The overview camera: its target on the plane, its height above it, and how it's turned. */
    struct Overview {
        gfx::Vec3 target{};
        float height = 450; // OVERVIEW_INIT_HEIGHT
        float turn = 0;     // radians; 0 faces +z
    };
    /** The camera's actions, as Input.map names them. */
    enum Action { kZoomIn, kZoomOut, kTurnLeft, kTurnRight, kScrollLeft, kScrollRight, kScrollUp, kScrollDown };
    /** The action a key does, if any: the keypad's + − / *, the arrows. */
    static int actionOf(uint32_t key);
    /** The overview after `actions` held for `seconds`. */
    static Overview moved(Overview from, const std::set<int> &actions, double seconds);
    /** Where the overview starts on a map: over its path `Camera_<slot>`'s first point; the area's middle without one. */
    static Overview startOf(const BznMap &map, int slot);

    explicit MissionView(const GameFiles &files) : _files(files), _scene(files) {}
    ~MissionView() { stop(); }
    MissionView(const MissionView &) = delete;
    MissionView &operator=(const MissionView &) = delete;

    /**
     * Starts a mission: `map` its map ("fed1.bzn"), drawn through
     * `renderer`, the camera over the player's `Camera_<slot>`; the
     * shroud over what the map's objects on `team` don't see (-1: none).
     * @return a failure if the map can't be read
     */
    Error start(gfx::Renderer3D *renderer, const std::string &map, int slot, int team, uint64_t millis);
    /** Lets go of the renderer and what it made. */
    void stop();
    /** A key pressed or let go. */
    void handleEvent(const Event &ev);
    /** Moves on to `millis` and draws. */
    void draw(uint64_t millis);
    const Overview &overview() const { return _overview; }

private:
    const GameFiles &_files;
    MapScene _scene;
    gfx::Renderer3D *_renderer = nullptr;
    Overview _overview;
    std::set<int> _held;
    uint64_t _last = 0;
};

} // namespace reluva::storm3d
