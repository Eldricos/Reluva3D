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
#include "engines/stawayteam/game/particles.h"
#include "engines/stawayteam/game/state_io.h"
#include "engines/stawayteam/game/objectives.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/game/water_warp.h"
#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/game_hud.h"
#include "engines/stawayteam/ui/escape_menu.h"
#include "engines/stawayteam/ui/objectives_window.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
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
    float time = 0.0f;                   // seconds played
    std::vector<std::string> discover;   // items found, for the next briefing (CDiscoverInventoryNextBriefingAction)
    std::vector<std::string> discovered; // the items found before it, for CIsDiscoveredAction
};

/** A mission's progress saved, or read back (Reluva's saves): all but its objectives' file. */
void stateOf(StateIo &s, MissionProgress &progress);
/** An officer saved, or read back. */
void stateOf(StateIo &s, TeamMember &member);

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
    /** An instigator that's neither an officer nor an enemy. */
    static constexpr int kNobody = -1;
    /** Hit points for a character whose model gives none (inferred). */
    static constexpr float kDefaultHealth = 100.0f;
    /** Held to a task till told it's done. */
    static constexpr float kHeldForever = 1e9f;
    /**
     * What the player chose that leaves the screen: the main menu, or, as
     * the mission's over, the next mission or this one again.
     */
    enum class Choice { kNone, kExit, kContinue, kRestart, kConfigure, kSaveGame, kLoadGame };

    /**
     * `map` shown with `rules`' view, `team` on it at its spawn points,
     * walking by `wayPoints`; `data`, `rules` and `options` must outlive it.
     * One of the mission's later maps, `arriving`, has no one on it till
     * they're sent there.
     */
    MapScreen(const Archive &data, const TextObject &rules, GameMap map, GameOptions &options, uint64_t now,
              std::vector<TeamMember> team = {}, WayPoints wayPoints = {}, audio::Mixer *mixer = nullptr,
              bool arriving = false);
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
        size_t member = 0;                        // in the team
        int spawn = -1;                           // the part they spawned at, or -1
        int standIn = -1;                         // off the team, the part left on the map in their place, or -1
        std::vector<const TextObject *> addedAIs; // AIs given them, their stand-in's if they leave
        float x = 0.0f;                           // where they stand
        float y = 0.0f;
        int settled = 0;                           // updates since they were put there (Updates Since Relocate)
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
        float stunned = 0.0f;                   // seconds of their stun left, till they're up
        float stunTotal = 0.0f;                 // their stun's seconds, falling, lying and getting up
        float stunLength = 0.0f;                // its seconds lying down (Time To Stun)
        float enhanced = 0.0f;                  // seconds their weapons are enhanced for
        std::string discharge = "Shoot1";       // the sequence they're using an item with
        // What their tricorder last said, each officer's own (0x55d410),
        // and how many characters of it are typed out.
        std::string scanText;
        float scanTyped = 0.0f;
        // What they carry: each item's definition and how many uses are
        // left (-1 as many as wanted), as the inventory shows them; the
        // one they've chosen to use.
        struct Held {
            std::string name;
            int count = -1;
            bool stun = false;      // set to stun, one with the phaser's settings
            float energy = 100.0f;  // a charged one's charge, out of 100
            bool on = false;        // a charged one switched on
            float overload = -1.0f; // seconds till an overloaded one goes off; below 0 not overloaded
            float beep = 0.0f;      // seconds till its next beep
        };
        std::vector<Held> items;
        int item = -1;
        // What they've been sent to do with it, walking there first: throw
        // it at a place, or use it on an officer.
        enum class Task { kNone, kThrow, kUseOn, kPlace, kScan, kProcess, kItemProcess, kMeld };
        Task task = Task::kNone;
        float taskX = 0.0f;
        float taskY = 0.0f;
        int taskOfficer = -1;
        int taskPart = -1;                       // what a scan or process is of, or the enemy a meld's with
        const TextObject *taskProcess = nullptr; // the part's process they're going through
        size_t taskStep = 0;                     // its Process Activity they're on
        // The inventory bar's places, an item type's each: which of the
        // place's items it shows (its first, 0, as they come).
        std::vector<int> shown;

        bool present = true;  // on this map, not on another of the mission's
        float opacity = 1.0f; // how much of them shows: a quarter cloaked
        bool cloaked = false; // their cloaking band's on: they fade to a quarter
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
        int settled = 0;        // updates since it was put there (Updates Since Relocate)
        float angle = 0.0f;     // which way it faces, as Facing Angle
        std::string state;      // "Patrol", "Alert", "Attack"...
        int target = -1;        // the officer it's after
        int foe = -1;           // or the enemy it's after: one whose category its Valid Targets name
        float stateTime = 0.0f; // seconds in its state
        float unseen = 0.0f;    // seconds its target's been out of its sight
        bool walking = false;
        bool running = false;   // walking at its Run's pace and frames
        float travelled = 0.0f; // into its walk's cycle
        // Investigating a noise: where it was heard, the way there, and how
        // long it's looked about since getting there.
        float heardX = 0.0f;
        float heardY = 0.0f;
        // Whom it'll target, by their categories (its Targeting's Valid
        // Targets: Player, Team Member; none for a prisoner).
        std::string validTargets;
        std::vector<size_t> bodiesSeen;       // the dead bodies it's looked at, not to again
        float runAway = 0.0f;                 // seconds of running away left
        float runRetry = 0.0f;                // seconds till it looks for a spot again; there, below 0
        std::string onceSequence;             // played once and then held (Inactive's shutting down)
        float onceTime = 0.0f;                // seconds into it
        std::string beforeInactive;           // the state it goes back to, activated
        float doTimer = -1.0f;                // DoSomething's repeat timer: seconds till it next acts
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
        float dead = -1.0f;      // seconds since it fell; below 0 alive
        float stunned = 0.0f;    // seconds of its stun left, till it's up
        float stunTotal = 0.0f;  // its stun's seconds, falling, lying and getting up
        float stunLength = 0.0f; // its seconds lying down (Time To Stun)
        bool stunKept = false;   // its Collideable put aside while it lies, to put back
        bool stunLaid = false;   // lying, its Collideable put aside
        float held = 0.0f;       // seconds it's held to a task (a stasis field's, an EM pulse's), kHeldForever
        int decoy = -1;          // the decoy's part it's after, or -1
        float reaction = 0.0f;   // seconds before Got a Target takes a target again, hit by another
        float energy = 100.0f;   // its energy weapon's, out of 100
        // Its CShieldAI, if any: how far it's adapted to each damage type,
        // out of 1, and the type that last hit it (Last Damage Type).
        const TextObject *shield = nullptr;
        std::map<std::string, float> adapted;
        std::string lastDamage;

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
        int id = 0;           // its instigator is kThrownInstigator less this
        float gravity = 0.0f; // falling by so much a second each second
    };
    const std::vector<Thrown> &thrown() const { return _thrown; }
    /** A thrown thing as an action's instigator: kThrownInstigator less its id. */
    static constexpr int kThrownInstigator = -1000000;
    /** A part an enemy's shot may meet, a decoy: kPartTarget less its part. */
    static constexpr int kPartTarget = -2000000;
    /** A beam shot, from muzzle to target, up off the ground as drawn. */
    /**
     * A beam weapon's shot as it's drawn, the program's CPhaserShot line
     * (docs/engines/stawayteam/systems/maps.md#fighting): from its tail to
     * its head, both along the way from the muzzle.
     */
    struct Beam {
        float x0 = 0.0f; // its tail, on the map
        float y0 = 0.0f;
        float x1 = 0.0f; // its head
        float y1 = 0.0f;
        std::array<uint8_t, 3> outside{};
        std::array<uint8_t, 3> middle{};
        float sx = 0.0f; // the muzzle, and the way from it
        float sy = 0.0f;
        float ux = 0.0f;
        float uy = 0.0f;
        float lift = 0.0f; // how far up the muzzle is
        float tail = 0.0f; // how far out each end is
        float head = 0.0f;
        float reach = 0.0f; // the furthest its head goes
        float left = 0.0f;  // seconds till its tail goes after its head
        int id = 0;
        // Its Starting Flash (the weapon's Shot Flash) on its tail, and its
        // Ending Flash (Hit Flash) on its head once it meets someone, both
        // going round their frames; seconds it's been out.
        std::string shotFlash;
        std::string hitFlash;
        bool hit = false;
        float time = 0.0f;
    };
    const std::vector<Beam> &beams() const { return _beams; }
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
    /** Switches the shown officer's chosen item between kill and stun, and every chosen officer's that has both with it. */
    void toggleStun();
    /**
     * Sets the chosen officer's chosen item to overload, one that can be,
     * or back.
     */
    void toggleOverload();
    /** How long an overload lasts, in seconds: the panel's button gives 10 (0x5350d7). */
    static constexpr float kOverloadTime = 10.0f;
    /** Switches the chosen officer's chosen charged item (the cloaking band) on or off. */
    void toggleCharge();
    /** A particle system on a part: one of its AIs (a CGenericParticleSystemAI). */
    struct Emitter {
        size_t part;
        ParticleSystem system;
        const TextObject *ai = nullptr; // the system's
    };
    const std::vector<Emitter> &emitters() const { return _emitters; }
    /** The map as it is now: its parts' categories changed by the game among it. */
    const GameMap &map() const { return _map; }
    /** The enemy taken over by the Psychic Rack, or -1. */
    int melded() const { return _melded; }
    /** The officer holding it, or -1. */
    int melder() const { return _melder; }
    /** Whether the enemy taken over is the one chosen: the clicks are its orders. */
    bool meldChosen() const { return _meldChosen; }
    /** Chooses the enemy taken over, if there is one. */
    void chooseMelded();
    /** Lets the enemy taken over go (the panel's BREAK CONTROL). */
    void breakMeld();
    /** Sends the enemy taken over to (x, y), or after the enemy there. */
    void orderMelded(float x, float y);
    /** What the chosen officer's tricorder last said, and how much of it's typed out. */
    const std::string &scanText() const;
    /**
     * A hit on enemy `enemy` by `attacker` (an officer, or -1) with
     * `weapon`: stunning it with `stun`, else hurting it.
     */
    void hurtEnemy(size_t enemy, const ItemDefinition &weapon, int attacker, bool stun);
    /** Whether damage type `type` gets past every Borg shield (its Unavoidable damage). */
    bool unavoidable(const std::string &type) const;
    /** A shot at officer `officer` with `weapon`: stunning them with `stun`, else hurting them. */
    void hurtOfficer(size_t officer, const ItemDefinition &weapon, bool stun);
    /**
     * The difficulty, 1 easy, 2 normal, as the difficulty window has it:
     * what the officers' hurts are multiplied by (0x4a9ac1).
     */
    void setDifficulty(int level) { _difficulty = level; }
    /**
     * What a change to officer `officer`'s health is divided by: their
     * Stamina of 100, times 1.5, and a half (0x4a99a0, 0x4a9b72); 1 without one.
     */
    float staminaFactor(size_t officer) const;
    /**
     * Who a shot from (x0, y0) at (x1, y1) by `shooter` (an officer, or
     * kEnemyInstigator less an enemy) meets first: the first standing
     * character on its line, the one aimed at if no one's before them; an
     * officer's passes the other officers. An instigator: an officer, or
     * kEnemyInstigator less an enemy; kNobody for none.
     */
    int firstInLine(float x0, float y0, float x1, float y1, int shooter) const;
    /**
     * An energy weapon's bolt fired from (x, y) at (tx, ty) by `self` (an
     * officer, or kEnemyInstigator less an enemy): it flies, hitting the
     * first it meets as `attacker` with `weapon` (stunning with `stun`).
     */
    void fireBolt(const ItemDefinition &weapon, float x, float y, float tx, float ty, int self, int attacker,
                  bool stun);
    /** The bolts in flight. */
    size_t boltsFlying() const { return _bolts.size(); }
    /** A shot by `shooter` with `weapon` hitting `who` (as firstInLine gives). */
    void hitWith(int who, const ItemDefinition &weapon, int shooter, bool stun);
    // What officer `officer`'s weapons' damage and range are multiplied by:
    // more while they're enhanced; 1 for anyone else.
    float enhancement(int officer) const;
    /** The enemy standing at (x, y) on the map, alive, or -1. */
    int enemyAt(float x, float y) const;
    /** Whether nothing that stands full height is between two points on the map. */
    bool clearSight(float x0, float y0, float x1, float y1) const;
    /**
     * Whether part `part`'s outline (its model's Bounds Poly) at (x, y)
     * meets a part that collides: a wall, a closed door; not a character.
     *
     * @param struck where given, set to the part met.
     */
    bool blockedAt(size_t part, float x, float y, size_t *struck = nullptr) const;
    /** As blockedAt, for `shape` at (x, y), any part but `ignore`; a shape of under 3 points as a point. */
    bool shapeBlockedAt(const std::vector<std::pair<float, float>> &shape, size_t ignore, float x, float y,
                        size_t *struck = nullptr) const;
    // Where officer `officer` puts down a mine of `model` sent to (x, y)
    // (map_items.cpp).
    std::pair<float, float> placeFor(size_t officer, const std::string &model, float x, float y);
    // A thrown thing going from (x0, y0) to (x1, y1) on the ground: false if
    // a wall or a collideable polygon's in the way, its velocity turned off
    // it, and `struck`, where given, set to its part (map_combat.cpp).
    bool throwPasses(float x0, float y0, float x1, float y1, float &vx, float &vy, size_t *struck = nullptr) const;
    // A move by `instigator` stopped by part `part`: its collide triggers
    // act (map_triggers.cpp).
    void collided(size_t part, int instigator);

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
    /** The items found in missions before, for the maps to ask about. */
    void setDiscovered(std::vector<std::string> items) { _progress->discovered = std::move(items); }
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
    /**
     * The part clicked at (x, y) on the map: the one whose picture, as last
     * drawn, is on top there, or else a polygon with processes it's in.
     * @return its index, or -1 for none.
     */
    int partAt(float x, float y) const;

    /** Whether time's stopped (the pause button): orders can still be given. */
    bool paused() const { return _paused; }
    void setPaused(bool paused) { _paused = paused; }
    /**
     * An officer hit: with the options' Pause Game When Hit, time stops if
     * they're out of the view, and the first time the rules' First Hit
     * Action runs (map_combat.cpp).
     */
    void officerHit(size_t officer);
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

    /**
     * Whether an energy weapon's `energy` (0-100) holds a shot's Energy Use
     * (0x5d0980); other weapons are always ready.
     */
    static bool hasShot(const ItemDefinition &weapon, float energy);
    /** A shot's Energy Use spent from an energy weapon's `energy`, to none at least (0x5d094b). */
    static void spendShot(const ItemDefinition &weapon, float &energy);

    /** Where the officers, enemies and points of interest are, for the minimap. */
    std::vector<GameHud::Mark> minimapMarks() const;

    /** How many times its size the map's drawn (Zoom In, Out and Reset). */
    float zoom() const { return _zoom; }
    /** Zoom In (1) or Out (-1) held or let go; Reset (0) pressed. */
    void holdZoom(int how, bool held);
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

    /**
     * Its trees that what's saved points into, for `s` to write by: the
     * map's, its models' and death behaviours' read so far, its items'.
     * Each is named `prefix` and its own name.
     */
    void stateRoots(const std::string &prefix, StateIo &s);
    /** One of those trees by its own name, read if it isn't yet; nullptr if there's none. */
    const TextObject *stateRoot(const std::string &name);
    /**
     * The map as the game stands, saved to `s` or read back from it into
     * the map as it's first opened (Reluva's saves): its parts, the team on
     * it, the enemies, the triggers and all in flight. Its mission's
     * progress is the maps' together, and saved apart (stateOf). Read back,
     * the sounds playing aren't, and its windows are closed.
     */
    void state(StateIo &s);
    /** The view as it's drawn now, at its size, without the windows over it. */
    image::Image viewPicture();

private:
    // What a ground value draws: a texture, and how much of the next one
    // over it (0-255) (0x538d20, the 16-bit table).
    struct Blend {
        int texture = -1;
        int next = -1;
        int weight = 0;
    };

    void scroll(float seconds);
    // A frame of the zoom keys held (map_interface.cpp).
    void stepZoom(float frame);
    void setScale(float scale);
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
    // The nearest decoy of its Valid Targets it sees, by part, or -1; its
    // distance in `distance`.
    int closestDecoy(const Enemy &e, float &distance) const;
    // Another enemy of its Valid Targets seen, it's alerted and fights
    // them; true while it does (map_enemies.cpp).
    bool fightFoe(size_t self, float seconds);
    // A Death Behaviors file, kept; nullptr if it won't read.
    const TextObject *deathBehaviors(const std::string &type);
    // A part's Death Behaviors, as it's killed: its body's process, its
    // categories (Death Behaviors/<its Default Death Type>).
    void runDeathBehaviors(size_t part, int instigator);
    // Where an action names: a part ($trigger), an officer or enemy
    // ($instigator), or the place it's done at.
    bool placeNamed(const std::string &name, int instigator, int trigger, float &x, float &y) const;
    // An officer (0 up) or enemy (kEnemyInstigator down) by what an action
    // calls them, kNobody for neither.
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
    // Where instigator `who` stands: an officer, an enemy or a thrown
    // thing; false for none.
    bool placeOfInstigator(int who, float &x, float &y) const;
    // Its Got a Target state's choice, made at once (map_enemies.cpp).
    void gotATarget(Enemy &e);
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
    // Whether `o` is in `e`'s cone and in sight, at any distance, and not
    // hidden by a cloak: what attacking and pursuing look at (0x434e20,
    // 0x434e90).
    bool inView(const Enemy &e, const Officer &o) const;
    // The nearest officer it sees, or -1.
    int closestSeen(const Enemy &e) const;
    // An enemy's Max Distance where its AI gives none (0x5dd316).
    static constexpr float kSightDefault = 300.0f;
    // Whether a point's in its sight: its Max Distance and Vision Cone, and
    // a clear line.
    bool seesPoint(const Enemy &e, float x, float y) const;
    // Fighting (map_combat.cpp).
    void setupCombat();
    void updateCombat(float seconds);
    const ModelInfo &modelInfo(const std::string &model) const;
    const ItemDefinition *weaponOf(const std::vector<std::string> &items) const;
    // A shot from (x, y) with `weapon` at (tx, ty) by `instigator`: its
    // sound, its flashes, the triggers it sets off; how long the shooter's
    // `model` takes over shooting it.
    float shoot(const ItemDefinition &weapon, const std::string &model, float x, float y, float tx, float ty,
                int instigator);
    // An energy weapon's bolt flying from (x0, y0) to (x1, y1) setting off
    // the touching triggers Triggered By Projectiles it crosses.
    // Those in `crossed` (by part) are passed over, and those set off added.
    void shotThrough(float x0, float y0, float x1, float y1, int instigator, std::vector<size_t> *crossed = nullptr);
    // The items (map_items.cpp).
    void setupItems();
    void updateItems(float seconds);
    // The charged items and what they do (map_gadgets.cpp).
    void updateCharges(float seconds);
    // Whether an officer at (fromX, fromY) can use `item` at (x, y): within
    // `range` (0 or less: anywhere), and in sight of it if it Needs LOS.
    bool inItemRange(const ItemDefinition &item, float fromX, float fromY, float x, float y, float range) const;
    // The particle systems: one of a part's AIs taken on, turned on or
    // off by a message, run, and drawn.
    std::vector<Emitter> _emitters;
    void addEmitter(size_t part, const TextObject *ai);
    void turnEmitters(size_t part, bool on);
    void updateEmitters(float seconds);
    // A particle as it's drawn: its picture, by its origin at (x, y) on the
    // map, sorted by `key` among the models.
    struct ParticleDrawn {
        float key;
        const Animation::Picture *picture;
        int x;
        int y;
    };
    std::vector<ParticleDrawn> particlesDrawn();
    // A CCreateCustomEntityAction's entity made a part of the map at (x, y).
    size_t spawnEntity(const TextField &entity, float x, float y, int instigator);
    // The mind meld: whether officer `officer` can take enemy `enemy` over
    // now, taking it over, and each frame, its orders carried out and its
    // charge drained, and the meld ended when it's gone.
    bool canMeld(size_t officer, size_t enemy) const;
    void startMeld(size_t officer, size_t enemy);
    void updateMeld(float seconds);
    int _melded = -1;
    int _melder = -1;
    bool _meldChosen = false;
    std::string _meldCategory; // the enemy's own, given back as it's let go
    // An officer's task done once they're near enough: true when it's done.
    bool doTask(size_t officer);
    void explode(const Thrown &t);
    // Overloaded items counting down, beeping, and going off.
    void updateOverloads(float seconds);
    // Takes item `index` from officer `o`, choosing their weapon again.
    void removeItem(Officer &o, size_t index);
    void updateMines(float seconds);
    // The pointer over (x, y) on the map: a process there that runs on
    // hovering is started as it comes onto its part.
    void hoverAt(float x, float y);
    // The process of part `part` (its CProcessListAI's) for officer `o`'s
    // chosen item, or with none; nullptr if none.
    const TextObject *processFor(size_t part, const Officer &o) const;
    // Whether officer `o` can be given `process` on `part`, clicked at
    // (x, y): what it needs chosen and carried, and in reach.
    bool canTakeProcess(const Officer &o, const TextObject *process, size_t part, float x, float y) const;
    // A part's first process of the Process Type `type` (any, if empty), or
    // none (0x5474c0).
    const TextObject *processOfType(size_t part, const std::string &type) const;
    // Sends the chosen officers through the process of the part at (x, y),
    // as many as it allows; false if none is given it.
    bool startProcess(float x, float y);
    // An officer's process a step; true once it's through.
    bool runProcess(size_t officer, float seconds);
    bool stuns(const Officer &o) const;
    // The energy of officer `o`'s weapon, the item of theirs it is; nullptr if none.
    float *weaponEnergy(Officer &o);
    // A line of a script of Levels/; empty if there's none.
    std::string describe(const std::string &script, const std::string &line) const;
    const ItemDefinition *itemOf(const Officer &o) const;
    // The sequence a character's drawn with, and how far into it, for one
    // shooting, fallen or stunned (stunned seconds left of a stun of
    // stunTotal, lying stunLength of them); empty for none of those.
    struct Stun {
        float left = 0.0f;
        float total = 0.0f;
        float length = 0.0f;
    };
    std::string combatSequence(float shooting, float dead, const Stun &stun, int &frame, const std::string &model,
                               const std::string &discharge = "Shoot1");
    // How long a stun by `weapon` lies one down: its Stun Duration by the
    // difficulty (0x5dae70).
    float stunLength(const ItemDefinition &weapon) const;
    // A spot for enemy `e` to run away to from officer `o`: a route there,
    // or none.
    void runAwayFrom(Enemy &e, const Officer &o);
    // A stunned enemy's Collideable put back, if it was put aside.
    void standUp(Enemy &e);
    // A stun of `length` lying down, as a character of `model` falls,
    // lies and gets up: its seconds in all.
    float stunTotal(const std::string &model, float length);
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
    // Officer `officer` dead: whose rescue or protection fails, and the
    // items needed checked.
    void officerFell(size_t officer);
    // Officer `officer` on the team, joining or back: their rescue's done.
    void joinedTeam(size_t officer);
    // Someone dead: a need-item objective whose item no one living on the
    // team holds has failed.
    void checkNeedItems();
    void runSpawnActions();
    void endMission(bool completed, const std::string &script, const std::string &line);
    // The team's noises: ageing, and their rings shown (map_noise.cpp).
    void updateNoises(float seconds);
    void drawNoises(Surface &view, int left, int top) const;
    // The enemy whose sight is shown: what it can see, tinted (map_vision.cpp).
    void drawVision(Surface &view, int left, int top) const;
    void drawPause(Surface &screen, uint64_t now);
    // The Warped polygons in sight, their water warped to now (map_screen.cpp).
    void drawWater(Surface &view, int left, int top);
    const image::Image *water(const std::string &name);
    void walk(Officer &o, float seconds);
    // A line a pixel wide, `opacity` (0-1) of the colour over what's there.
    static void drawLine(Surface &view, float x0, float y0, float x1, float y1, const std::array<uint8_t, 3> &rgb,
                         float opacity);
    // The way from (x0, y0) to (x1, y1) along the waypoints, round the
    // characters standing in it if that's not much farther, the officer or
    // enemy given not among them; straight there without waypoints, or
    // with no way by them, unless `straight` is false: then none.
    std::vector<std::pair<float, float>> route(float x0, float y0, float x1, float y1, int officer, int enemy,
                                               bool straight = true) const;
    std::vector<std::pair<float, float>> route(const Officer &o, float x, float y) const {
        return route(o.x, o.y, x, y, int(&o - _officers.data()), -1);
    }
    std::vector<std::pair<float, float>> route(const Enemy &e, float x, float y, bool straight = true) const {
        return route(e.x, e.y, x, y, -1, int(&e - _enemies.data()), straight);
    }
    void drawOfficer(Surface &view, const Officer &o, int left, int top, uint64_t now);
    const Animation &animation(const std::string &model, const std::string &sequence);
    // The officer at (x, y) on the map, or -1.
    int officerAt(float x, float y) const;
    void drawPolygon(const std::function<uint8_t *(int)> &rowOf, int w, int h, int cellX, int cellY,
                     const MapPart &part);
    const image::Image &texture(const std::string &name);

    const Archive &_data;
    const TextObject &_rules;
    GameMap _map;
    GameOptions &_options; // its Show Pause When Hit Help turned off once shown
    int _viewX = 0;        // the view's place on the screen and its size (Screen Pos, Screen Size)
    int _viewY = 0;
    int _viewW = 0;
    int _viewH = 0;
    float _cameraX = 0.0f;
    float _cameraY = 0.0f;
    // The scrolling's velocity, map pixels a second, and the time not yet
    // taken in its steps.
    float _scrollVX = 0.0f;
    float _scrollVY = 0.0f;
    float _scrollTime = 0.0f;
    float _zoom = 1.0f;
    std::array<Blend, 256> _blends{};
    std::vector<image::Image> _groundTextures;
    std::map<std::string, image::Image> _textures; // the polygons', by name
    std::unordered_map<int, image::Image> _cells;
    std::vector<std::vector<size_t>> _cellPolygons; // the ground polygons over each cell
    std::map<std::string, Animation> _models;       // by model and sequence
    std::vector<size_t> _standing;                  // the models, back to front
    // A part's picture as last drawn, on the map: its origin at (x, y),
    // only between clipLeft and clipRight; back to front.
    struct Drawn {
        size_t part = 0;
        const Animation::Picture *picture = nullptr;
        int x = 0;
        int y = 0;
        int clipLeft = 0;
        int clipRight = 0;
    };
    std::vector<Drawn> _drawn;
    std::vector<size_t> _overlays; // the Trans polygons
    std::vector<size_t> _warped;   // the Warped polygons: water
    std::vector<size_t> _raised;   // the polygons not on the ground, sorted with the models
    // A Water Warp Effects file's warp, its texture and what it warped to.
    struct Water {
        bool loaded = false;
        WaterWarp warp;
        image::Image picture;
        double at = -1.0; // the game's time `picture` is for
    };
    std::map<std::string, Water> _waters;
    double _waterTime = 0.0; // seconds of the game's time, the water's clock
    // The scroll keys held, and the mouse.
    bool _left = false;
    bool _right = false;
    bool _up = false;
    bool _down = false;
    bool _zoomingIn = false;
    bool _zoomingOut = false;
    int _mouseX = -1;
    int _mouseY = -1;
    int _hovered = -1; // the part the pointer's over, for processes run on hovering
    uint64_t _last = 0;
    std::vector<TeamMember> _team;
    WayPoints _wayPoints;
    std::vector<Officer> _officers;
    int _selected = -1;
    // A box being dragged to choose officers: where it started, on the
    // screen, and whether it's moved far enough to be one.
    bool _pressed = false;
    bool _pressedMove = false; // the press was the Move button's, else the Action button's
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
        bool done = false; // triggered, and only once
        // A touching trigger's one in it, as an instigator, if it's occupied,
        // and the seconds till its next Repeat Action.
        bool occupied = false;
        int occupant = -1;
        float repeatIn = 0.0f;
        std::vector<bool> touchingEnemies; // a pass-through trigger's: each one who's about
        // An anything-touching trigger's: something in it, and its action
        // waiting out its delay, for whom.
        bool anything = false;
        const TextObject *pending = nullptr;
        float countdown = 0.0f;
        int instigator = -1;
        std::vector<int> thrownIn; // the thrown things in a pass-through trigger, by id
        std::vector<int> inside;   // an every-touching trigger's, as instigators
        float timer = 0.0f;        // a repeat timer's seconds till it next acts
    };
    // How far a shadow's cast left for each pixel of height; down, half
    // (0x491451).
    static constexpr float kShadowSlantX = 0.7014925f;
    // Each character's cells in the program's grid of the map's parts,
    // 64 pixels square, and the count when it last came into them: the
    // grid's searches find them by those (0x569f00, 0x569a20).
    struct GridPlace {
        std::array<int, 4> cells{}; // left, top, right, bottom
        uint64_t since = 0;
    };
    std::map<int, GridPlace> _gridPlaces; // by instigator
    uint64_t _gridCount = 0;
    // A model's sequences' bounds about where it stands, joined: those
    // lifted by its Rendering Height, those on the ground, the shadows;
    // empty, the left past the right.
    std::map<std::string, std::array<std::array<int, 4>, 3>> _modelBoxes;
    // Moves each officer and enemy about in the grid.
    void placeInGrid();
    std::array<int, 4> gridCells(const std::string &model, float x, float y, int lift);
    std::array<int, 4> triggerCells(const Trigger &t) const;
    // The grid's cells an area of the map covers, the map's edges holding
    // them.
    std::array<int, 4> areaCells(float x0, float y0, float x1, float y1) const;
    // Where the grid's search of `area` finds `who`: the row and column of
    // the first cell of both, then when they came into it; the least
    // first.
    std::array<uint64_t, 3> foundAt(int who, const std::array<int, 4> &area) const;
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
    std::string _warpEffect; // its Warp Effect's noise sequence
    bool _warpFadeIn = false;
    bool _warpIn = false; // Warp In: they fade in, rather than out
    // A picture under a warp of noise sequence `effect` gone `amount` (0-1)
    // of the way to it; the picture itself without one.
    const Animation::Picture *underWarp(const Animation::Picture *p, const std::string &effect, float amount);
    std::deque<Animation::Picture> _warpedPictures; // this frame's, kept while it's drawn
    // The info dialog.
    std::string _dialogText;
    std::string _dialogSpeaker; // their icon's
    image::Image _dialogIcon;
    audio::SoundHandle _dialogVoice = audio::kNoSound;
    float _dialogLeft = 0.0f;
    image::Image _dialogBackground;
    Font _dialogFont;
    // Fighting: the weapons, what the models say, what blocks sight and
    // throws (the footprints of the parts that collide, on the map) and the
    // shots' flashes showing.
    std::map<std::string, ItemDefinition> _items;
    mutable std::map<std::string, ModelInfo> _modelInfos; // read as they're first wanted
    mutable std::map<std::string, bool> _unavoidable;     // Damage Types/' Unavoidable damage, as wanted
    struct Wall {
        size_t part;
        bool full; // standing Full Height, so blocking sight
        bool half; // Half Height: cutting an enemy's drawn sight, as full height does
        std::vector<std::pair<float, float>> points;
        // Its bounds.
        float x0;
        float y0;
        float x1;
        float y1;
    };
    std::vector<Wall> _walls;
    std::vector<size_t> _bouncers; // the collideable polygons
    int _thrownIds = 0;
    struct Flash {
        std::string model;
        float x;
        float y;
        float time = 0.0f;             // seconds shown
        std::string sequence = "Idle"; // of its model, played once
        int follow = -1;               // the enemy it's drawn on, or -1 where it is
    };
    std::vector<Flash> _flashes;
    std::vector<Beam> _beams;
    int _beamIds = 0;
    int _pendingBeam = -1; // the beam the next bolt fired goes with
    // Bolts in flight: from (x, y) along (ux, uy), the head `head` on,
    // flying up to `range`; the triggers they've crossed, by part.
    struct Bolt {
        const ItemDefinition *weapon = nullptr;
        int self = kNobody;
        int attacker = kNobody;
        bool stun = false;
        float x = 0.0f;
        float y = 0.0f;
        float ux = 0.0f;
        float uy = 0.0f;
        float aimX = 0.0f;
        float aimY = 0.0f;
        float head = 0.0f;
        float range = 0.0f;
        std::vector<size_t> crossed;
        int beam = -1; // the beam drawn for it, by its id
    };
    std::vector<Bolt> _bolts;
    void updateBolts(float seconds);
    // firstInLine over the part of the line from `from` (0-1) on, the one
    // aimed at at (aimX, aimY).
    int firstOnStretch(float x0, float y0, float x1, float y1, int shooter, float from, float aimX, float aimY) const;
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
        float fading = -1.0f; // seconds since its death behaviours began fading it out; below 0 not dead
        std::string then = "Idle";
        const TextObject *done = nullptr; // run as it's played once through
        int doneBy = -1;                  // its instigator
    };
    std::vector<Deployed> _deployed;
    // How much of something put down shows as its death fades it out.
    float deathShown(size_t part) const;
    // A decoy put down hurt by `weapon`, shot by `shooter`.
    void hurtDecoy(size_t part, const ItemDefinition &weapon, int shooter);
    // A part's sequence played once through from its start, then `then`,
    // then `done` run (a door opening, then Open, then After Opened).
    void playSequence(size_t part, const std::string &sequence, const std::string &then, const TextObject *done,
                      int doneBy);
    // An AI given a part (CAddAIAction's).
    void giveAI(size_t part, const TextObject *ai);
    // Whether a part's model has a sequence of that name.
    bool hasSequence(size_t part, const std::string &sequence);
    // The doors (CDoorAI), each's locked and open.
    struct Door {
        bool locked = false;
        bool open = false;
        bool byPlayers = false; // its Triggered By Players and Enemies: theirs to walk through
        bool byEnemies = false;
    };
    std::map<size_t, Door> _doors;
    // Parts told to Stop Animation: the frame, counted from the clock's
    // start, their sequence stops on.
    std::map<size_t, int> _stoppedAt;
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
        std::string overlay; // its Warp Overlay Effect's sequence, drawn over it
        int overlayY = 0;    // and how far up it's raised: its Overlay YOffset
        int rotations = 4;   // the overlay's frames as so many directions: Num Rotations
        std::string effect;  // its Warp Effect's noise sequence
        bool fadeIn = false; // Fade In: held a quarter of the time, then fading
        bool warpIn = false; // Warp In: fading in rather than out
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
    std::vector<std::string> _itemTypes; // Inventory/Item Types, the bar's places in order
    // Seconds the chosen item's name's still shown over the bar, from when
    // it's chosen (0x423e52).
    static constexpr float kItemTitleTime = 1.5f;
    float _itemTitle = 0.0f;
    std::map<std::string, Animation> _cursors; // by name
    bool _overInterface = false;               // the mouse over the interface, not the map
    std::shared_ptr<MissionProgress> _progress;
    int _difficulty = 2;
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
