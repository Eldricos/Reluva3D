// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/play/hud.h"
#include "engines/storm3d/scene/map_scene.h"
#include "graphics/gfx3d.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
    /** A skirmish's player: where they start, their race's definition, their colour. */
    struct Player {
        int slot = 1;     // their map path Camera_<slot>
        std::string race; // "fed", "kling", "rom", "borg"
        gfx::Vec3 colour{};
        bool human = false;
    };
    /**
     * Starts a skirmish on `map`: each player's starting units as the game
     * makes them, in their colour; the camera over the human player's
     * Camera_<slot>; with `shroud`, the shroud over what their units don't see.
     * @return a failure if the map can't be read
     */
    Error startSkirmish(gfx::Renderer3D *renderer, const std::string &map, const std::vector<Player> &players,
                        bool shroud, const Hud::Resources &start, uint64_t millis);
    /**
     * The player's counters at the start: `start`'s dilithium and crew; the
     * officers their units cost (officerCost), of `start`'s most and what
     * their units add (officerGain), as the game's first frame of play has it.
     */
    static Hud::Resources resourcesOf(const Hud::Resources &start, const std::vector<std::string> &units,
                                      const SearchSet &files);
    /** The interface's layout's name for a race's definition ("fed" for "fed", "kli" for "kling"). */
    static std::string hudRaceOf(const std::string &race);
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
    Hud _hud;
    Surface _overlay;
    std::optional<Hud::Resources> _resources; // none: not known (a campaign's are its script's)
    std::set<int> _held;
    uint64_t _last = 0;
};

} // namespace reluva::storm3d
