// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/game_hud.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * A map seen through the game's view: its ground, the polygons laid on it
 * and its models, the camera scrolled by the keys and the screen's edges
 * (docs/engines/stawayteam/systems/maps.md).
 */
class MapScreen {
public:
    /** What the player chose that leaves the screen. */
    enum class Choice { kNone, kExit };

    /**
     * `map` shown with `rules`' view, `team` on it at its spawn points,
     * walking by `wayPoints`; `data`, `rules` and `options` must outlive it.
     */
    MapScreen(const Archive &data, const TextObject &rules, GameMap map, const GameOptions &options, uint64_t now,
              std::vector<TeamMember> team = {}, WayPoints wayPoints = {}, audio::Mixer *mixer = nullptr);
    ~MapScreen();

    /** What `ev` does to it at `now` (milliseconds). */
    Choice handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now);
    /** Moves the camera on to `now`, then draws the view. */
    void draw(Surface &screen, uint64_t now);

    /** The map pixel at the view's middle. */
    float cameraX() const { return _cameraX; }
    float cameraY() const { return _cameraY; }
    /** Puts the view's middle at (x, y), kept on the map. */
    void moveCamera(float x, float y);

    /** The ground's cell at (cx, cy), 64 pixels square, as drawn. */
    const image::Image &groundCell(int cx, int cy);

    /** An officer on the map. */
    struct Officer {
        size_t member = 0; // in the team
        float x = 0.0f;    // where they stand
        float y = 0.0f;
        int direction = 0;                         // of their model's 12, 0 facing left
        std::vector<std::pair<float, float>> path; // where they're walking to, the next first
        float travelled = 0.0f;                    // into their walk's cycle, in pixels
        int move = 0;                              // walking, running (1) or crouching (2)
    };
    const std::vector<Officer> &officers() const { return _officers; }
    int selected() const { return _selected; }
    /** Sends the officer chosen walking to (x, y) on the map, by the waypoints. */
    void order(float x, float y);
    /** How the officer chosen moves: 0 walking, 1 running, 2 crouching. */
    void setMove(int move);
    /** Moves everyone on to `now` (the camera, the officers walking). */
    void update(uint64_t now);

    /** Whether the map's camera scripts are still playing. */
    bool scripted() const { return !_scripts.empty(); }
    /** Whether the team has beamed in (or never had to). */
    bool teamShown() const { return _teamShown; }
    /** The words of the info dialog shown; empty if none is. */
    const std::string &dialogText() const { return _dialogText; }

    /** A part's state as the map's triggers change it. */
    struct PartState {
        bool active = true;
        bool visible = true;
        bool deleted = false;
    };
    const PartState &partState(size_t part) const { return _partStates[part]; }
    /** Runs `action` (one of the map's), as a trigger would, `instigator` the officer who set it off (or -1). */
    void runAction(const TextObject *action, int instigator = -1);

private:
    // What a ground value draws: a texture, and how much of the next one
    // over it (0-255) (0x538d20, the 16-bit table).
    struct Blend {
        int texture = -1;
        int next = -1;
        int weight = 0;
    };

    void scroll(float seconds);
    // The camera's scripts and their actions (0x4776a0).
    void playScripts(float seconds);
    // The map's triggers (map_triggers.cpp).
    void setupTriggers();
    void updateTriggers(float seconds);
    // The parts named by `names`, a comma's list; "$Instigator" the officer
    // who set it off.
    std::vector<size_t> partsNamed(const std::string &names) const;
    void relay(const std::string &names, int instigator);
    bool shown(size_t part) const;
    void showDialog(const std::string &script, const std::string &line, const std::string &speaker);
    void drawDialog(Surface &screen);
    void walk(Officer &o, float seconds);
    void drawOfficer(Surface &view, const Officer &o, int left, int top, uint64_t now);
    const Animation &animation(const std::string &model, const std::string &sequence);
    // The officer at (x, y) on the map, or -1.
    int officerAt(float x, float y);
    void drawPolygon(image::Image &cell, int cellX, int cellY, const MapPart &part);
    const image::Image &texture(const std::string &name);

    const Archive &_data;
    const TextObject &_rules;
    GameMap _map;
    const GameOptions &_options;
    int _viewX = 0; // the view's place on the screen and its size (Screen Pos, Screen Size)
    int _viewY = 0;
    int _viewW = 0;
    int _viewH = 0;
    float _cameraX = 0.0f;
    float _cameraY = 0.0f;
    std::array<Blend, 256> _blends{};
    std::vector<image::Image> _groundTextures;
    std::map<std::string, image::Image> _textures; // the polygons', by name
    std::unordered_map<int, image::Image> _cells;
    std::vector<std::vector<size_t>> _cellPolygons; // the ground polygons over each cell
    std::map<std::string, Animation> _models;       // by model and sequence
    std::vector<size_t> _standing;                  // the models, back to front
    std::vector<size_t> _overlays;                  // the Trans polygons
    // The scroll keys held, and the mouse.
    bool _left = false;
    bool _right = false;
    bool _up = false;
    bool _down = false;
    int _mouseX = -1;
    int _mouseY = -1;
    uint64_t _last = 0;
    std::vector<TeamMember> _team;
    WayPoints _wayPoints;
    std::vector<Officer> _officers;
    int _selected = -1;
    std::unique_ptr<GameHud> _hud;
    audio::Mixer *_mixer;
    // The camera's scripts: the one playing first, panning or waiting.
    std::vector<CameraScript> _scripts;
    bool _scriptBegun = false;
    bool _scriptWaiting = false;
    float _panStartX = 0.0f;
    float _panStartY = 0.0f;
    float _panDirX = 0.0f;
    float _panDirY = 0.0f;
    float _panDistance = 0.0f;
    float _panTime = 0.0f;
    float _waitLeft = 0.0f;
    struct Delayed {
        float left;
        const TextObject *action;
        int instigator;
    };
    // A part's trigger: its AI, and how it stands.
    struct Trigger {
        size_t part;
        const TextObject *ai;
        bool done = false;             // triggered, and only once
        std::vector<bool> touching;    // each officer's, for a touching trigger
        std::vector<float> nextRepeat; // each officer's next Repeat Action
        float timer = 0.0f;            // a repeat timer's seconds till it next acts
    };
    std::vector<PartState> _partStates;
    std::vector<Trigger> _triggers;
    std::map<const TextObject *, size_t> _series; // a series' next action
    std::map<std::string, std::vector<size_t>> _byName;
    unsigned _random = 1;
    std::vector<Delayed> _delayed;
    // The team beaming in: shown, and how long of the effect is left.
    bool _teamShown = true;
    float _warpLeft = 0.0f;
    float _warpTime = 0.0f;
    // The info dialog.
    std::string _dialogText;
    image::Image _dialogIcon;
    audio::SoundHandle _dialogVoice = audio::kNoSound;
    float _dialogLeft = 0.0f;
    image::Image _dialogBackground;
    Font _dialogFont;
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
