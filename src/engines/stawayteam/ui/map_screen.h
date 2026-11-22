// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/game/objectives.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/game_hud.h"
#include "engines/stawayteam/ui/escape_menu.h"
#include "engines/stawayteam/ui/objectives_window.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace reluva::stawayteam {

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
    /** An instigator that's enemy `e`: kEnemyInstigator - e (officers are 0 up). */
    static constexpr int kEnemyInstigator = -1000;
    /** Hit points for a character whose model gives none (inferred). */
    static constexpr float kDefaultHealth = 100.0f;
    /** Held to a task till told it's done. */
    static constexpr float kHeldForever = 1e9f;
    /**
     * What the player chose that leaves the screen: the main menu, or, as
     * the mission's over, the next mission or this one again.
     */
    enum class Choice { kNone, kExit, kContinue, kRestart, kConfigure };

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
        float waited = 0.0f;                       // seconds they've waited for someone in their way
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
            bool stun = false;     // set to stun, one with the phaser's settings
            float energy = 100.0f; // a charged one's charge, out of 100
            bool on = false;       // a charged one switched on
        };
        std::vector<Held> items;
        int item = -1;
        // What they've been sent to do with it, walking there first: throw
        // it at a place, or use it on an officer.
        enum class Task { kNone, kThrow, kUseOn, kPlace, kScan, kProcess, kItemProcess };
        Task task = Task::kNone;
        float taskX = 0.0f;
        float taskY = 0.0f;
        int taskOfficer = -1;
        int taskPart = -1;                       // what a scan or process is of
        const TextObject *taskProcess = nullptr; // the part's process they're going through
        size_t taskStep = 0;                     // its Process Activity they're on
        // The inventory bar's places, an item type's each: which of the
        // place's items it shows (its first, 0, as they come).
        std::vector<int> shown;

        bool present = true;  // on this map, not on another of the mission's
        float opacity = 1.0f; // how much of them shows: a quarter cloaked
        bool cloaked = false; // their cloaking band's on: the enemies don't see them
        float held = 0.0f;    // seconds they're held still (a stasis field's), kHeldForever

        bool alive() const { return dead < 0.0f; }
        /** Fallen, stunned or elsewhere: they can't move, shoot, or be seen as a target. */
        bool down() const { return dead >= 0.0f || stunned > 0.0f || !present; }
    };
    const std::vector<Officer> &officers() const { return _officers; }
    /** The team: each officer's member, by their `member`. */
    const std::vector<TeamMember> &team() const { return _team; }
    /**
     * Another officer: `member`, here at (x, y) if `present`, else on
     * another of the mission's maps; their place among the officers.
     */
    size_t addMember(const TeamMember &member, bool present, float x, float y);
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
        float waited = 0.0f;    // seconds it's waited for someone in its way
        // Investigating a noise: where it was heard, the way there, and how
        // long it's looked about since getting there.
        float heardX = 0.0f;
        float heardY = 0.0f;
        // Whom it'll target, by their categories (its Targeting's Valid
        // Targets: Player, Team Member; none for a prisoner).
        std::string validTargets;
        std::vector<size_t> bodiesSeen;       // the dead bodies it's looked at, not to again
        float runAway = 0.0f;                 // seconds of running away left
        float recharge = 0.0f;                // seconds in Recharge, for its Time To Recharge
        const TextObject *patrolAI = nullptr; // a CSetPatrolAIAction's, in place of its own
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
        float held = 0.0f;    // seconds it's held to a task (a stasis field's, an EM pulse's), kHeldForever
        int decoy = -1;       // the decoy's part it's after, or -1

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
        // Each transition that's run: once, and for whom.
        std::map<const TextObject *, std::pair<bool, int>> transitions;
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
        // Its flight: how high it is, and how fast it's going, pixels a
        // second, along the ground and up.
        float z = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float vz = 0.0f;
        bool resting = false;
    };
    const std::vector<Thrown> &thrown() const { return _thrown; }
    /** A mine put down: arming, armed, or set off and about to go. */
    struct Mine {
        const ItemDefinition *item = nullptr;
        std::string model;
        float x = 0.0f;
        float y = 0.0f;
        float placed = 0.0f; // seconds since it was put down
        bool setOff = false;
        float fuse = 0.0f; // seconds till it goes, once set off
        int placedBy = -1;
    };
    const std::vector<Mine> &mines() const { return _mines; }
    /** Sets off every remote mine put down (the remote mine's DETONATE). */
    void detonate();
    /** Switches the chosen officer's chosen item between kill and stun, one that has both. */
    void toggleStun();
    /** Switches the chosen officer's chosen charged item (the cloaking band) on or off. */
    void toggleCharge();
    /** What the chosen officer's tricorder last said, and how much of it's typed out. */
    const std::string &scanText() const { return _scanText; }
    /**
     * A hit on enemy `enemy` by `attacker` (an officer, or -1) with
     * `weapon`: stunning it with `stun`, else hurting it.
     */
    void hurtEnemy(size_t enemy, const ItemDefinition &weapon, int attacker, bool stun);
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

    /** Whether time's stopped (the pause button): orders can still be given. */
    bool paused() const { return _paused; }
    void setPaused(bool paused) { _paused = paused; }
    /** Drops the chosen officers' orders: walking, attacking, using an item. */
    void cancelOrders();
    /** The vision button's state: 0 off, 1 choosing an enemy, 2 showing its sight. */
    int visionState() const { return _vision; }
    /** The enemy whose sight is shown; -1 for none. */
    int visionEnemy() const { return _visionEnemy; }
    /** The vision button pressed: off, choosing, showing, and round. */
    void pressVision();
    /** The next enemy in the view whose sight is shown (Cycle Vision Cones). */
    void cycleVision();
    /** Whether the escape menu's open. */
    bool escapeShown() const { return _escape != nullptr; }
    /** Whether the team's noises are shown (the sound button). */
    bool listening() const { return _listening; }
    void setListening(bool on) { _listening = on; }

    /**
     * The inventory bar's places for `o`, an item type's each, the last
     * type's first: the items of theirs of that type shown on the bar.
     */
    std::vector<std::vector<int>> itemPlaces(const Officer &o) const;
    /**
     * Inventory Next (`step` 1) or Prev (-1) for officer `officer`: along
     * the items of the chosen one's place, then on to the next place with
     * any (0x423da0, 0x423e90).
     */
    void stepItem(int officer, int step);

    /** Where the officers, enemies and points of interest are, for the minimap. */
    std::vector<GameHud::Mark> minimapMarks() const;

    /** How many times its size the map's drawn (Zoom In, Out and Reset). */
    float zoom() const { return _zoom; }
    /** Zoom In (1), Out (-1) or Reset (0). */
    void setZoom(int how);
    /** The map pixel under screen pixel (sx, sy). */
    float mapX(int sx) const { return _cameraX - float(shownW()) / 2.0f + float(sx - _viewX) / _zoom; }
    float mapY(int sy) const { return _cameraY - float(shownH()) / 2.0f + float(sy - _viewY) / _zoom; }

    /**
     * The mouse pointer for where it is: its name, of Interface/Game
     * Cursors, or "Pointer" for Interface/Cursors' (inferred).
     */
    std::string cursorName();
    /** The mouse pointer's picture at `now`; nullptr for none. */
    const Animation::Picture *cursor(uint64_t now);

private:
    // What a ground value draws: a texture, and how much of the next one
    // over it (0-255) (0x538d20, the 16-bit table).
    struct Blend {
        int texture = -1;
        int next = -1;
        int weight = 0;
    };

    void scroll(float seconds);
    // How much of the map the view shows, in its pixels.
    int shownW() const { return int(std::lround(float(_viewW) / _zoom)); }
    int shownH() const { return int(std::lround(float(_viewH) / _zoom)); }
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
    // The team's (map_mission.cpp): characters joining and leaving it, and
    // actors paused.
    bool runTeamAction(const TextObject *action, int instigator, int trigger);
    // A part killed: whose rescue or protection fails.
    void partFell(size_t part);
    // The roster, for characters joining by name.
    std::vector<TeamMember> _roster;
    // An enemy's state changed, its actions and transitions run.
    void setState(Enemy &e, const std::string &to);
    // A state's name as CBorgAI's field for it: "Run Away" is RunAway.
    static std::string stateKey(const std::string &name);
    // The gadgets' (map_gadgets.cpp): what's put down, given, used up and
    // held still.
    bool runGadgetAction(const TextObject *action, int instigator, int trigger);
    // A model put down at (x, y) as a part of the map, its Default
    // Entity's AIs running, its sequence from the first frame.
    size_t spawnPart(const std::string &model, const std::string &name, float x, float y, const std::string &sequence);
    // A model's whole object (Cache/Models/<model>.mdl16), kept; nullptr
    // if it won't read.
    const BinaryObject *modelObject(const std::string &model);
    void updateDeployed(float seconds);
    // An enemy after a decoy: it goes to it and shoots it (false if it has
    // none).
    bool fightDecoy(Enemy &e, float seconds);
    // A part's Death Behaviors, as it's killed: its body's process, its
    // categories (Death Behaviors/<its Default Death Type>).
    void runDeathBehaviors(size_t part, int instigator);
    // Where an action names: a part ($trigger), an officer or enemy
    // ($instigator), or the place it's done at.
    bool placeNamed(const std::string &name, int instigator, int trigger, float &x, float &y) const;
    // An officer (0 up) or enemy (kEnemyInstigator down) by what an action
    // calls them, kNobody for neither.
    static constexpr int kNobody = -1;
    int whoNamed(const std::string &name, int instigator, int trigger) const;
    // An item given to an officer, `count` of its uses; one used up.
    void giveItem(int officer, const std::string &item, int count);
    void useUpItem(int officer, const std::string &item);
    // An officer or enemy held to a task for `seconds`.
    void hold(int who, float seconds);
    // What a CConditionalAction tries.
    bool checkAction(const TextObject *attempt, int instigator, int trigger);
    // An officer's item's own Process Activity, as a part's (an audio
    // decoy put down, a stasis field set up).
    bool runItemProcess(size_t officer, float seconds);
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
    // Going to look, by its state's investigating AI (Investigate's, or
    // InvestigateDeadBody's).
    bool runInvestigate(Enemy &e, float seconds, const char *state = "Investigate",
                        const char *aiKey = "Investigate AI");
    // Turns `e` towards `angle` at `degreesPerSecond`; true there.
    bool turnTo(Enemy &e, float angle, float degreesPerSecond, float seconds);
    bool sees(const Enemy &e, const Officer &o) const;
    // Whether a point's in its sight: its Max Distance and Vision Cone, and
    // a clear line.
    bool seesPoint(const Enemy &e, float x, float y) const;
    // Fighting (map_combat.cpp).
    void setupCombat();
    void updateCombat(float seconds);
    const ModelInfo &modelInfo(const std::string &model);
    const ItemDefinition *weaponOf(const std::vector<std::string> &items) const;
    // A shot from (x, y) with `weapon` at (tx, ty): its sound, its flashes;
    // how long the shooter's `model` takes over shooting it.
    float shoot(const ItemDefinition &weapon, const std::string &model, float x, float y, float tx, float ty);
    // The items (map_items.cpp).
    void setupItems();
    void updateItems(float seconds);
    // The charged items and what they do (map_gadgets.cpp).
    void updateCharges(float seconds);
    // An officer's task done once they're near enough: true when it's done.
    bool doTask(size_t officer);
    void explode(const Thrown &t);
    void updateMines(float seconds);
    // The part with a model nearest (x, y), close enough to be what's
    // clicked; -1 for none.
    int partAt(float x, float y) const;
    // The process of part `part` (its CProcessListAI's) the chosen officer
    // can go through with their chosen item, or with none; nullptr if none.
    const TextObject *processFor(size_t part, const Officer &o) const;
    // Sends the chosen officer through the process of the part at (x, y);
    // false if there's none for them.
    bool startProcess(float x, float y);
    // An officer's process a step; true once it's through.
    bool runProcess(size_t officer, float seconds);
    bool stuns(const Officer &o) const;
    // A line of a script of Levels/; empty if there's none.
    std::string describe(const std::string &script, const std::string &line) const;
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
    // A part's AI that's a trigger watched from now on.
    void addTrigger(size_t part, const TextObject *ai);
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
    // The enemy whose sight is shown: what it can see, tinted (map_vision.cpp).
    void drawVision(Surface &view, int left, int top) const;
    void drawPause(Surface &screen, uint64_t now);
    void walk(Officer &o, float seconds);
    // A step of `step` pixels from (x, y) along (ux, uy), turned aside if
    // someone's in the way (the officer or enemy given isn't), unless it's
    // `toGo` from where it's going; false if there's no way past, the way
    // left as it was.
    bool stepAside(float x, float y, float &ux, float &uy, float step, float toGo, int officer, int enemy) const;
    void drawOfficer(Surface &view, const Officer &o, int left, int top, uint64_t now);
    const Animation &animation(const std::string &model, const std::string &sequence);
    // The officer at (x, y) on the map, or -1.
    int officerAt(float x, float y) const;
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
    float _zoom = 1.0f;
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
        bool done = false;                 // triggered, and only once
        std::vector<bool> touching;        // each officer's, for a touching trigger
        std::vector<bool> touchingEnemies; // each enemy's (a collide trigger's: each who's about)
        std::vector<float> enemyRepeat;
        // An anything-touching trigger's: something in it, and its action
        // waiting out its delay, for whom.
        bool anything = false;
        const TextObject *pending = nullptr;
        float countdown = 0.0f;
        int instigator = -1;
        std::vector<float> nextRepeat; // each officer's next Repeat Action
        float timer = 0.0f;            // a repeat timer's seconds till it next acts
    };
    std::vector<PartState> _partStates;
    std::vector<Enemy> _enemies;
    std::map<size_t, size_t> _enemyOfPart;
    std::vector<Trigger> _triggers;
    // Whether a point's in a touching trigger's oval or polygon.
    bool touches(const Trigger &t, float x, float y) const;
    // The anything-touching, team-touching, collide and pass-through
    // triggers (false for another kind).
    bool updateOtherTrigger(Trigger &t, float seconds);
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
    std::vector<Mine> _mines;
    // A model put down, as a part: its sequence and how far into it, the
    // frames whose actions have run, and its hit points if it has them.
    struct Deployed {
        size_t part = 0;
        std::string sequence;
        float time = 0.0f;
        int frames = 0;   // of its sequence passed
        bool once = true; // its sequence plays once, then `then`
        float health = 0.0f;
        std::string painSound;
        std::string then = "Idle";
        const TextObject *done = nullptr; // run as it's played once through
        int doneBy = -1;                  // its instigator
    };
    std::vector<Deployed> _deployed;
    // A part's sequence played once through from its start, then `then`,
    // then `done` run (a door opening, then Open, then After Opened).
    void playSequence(size_t part, const std::string &sequence, const std::string &then, const TextObject *done,
                      int doneBy);
    // Whether a part's model has a sequence of that name.
    bool hasSequence(size_t part, const std::string &sequence);
    // The doors (CDoorAI), each's locked and open.
    struct Door {
        bool locked = false;
        bool open = false;
    };
    std::map<size_t, Door> _doors;
    void setupDoors();
    // Opens or closes a door, for `instigator`; true if it's a door.
    bool moveDoor(size_t part, bool open, int instigator);
    std::map<std::string, std::unique_ptr<BinaryObject>> _modelObjects;
    std::map<std::string, std::unique_ptr<TextObject>> _deathBehaviors;
    // Parts beamed away: deleted once their warp's done.
    struct Warp {
        size_t part;
        float left; // seconds
        float time;
    };
    std::vector<Warp> _warping;
    // How much of a part's still there as it's beamed away (0-1).
    float warpShown(size_t part) const;
    // The place an action's done at when it's no part's: where an item's
    // used, where a grenade goes off.
    // The one a radius's action is on: $instigator in its Touching Action.
    bool _hasTouched = false;
    int _touched = kNobody;
    bool _hasHere = false;
    float _hereX = 0.0f;
    float _hereY = 0.0f;
    std::string _scanText;
    float _scanTyped = 0.0f;
    std::vector<std::string> _itemTypes; // Inventory/Item Types, the bar's places last first
    // Seconds the chosen item's name's still shown over the bar, from when
    // it's chosen (0x423e52).
    static constexpr float kItemTitleTime = 1.5f;
    float _itemTitle = 0.0f;
    std::map<std::string, Animation> _cursors; // by name
    bool _overInterface = false;               // the mouse over the interface, not the map
    std::shared_ptr<MissionProgress> _progress;
    bool _active = true;
    std::vector<Relocation> _relocations;
    int _switchTo = -1;
    std::unique_ptr<EndGameWindow> _endGame;
    std::unique_ptr<ObjectivesWindow> _objectivesWindow;
    std::vector<Noise> _noises; // the newest first
    bool _listening = false;
    bool _paused = false;
    int _vision = 0;
    int _visionEnemy = -1;
    std::unique_ptr<EscapeWindow> _escape;
    Animation _pauseIndicator;
    Font _pauseFont;
};

} // namespace reluva::stawayteam
