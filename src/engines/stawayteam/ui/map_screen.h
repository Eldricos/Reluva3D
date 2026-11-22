// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/game/objectives.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/game_hud.h"
#include "engines/stawayteam/ui/objectives_window.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * How a mission's going, the same on each of its maps: its objectives, its
 * Win Action, whether it's over, what's next, and its time
 * (docs/engines/stawayteam/systems/objectives.md).
 */
struct MissionProgress {
    MissionObjectives mission; // keeps its actions alive
    std::vector<Objective> objectives;
    bool won = false;
    bool over = false;
    bool completed = false;
    std::string nextMission;
    std::string overText;
    float time = 0.0f; // seconds played
};

/**
 * Officers sent to another of the mission's maps (`CStarTrekRelocateTeamAction`):
 * the map ("Mission 02/02 Security deck.zax"), the part they arrive at, and
 * whether it's then the map shown.
 */
struct Relocation {
    std::string map;
    std::string location;
    std::vector<int> officers;
    bool show = true;
};

/**
 * A map seen through the game's view: its ground, the polygons laid on it
 * and its models, the camera scrolled by the keys and the screen's edges
 * (docs/engines/stawayteam/systems/maps.md).
 */
class MapScreen {
public:
    /**
     * What the player chose that leaves the screen: the main menu, or, as
     * the mission's over, the next mission or this one again.
     */
    enum class Choice { kNone, kExit, kContinue, kRestart };

    /**
     * `map` shown with `rules`' view, `team` on it at its spawn points,
     * walking by `wayPoints`; `data`, `rules` and `options` must outlive it.
     * One of the mission's later maps, `arriving`, has no one on it till
     * they're sent there.
     */
    MapScreen(const Archive &data, const TextObject &rules, GameMap map, const GameOptions &options, uint64_t now,
              std::vector<TeamMember> team = {}, WayPoints wayPoints = {}, audio::Mixer *mixer = nullptr,
              bool arriving = false);
    ~MapScreen();

    /** What `ev` does to it at `now` (milliseconds). */
    Choice handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now);
    /** Moves the camera on to `now`, then draws the view. */
    void draw(Surface &screen, uint64_t now);

    /** The map's own song (its Map Song, of Music/); "!None.ogg" or empty for none. */
    const std::string &song() const { return _map.song; }
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
        float stunned = 0.0f;                   // seconds of their stun left
        std::string discharge = "Shoot1";       // the sequence they're using an item with
        // What they carry: each item's definition and how many uses are
        // left (-1 as many as wanted), as the inventory shows them; the
        // one they've chosen to use.
        struct Held {
            std::string name;
            int count = -1;
        };
        std::vector<Held> items;
        int item = -1;
        // What they've been sent to do with it, walking there first: throw
        // it at a place, or use it on an officer.
        enum class Task { kNone, kThrow, kUseOn };
        Task task = Task::kNone;
        float taskX = 0.0f;
        float taskY = 0.0f;
        int taskOfficer = -1;

        bool present = true; // on this map, not on another of the mission's

        bool alive() const { return dead < 0.0f; }
        /** Fallen, stunned or elsewhere: they can't move, shoot, or be seen as a target. */
        bool down() const { return dead >= 0.0f || stunned > 0.0f || !present; }
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
        bool running = false;   // walking at its Run's pace and frames
        float travelled = 0.0f; // into its walk's cycle
        // Investigating a noise: where it was heard, the way there, and how
        // long it's looked about since getting there.
        float heardX = 0.0f;
        float heardY = 0.0f;
        // Where it last saw its target, to go after them.
        float seenX = 0.0f;
        float seenY = 0.0f;
        bool retreated = false; // it's run to its Retreat AI's place, and holds it
        std::vector<std::pair<float, float>> route;
        float looked = -1.0f; // below 0 not there yet
        float health = 0.0f;
        float maxHealth = 0.0f;
        const ItemDefinition *weapon = nullptr;
        bool stuns = false; // its weapon's set to stun
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
    /**
     * Runs `action` (one of the map's), as a trigger would: `instigator`
     * the officer who set it off (or -1), `trigger` the part whose action
     * it is, `$Trigger` to it (or -1).
     */
    void runAction(const TextObject *action, int instigator = -1, int trigger = -1);

    /** Sends the officers chosen to shoot enemy `enemy`, walking into range first. */
    void attack(int enemy);
    /** Makes item `item` (of their items) the one officer `officer` uses. */
    void chooseItem(int officer, int item);
    /**
     * Uses the chosen officer's item at (x, y) on the map, as a click there
     * does: a grenade's thrown there, a hypospray used on the officer there;
     * false if their item isn't one used so.
     */
    bool useItem(float x, float y);
    /** A grenade thrown, flying or lying till it goes off. */
    struct Thrown {
        const ItemDefinition *item = nullptr;
        std::string model;
        float x = 0.0f;
        float y = 0.0f;
        float toX = 0.0f;
        float toY = 0.0f;
        float left = 0.0f; // seconds till it goes off
        int thrower = -1;
    };
    const std::vector<Thrown> &thrown() const { return _thrown; }
    /** The enemy standing at (x, y) on the map, alive, or -1. */
    int enemyAt(float x, float y) const;
    /** Whether nothing that stands full height is between two points on the map. */
    bool clearSight(float x0, float y0, float x1, float y1) const;

    /** A noise the team has made, heard by the enemies about it (0x5194e0). */
    struct Noise {
        float x = 0.0f;
        float y = 0.0f;
        float radius = 0.0f;
        float age = 0.0f;  // seconds it's heard for
        float left = 0.0f; // seconds of it left
    };
    const std::vector<Noise> &noises() const { return _noises; }
    /** Makes a noise at (x, y), heard `radius` away for `age` seconds. */
    void makeNoise(float x, float y, float radius, float age);
    /** The noise a footstep of officer `officer` makes, as Rules.txt and their Stealth make it. */
    float footstepNoise(const Officer &o, const Animation::Noise &step) const;
    /**
     * The mission's objectives, those it starts with first, and its Win
     * Action (docs/engines/stawayteam/systems/objectives.md).
     */
    void setObjectives(MissionObjectives objectives);
    const std::vector<Objective> &objectives() const { return _progress->objectives; }
    /** An objective's words, as the lists show them: its line, with its count. */
    std::string objectiveText(const Objective &o) const;
    /** The mission the triggers have said comes next ("Mission 02/Mission 02"); empty if none. */
    const std::string &nextMission() const { return _progress->nextMission; }
    /** Whether the mission's over, and how: the end game window's showing. */
    bool missionOver() const { return _endGame != nullptr; }
    bool missionCompleted() const { return _progress->completed; }
    /** The words the mission ended with. */
    const std::string &missionOverText() const { return _progress->overText; }
    /** The objectives as the lists show them: those shown, primary or not, with their words. */
    std::vector<ObjectiveLine> objectiveLines() const;
    /** How long the mission's been played, in seconds (the game's Mission Time). */
    float missionTime() const { return _progress->time; }
    /** Shares `progress` with the mission's other maps. */
    void shareProgress(std::shared_ptr<MissionProgress> progress) { _progress = std::move(progress); }
    const std::shared_ptr<MissionProgress> &progress() const { return _progress; }
    /** Whether it's the map shown: only that one counts the mission's time. */
    void setActive(bool active) { _active = active; }
    /** The officers its actions have sent to other maps since asked last. */
    std::vector<Relocation> takeRelocations() { return std::exchange(_relocations, {}); }
    /** The officer the player chose on the team bar who's on another map, once; -1 for none. */
    int takeSwitch() { return std::exchange(_switchTo, -1); }
    /** Officer `officer` arrives here, as `state` left them, at (x, y). */
    void arrive(size_t officer, const Officer &state, float x, float y);
    /** Officer `officer` leaves this map. */
    void depart(size_t officer);
    /** Where the part named `name` stands; false if there's none. */
    bool partPosition(const std::string &name, float &x, float &y) const;
    /** Whether the objectives window is open. */
    bool objectivesShown() const { return _objectivesWindow != nullptr; }

    /** Whether the team's noises are shown (the sound button). */
    bool listening() const { return _listening; }
    void setListening(bool on) { _listening = on; }

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
    // Walks `e` towards (x, y) at its walk's pace times `ratio`, or its
    // run's with `running`; true there.
    bool walkTo(Enemy &e, float x, float y, float ratio, float seconds, bool running = false);
    // Whether `e` hears one of the team's noises, and where.
    bool hears(const Enemy &e, float &x, float &y) const;
    // Sends `e` to investigate a noise at (x, y).
    void investigate(Enemy &e, float x, float y);
    // The enemies' messages and actions on them (map_enemies.cpp); true if
    // `action` was one.
    bool runEnemyAction(const TextObject *action, int instigator, int trigger);
    // Where part `part` stands: an enemy where it's got to.
    void placeOf(size_t part, float &x, float &y) const;
    // `e` goes after officer `officer`: attacking, its Attack's Start Action run.
    void attackOfficer(Enemy &e, int officer);
    // Walks `e` along its route; true once it's at its end.
    bool followRoute(Enemy &e, float seconds, bool running);
    // Whether an enemy's state moves it at a run (its Move Mode).
    bool runs(const Enemy &e, const char *state) const;
    // Sends `e` back to where its Retreat AI says, if it's hurt enough (its
    // Retreat At Percent).
    void maybeRetreat(Enemy &e);
    // Its Investigate state a step; true once it's done.
    bool runInvestigate(Enemy &e, float seconds);
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
    // A hit on enemy `enemy` by `attacker` (an officer, or -1): stunning it
    // with `stun`, else hurting it.
    void hurtEnemy(size_t enemy, const ItemDefinition &weapon, int attacker, bool stun);
    // The items (map_items.cpp).
    void setupItems();
    void updateItems(float seconds);
    // An officer's task done once they're near enough: true when it's done.
    bool doTask(size_t officer);
    void explode(const Thrown &t);
    const ItemDefinition *itemOf(const Officer &o) const;
    // A shot at officer `officer`: hurting them, or, from a weapon set to
    // `stun`, stunning them.
    void hurtOfficer(size_t officer, const ItemDefinition &weapon, bool stun);
    // The sequence a character's drawn with, and how far into it, for one
    // shooting or fallen; empty for neither.
    std::string combatSequence(float shooting, float dead, float stunned, int &frame, const std::string &model,
                               const std::string &discharge = "Shoot1");
    // The map's triggers (map_triggers.cpp).
    void setupTriggers();
    void updateTriggers(float seconds);
    // The parts named by `names`, a comma's list; "$Trigger" is `trigger`.
    std::vector<size_t> partsNamed(const std::string &names, int trigger = -1) const;
    void relay(const std::string &names, int instigator);
    bool shown(size_t part) const;
    void showDialog(const std::string &script, const std::string &line, const std::string &speaker);
    void drawDialog(Surface &screen);
    // The mission's objectives and its end (map_mission.cpp).
    bool runMissionAction(const TextObject *action, int instigator);
    void setObjectiveStatus(size_t objective, ObjectiveStatus status);
    void addObjective(Objective o, int instigator);
    void officerFell(size_t officer);
    void runSpawnActions();
    void endMission(bool completed, const std::string &script, const std::string &line);
    // The team's noises: ageing, and their rings shown (map_noise.cpp).
    void updateNoises(float seconds);
    void drawNoises(Surface &view, int left, int top) const;
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
        int trigger;
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
    std::vector<Thrown> _thrown;
    std::shared_ptr<MissionProgress> _progress;
    bool _active = true;
    std::vector<Relocation> _relocations;
    int _switchTo = -1;
    std::unique_ptr<EndGameWindow> _endGame;
    std::unique_ptr<ObjectivesWindow> _objectivesWindow;
    std::vector<Noise> _noises; // the newest first
    bool _listening = false;
};

} // namespace reluva::stawayteam
