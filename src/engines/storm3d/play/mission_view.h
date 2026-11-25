// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/play/hud.h"
#include "engines/storm3d/scene/map_scene.h"
#include "graphics/gfx3d.h"
#include "image/image.h"

#include <cstdint>
#include <array>
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
    /** A key pressed or let go, the mouse moved or clicked. */
    void handleEvent(const Event &ev);
    /** The unit selected, into the scene's units; -1 for none. */
    int selected() const { return _selected; }
    /**
     * The unit under a point of the screen, seen through `viewProj`: the
     * one whose middle is nearest it, of those whose box reaches it there;
     * -1 for none.
     */
    static int unitAt(const std::vector<MapScene::Unit> &units, const gfx::Mat4 &viewProj,
                      const MapScene::Camera &camera, int x, int y);
    /** Where the selection's ring lies: its middle, and half its side, from a unit's box. */
    static gfx::Vec3 ringMiddleOf(const MapScene::Unit &unit);
    static float ringHalfOf(const MapScene::Unit &unit);
    /** A unit's health bar's place on the screen, from its box there: left, top and width. */
    static std::array<int, 3> healthBarOf(const MapScene::Unit &unit, const gfx::Mat4 &viewProj);
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
    int _mouseX = 320;
    int _mouseY = 240;
    int _pressX = -1; // where the left button went down; -1 while it's up
    int _pressY = -1;
    int _selected = -1;
    int _hover = -1;
    gfx::MeshId _ringMesh = 0;
    gfx::TextureId _ringTexture = 0;
    MapScene::Camera camera() const;
    void makeRing();
    uint64_t _last = 0;
};

} // namespace reluva::storm3d
