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
        bool chosen = false;                       // among those the player has chosen
        float health = 0.0f;
        float maxHealth = 0.0f;
        const ItemDefinition *weapon = nullptr; // the one they shoot with, if any
        int attacking = -1;                     // the enemy they've been sent to shoot
        float reload = 0.0f;                    // seconds till their weapon shoots again
        float shooting = 0.0f;                  // seconds into their shot's sequence; 0 not shooting
        float dead = -1.0f;                     // seconds since they fell; below 0 alive

        bool alive() const { return dead < 0.0f; }
    };
    const std::vector<Officer> &officers() const { return _officers; }
    /** The officer chosen first: whose items the inventory shows; -1 for none. */
    int selected() const { return _selected; }
    /** Chooses officer `officer` alone, or, with `add`, adds or takes them away. */
    void choose(int officer, bool add = false);
    /** Chooses those standing in the box between two points on the map, or adds them with `add`. */
    void chooseIn(float x0, float y0, float x1, float y1, bool add = false);
    /** Chooses the whole team (Select Entire Team). */
    void chooseAll();
    /** Sends the officers chosen walking to (x, y) on the map, by the waypoints, side by side. */
    void order(float x, float y);
    /** How the officers chosen move: 0 walking, 1 running, 2 crouching. */
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
        bool collideable = true; // standing in the way, if it stands full height
    };
    const PartState &partState(size_t part) const { return _partStates[part]; }

    /** One of the map's characters with an enemy's AI (`CBorgAI`). */
    struct Enemy {
        size_t part = 0; // its part in the map
        const TextObject *ai = nullptr;
        float x = 0.0f; // where it stands
        float y = 0.0f;
        float angle = 0.0f;     // which way it faces, as Facing Angle
        std::string state;      // "Patrol", "Alert", "Attack"...
        int target = -1;        // the officer it's after
        float stateTime = 0.0f; // seconds in its state
        float unseen = 0.0f;    // seconds its target's been out of its sight
        bool walking = false;
        float travelled = 0.0f; // into its walk's cycle
        float health = 0.0f;
        float maxHealth = 0.0f;
        const ItemDefinition *weapon = nullptr;
        float reload = 0.0f;
        float shooting = 0.0f;
        float dead = -1.0f;   // seconds since it fell; below 0 alive
        float stunned = 0.0f; // seconds of its stun left

        bool alive() const { return dead < 0.0f; }
        bool down() const { return dead >= 0.0f || stunned > 0.0f; }
        // Each of its AIs' progress, by the AI.
        struct Progress {
            float time = 0.0f;
            size_t index = 0;
            std::vector<std::pair<float, float>> points;
            int step = 1;
            float from = 0.0f;
            float to = 0.0f;
            bool begun = false;
        };
        std::map<const TextObject *, Progress> progress;
    };
    const std::vector<Enemy> &enemies() const { return _enemies; }
    /** Runs `action` (one of the map's), as a trigger would, `instigator` the officer who set it off (or -1). */
    void runAction(const TextObject *action, int instigator = -1);

    /** Sends the officers chosen to shoot enemy `enemy`, walking into range first. */
    void attack(int enemy);
    /** The enemy standing at (x, y) on the map, alive, or -1. */
    int enemyAt(float x, float y) const;
    /** Whether nothing that stands full height is between two points on the map. */
    bool clearSight(float x0, float y0, float x1, float y1) const;

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
    // The enemies (map_enemies.cpp).
    void setupEnemies();
    void updateEnemies(float seconds);
    // Runs `ai` a step for `e`; true once it's done.
    bool runAI(Enemy &e, const TextObject *ai, float seconds);
    void forget(Enemy &e, const TextObject *ai);
    // Walks `e` towards (x, y) at its walk's pace times `ratio`; true there.
    bool walkTo(Enemy &e, float x, float y, float ratio, float seconds);
    // Turns `e` towards `angle` at `degreesPerSecond`; true there.
    bool turnTo(Enemy &e, float angle, float degreesPerSecond, float seconds);
    bool sees(const Enemy &e, const Officer &o) const;
    // Fighting (map_combat.cpp).
    void setupCombat();
    void updateCombat(float seconds);
    const ModelInfo &modelInfo(const std::string &model);
    const ItemDefinition *weaponOf(const std::vector<std::string> &items) const;
    // A shot from (x, y) with `weapon` at (tx, ty): its sound, its flashes;
    // how long the shooter's `model` takes over shooting it.
    float shoot(const ItemDefinition &weapon, const std::string &model, float x, float y, float tx, float ty);
    void hurtEnemy(size_t enemy, const ItemDefinition &weapon, int attacker);
    void hurtOfficer(size_t officer, const ItemDefinition &weapon);
    // The sequence a character's drawn with, and how far into it, for one
    // shooting or fallen; empty for neither.
    std::string combatSequence(float shooting, float dead, float stunned, int &frame, const std::string &model);
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
    // A box being dragged to choose officers: where it started, on the
    // screen, and whether it's moved far enough to be one.
    bool _pressed = false;
    bool _dragging = false;
    int _pressX = 0;
    int _pressY = 0;
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
    std::vector<Enemy> _enemies;
    std::map<size_t, size_t> _enemyOfPart;
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
    // Fighting: the weapons, what the models say, what blocks sight (the
    // footprints of the parts standing full height, on the map) and the
    // shots' flashes showing.
    std::map<std::string, ItemDefinition> _items;
    std::map<std::string, ModelInfo> _modelInfos;
    struct Wall {
        size_t part;
        std::vector<std::pair<float, float>> points;
        // Its bounds.
        float x0;
        float y0;
        float x1;
        float y1;
    };
    std::vector<Wall> _walls;
    struct Flash {
        std::string model;
        float x;
        float y;
        float time = 0.0f; // seconds shown
    };
    std::vector<Flash> _flashes;
};

} // namespace reluva::stawayteam
