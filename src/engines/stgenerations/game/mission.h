// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <set>
#include <memory>
#include <vector>

#include "engines/stgenerations/game/random.h"
#include "engines/stgenerations/game/actors.h"
#include "engines/stgenerations/game/hud.h"
#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/game/movers.h"
#include "engines/stgenerations/game/projectiles.h"
#include "engines/stgenerations/game/render3d.h"
#include "engines/stgenerations/formats/script.h"
#include "engines/stgenerations/game/sound.h"
#include "engines/stgenerations/game/vm.h"
#include "engines/stgenerations/game/walker.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// Runs a 3D level's scripts (GAME.BIN) against the level (GAME.LV2): the
// role of the original's mission object (vtable 0x531768).
//
// At the start every script object enters state 0, in the order the
// original visits them, and the "mission started" broadcast follows. After
// that, scripts run when something happens:
//
// - **Timers** send event 1 to their object.
// - **The player** touching a wall, stepping onto a floor or entering a
//   billboard's area sends events to that thing's script object. The
//   object's id is the record's id plus the type bits: 0x4000 walls,
//   0xC000 floors, 0x2000 billboards, 0xA000 actors.
// - **Scripts** signal each other.
//
// The natives the scripts call are implemented here as far as they are
// understood. The rest balance the stack and are counted, so their
// effects are simply missing (the HUD and panels, inventory, combat,
// screen effects).
class Mission : public ScriptHost {
public:
    // Script events, as the original numbers them.
    enum Event {
        kEventTimer = 1,
        kEventSignal = 4,
        kEventSetValue = 5,
        kEventStart = 6,
        kEventBriefingDone = 7,
        kEventObjectivesDone = 8,
        kEventShieldEmpty = 48,
        kEventPlayerDamaged = 9,
        kEventPlayerHealed = 10,
        kEventTouch = 14,
        // Something hit the object: damage, power, the attacker's team.
        kEventHit = 16,
        kEventUntouch = 15,
        kEventActivate = 45,
        kEventEffectDone = 55,
        kEventHitReport = 59,
        // Broadcast when the player fires a weapon: its cost.
        kEventItemUsed = 50,
        kEventFloorLeft = 54,
        kEventFloorEntered = 53,
        // The water state changed (0x43D0C9): under it, wading or afloat, out of it.
        kEventUnderWater = 56,
        kEventInWater = 57,
        kEventOutOfWater = 58
    };

    // Type bits that turn a level record's id into its script object's id.
    enum ObjectType {
        kTypeBillboard = 0x2000,
        kTypeWall = 0x4000,
        kTypeActor = 0xA000,
        kTypeFlat = 0xC000,
        kTypeLogic = 0xE000,
        kTypeMask = 0xE000
    };

    // actors may be null for a level without them, and mixer null to run
    // silently; soundFiles opens the sound files the scripts play.
    Mission(LevelFile& level, const ScriptFile& script, LevelRenderer& renderer, Walker& walker, Actors* actors,
            Mixer* mixer = nullptr, const MissionSound::FileOpener& soundFiles = nullptr);
    ~Mission() override;

    // Start the level: every object to state 0, then the start broadcast.
    void start();

    // Advance the mission by ms milliseconds: run due timers and tell the
    // scripts what the player's last move ran into. Call after moving the
    // walker.
    void update(uint32_t ms);

    // Milliseconds since the mission started.
    uint32_t getTime() const { return _time; }

    // If a script turned the player since the last call, the new yaw.
    bool takePlayerYaw(double& yaw);
    // The camera's yaw, for scripts that ask which way the player faces.
    void setPlayerYaw(double yaw) { _currentYaw = yaw; }

    ScriptVM& getVM() { return *_vm; }
    MissionSound& getSound() { return *_sound; }

    // A subtitle that started since the last call, and how long to show it.
    bool takeSubtitle(std::string& text, uint32_t& durationMs) { return _sound->takeSubtitle(text, durationMs); }

    // Natives called that aren't implemented, by opcode, with call counts.
    const std::map<uint16_t, int>& getMissingNatives() const { return _missing; }

    int16_t getHealth() const { return _health; }
    // Hurt the player (game.vt34C): source object, damage, and a kind (a shot's team).
    void damagePlayer(int16_t source, int16_t damage, int16_t kind);

    // The bare-hand attack (SET_PUNCH): an overlay played over the view, a
    // frame at which it lands, what it does, and its sounds.
    struct Punch {
        // Overlay file name, from the script's resources; empty for none.
        std::string image;
        uint32_t msPerFrame = 100;
        int hitFrame = 0;
        int16_t damage = 0, power = 0;
        int swingSound = -1, swingVolume = -1;
        int hitSound = -1, hitVolume = -1;
    };
    const Punch& getPunch() const { return _punch; }
    // The punch starts: its swing sounds.
    void swing();
    // The punch lands on what the view shows at its centre (code 19, 0x4364B8).
    void punch(const LevelRenderer::Pick& pick);

    // An inventory item's behaviour (ATTACH_ITEM_BEHAVIOUR). Weapons and
    // the like hold a charge (0x449DCD); each use costs the power level,
    // which is also the damage of what they fire.
    struct Item {
        int16_t kind = 0, b = 0, c = 0;
        int32_t charge = 0, maxCharge = 0;
        int32_t level = 0, maxLevel = 0;
        // SET_ITEM_SETTING: how long the muzzle flashes, in ms.
        int32_t setting = 0;
    };
    // An item's behaviour, or null.
    const Item* getItem(uint16_t id) const {
        auto it = _items.find(id);
        return it == _items.end() ? nullptr : &it->second;
    }
    // Set the active item's power level (the [ and ] keys, the weapon display), within 1 and its maximum.
    void setItemLevel(int level);

    enum FireResult {
        kNoWeapon, // nothing to fire: the player punches instead
        kEmpty,    // not enough charge
        kFired
    };
    // Fire the active item if it is a weapon (0x43FDC4) from its muzzle
    // (ahead, up and to the left of the eye) at what the view shows at
    // (x, y): pick, the renderer's pick there, in a view w x h seen
    // through camera.
    FireResult fireWeapon(const Camera& camera, const LevelRenderer::Pick& pick, int x, int y, int w, int h,
                          double ahead, double up, double left);

    // The console: inventory, panels, body picture, map markers, screen effects.
    Hud& getHud() { return _hud; }
    // Use the item in an inventory slot, as clicking it does (event 45).
    void useItem(int slot);
    // Right click on an inventory slot (event 62).
    void describeItem(int slot);
    // An item dragged out of the inventory (event 63).
    void dropItem(int slot);
    // Whether an item is one the player holds and fires.
    bool isWeapon(uint16_t id) const;
    // An item's kind (ATTACH_ITEM_BEHAVIOUR), or 0.
    int getItemKind(uint16_t id) const {
        auto it = _items.find(id);
        return it == _items.end() ? 0 : it->second.kind;
    }
    // Where an object (the player for 0) is.
    bool locate(uint16_t id, double& x, double& y, double& z) const {
        Position position;
        if (!getPosition(id, position))
            return false;
        x = position.x;
        y = position.y;
        z = position.z;
        return true;
    }
    // An item's frame in the mission's INVENT.BMP, or -1.
    int getItemIcon(uint16_t id) const;
    // GAME_MODE 2 or 3 asked for the left panel off or on its body view since the last call; 0 if not.
    int takeLeftPanelRequest() {
        int request = _leftPanelRequest;
        _leftPanelRequest = 0;
        return request;
    }
    // Save or load the mission: the scripts, the world they changed, the
    // player and the actors. Sounds playing aren't kept; shots in flight
    // are dropped.
    void sync(Serializer& s);

    // END_MISSION (0xBF) or END_TRANSITION (0x92) has ended the location.
    bool isFinished() const { return _finished; }
    // Every objective was done (event 8): what the original's campaign
    // reads, as its controller's +0x10, to know the mission succeeded.
    bool isAccomplished() const { return _accomplished; }

    // What a transition (0x91, 0x92) shows: the mission's BRIEFING.BMP or
    // the game, and how bright, 0 (black) to 1. The original's blends may
    // run differently; this fades through black.
    struct TransitionView {
        bool active = false;
        // The briefing gives way to the game, the game to black (the
        // ending), or the game before a teleport to the game after it.
        enum Kind { kBriefing, kEnding, kTeleport } kind = kBriefing;
        // 0x43E13A: 1 speckles grow over the first picture, 2 it
        // crossfades to the second under a quarter's speckles, 3 they
        // clear from the second; 0 once over.
        int phase = 0;
        // In 1024ths: 0-256 in phases 1 and 3, 0-1024 in phase 2.
        int amount = 0;
        // SET_TRANSITION_STYLE (0x95): 1 pixels, 2 streaks.
        int style = 1;
        // The speckles' colour (SET_TRANSITION_COLOUR, 0x94).
        uint8_t r = 0, g = 0, b = 0;
    };
    TransitionView getTransitionView() const;
    bool isInTransition() const { return _transition.active; }

    // How the player moves (SET_PLAYER_MOTION, 0xB2), in the original's
    // units; the viewer scales its own speeds by them. A speed of 0
    // freezes the player for a cutscene.
    struct Motion {
        double speed = 1.5;
        int turnRate = 15, lookRate = 10;
        double accel = 1.0;
        int turnAccel = 10, lookAccel = 10;
        double jump = 13.9;
    };
    const Motion& getMotion() const { return _motion; }
    // SET_PLAYER_CONTROLS (0xAE): game+0x1E8, 0x5F by default. The bits
    // (tested in 0x43AD04): 1 walk and strafe, 2 turn, 4 look up and down,
    // 8 jump, 0x10 crouch, 0x20 the view bobs while walking (no script
    // sets it), 0x40 in low gravity, walking goes where the view points.
    enum Control {
        kControlWalk = 1,
        kControlTurn = 2,
        kControlLook = 4,
        kControlJump = 8,
        kControlCrouch = 0x10,
        kControlBob = 0x20,
        kControlSwim = 0x40
    };
    uint16_t getControls() const { return _controls; }
    bool isLowGravity() const { return _lowGravity; }
    // Zones to draw as well as the player's: above the water while afloat.
    uint64_t getExtraZones() const { return _waterZones; }
    // GET_ARRAY/SET_ARRAY's words, which outlive a mission (0x56F844):
    // verid1 leaves the player's health and phaser charge there.
    const std::vector<int16_t>& getCarriedArray() const { return _array; }
    void setCarriedArray(const std::vector<int16_t>& array) {
        if (array.size() == _array.size())
            _array = array;
    }
    // GET_DIFFICULTY (0xD5) answers this; the original's default is 1.
    void setDifficulty(int difficulty) { _difficulty = difficulty; }

    // SET_PLAYER_SPIN (0xAF): degrees a second the player is turned.
    int getSpin() const { return _spin; }
    // SHOW_VIEW_OVERLAY (0xB8): an .OVR over the view, and its frame; no image for none.
    struct ViewOverlay {
        std::string image;
        // How it is drawn (0x409DDC): 1 or 2, centred (they differ only for a picture wider than the view); else not at all.
        int mode = 0;
    };
    const ViewOverlay& getViewOverlay() const { return _viewOverlay; }
    // SET_MISSION_OUTCOME (0x97): codes left for the campaign.
    const std::vector<int32_t>& getOutcomes() const { return _outcomes; }
    // The worn shield (SET_WORN_ITEM) and the script gauge (SET_WORN_GAUGE), or 0.
    uint16_t getWornItem() const { return _wornItem; }
    uint16_t getGaugeItem() const { return _gaugeItem; }

    // The item in use (SET_ACTIVE_ITEM), or 0.
    uint16_t getActiveItem() const { return _activeItem; }

    // Fire a projectile of the given type (see Projectiles::fire), sounding it.
    void fire(int type, double x, double y, double z, double yaw, double pitch, int shooter);
    Projectiles& getProjectiles() { return *_projectiles; }
    Movers& getMovers() { return *_movers; }

    // ScriptHost
    void callNative(ScriptVM& vm, ScriptInstance& self, uint16_t op) override;
    void unhandledEvent(ScriptVM& vm, ScriptInstance& self, uint16_t event, const std::vector<int16_t>& args) override;

    // Send an event to the object with the given id, if there is one.
    int16_t send(uint16_t id, uint16_t event, const std::vector<int16_t>& args = {});
    // Send an event to every object that subscribed to broadcasts.
    void broadcast(uint16_t event, const std::vector<int16_t>& args = {});

private:
    struct Timer {
        uint32_t period;
        uint32_t due;
    };

    struct Position {
        double x, y, z;
        int16_t heading;
    };

    // Object order at the start, as the original's hash map yields it.
    std::vector<int> getStartOrder() const;

    void runTimers();
    // A transition (0x91-0x93) going on: its time, the teleport, its end.
    void updateTransition(uint32_t ms);
    void checkPlayerContacts();
    // The pass-through floor at the player's feet, or -1.
    int findPassThroughFloor() const;
    void setPool(uint16_t surfaceBelow, uint16_t surfaceAbove, uint16_t bottom);
    void updateWater();
    void setTimer(uint16_t id, uint32_t ms);

    // Pop a 16.16 dword (high word on top).
    static double pop32(ScriptVM& vm);
    // Pop a 32-bit integer (high word on top).
    static uint32_t popRaw32(ScriptVM& vm);
    static void push32(ScriptVM& vm, double value);
    static Position popPosition(ScriptVM& vm);
    static void pushPosition(ScriptVM& vm, const Position& position);

    bool getPosition(uint16_t id, Position& position) const;
    // SET_POSITION: actors move; billboards stay put (items settle onto the floor).
    void setPosition(uint16_t id, const Position& position);
    // DROP_AT: move an actor or billboard to a position, billboards onto the floor there.
    void dropAt(uint16_t id, const Position& position);
    void dropBillboard(LevelFile::Billboard& billboard);
    void setVisible(uint16_t id, bool visible);
    void setTexture(uint16_t id, uint16_t texture, uint16_t frames, uint16_t speed);
    void restoreTexture(uint16_t id);
    void setFullbright(uint16_t id, bool on);
    void setPlayerHeading(int16_t degrees);
    // Set an actor's mode from a native that pops the actor's id.
    void setActorMode(ScriptVM& vm, int mode, bool popTarget);
    void skipNative(ScriptVM& vm, uint16_t op);
    void runProjectiles(uint32_t ms);
    void runPush(uint32_t ms);
    void setTextureScroll(uint16_t id, int16_t u, int16_t v);
    // Walls that doors moved: collisions and shots follow them.
    void refreshGeometry(const std::vector<int>& walls);
    // LIFT_GO_TO: a lift's flats to one of its heights (1-based).
    void moveLift(uint16_t id, int level);
    void playSoundAt(int sound, double x, double y, double z);

    LevelFile& _level;
    const ScriptFile& _script;
    LevelRenderer& _renderer;
    Walker& _walker;
    std::unique_ptr<ScriptVM> _vm;
    RandomSource _random;

    uint32_t _time = 0;
    std::map<uint16_t, Timer> _timers;
    // Objects that receive broadcasts, newest first.
    std::vector<uint16_t> _listeners;
    // Screen effects still running: end time -> object to tell.
    std::vector<int16_t> _array;

    // Textures as loaded, for RESET_DEFAULT.
    std::vector<uint16_t> _wallTextures, _flatTextures, _billboardTextures;
    Actors* _actors;
    std::unique_ptr<MissionSound> _sound;
    std::unique_ptr<Projectiles> _projectiles;
    std::unique_ptr<Movers> _movers;

    // An item's behaviour (ATTACH_ITEM_BEHAVIOUR): its kind and two settings.
    std::map<uint16_t, Item> _items;
    Hud _hud;
    int _leftPanelRequest = 0;
    uint16_t _activeItem = 0;
    bool _finished = false;
    // game+0x94 and +0x90.
    uint32_t _objectives = 0xFFFFFFFF;
    uint32_t _objectivesDone = 0;
    bool _accomplished = false;
    struct Transition {
        bool active = false;
        bool ending = false;
        // TRANSITION_TELEPORT: the player moves at the darkest point.
        bool teleport = false, moved = false;
        double x = 0, y = 0, z = 0;
        int16_t heading = 0;
        uint32_t phase[3] = {};
        uint32_t elapsed = 0;
    };
    uint8_t _transitionColour[3] = {};
    uint32_t _environmentPeriod = 0, _environmentElapsed = 0;
    uint16_t _environmentSource = 0;
    uint16_t _wornItem = 0, _gaugeItem = 0;
    int _spin = 0;
    ViewOverlay _viewOverlay;
    std::vector<int32_t> _outcomes;
    Transition _transition;
    int _transitionSound = -1, _transitionVolume = 64, _transitionStyle = 1;
    Motion _motion;
    uint16_t _controls = 0x5F;
    // LOW_GRAVITY (0xB5) is on (game+0x148).
    bool _lowGravity = false;
    // Flat objects safe to land on (SET_FLAT_SAFE, 0xED).
    std::set<uint16_t> _safeFlats;
    // The save version that first holds _lowGravity and _safeFlats.
    static const uint32_t kLowGravitySaveVersion = 6;
    int _difficulty = 1;
    // SET_FALL_DAMAGE: the defaults are 1 and 32.0 (game+0x270, +0x26C).
    double _fallMultiplier = 1, _fallDivisor = 32.0;
    struct Push {
        bool active = false;
        double x = 0, y = 0, strength = 0;
        uint32_t ramp = 0, hold = 0, end = 0, elapsed = 0;
    };
    Push _push;
    // ENGAGE (0x73): the object told when the player walks 3 units off.
    uint16_t _engaged = 0;
    double _engagedX = 0, _engagedY = 0;
    // LIFT_SETUP (0x5E): by lift object, its flat group and heights (16.16).
    struct Lift {
        uint16_t flat = 0;
        int count = 0;
        int32_t heights[8] = {};
    };
    std::map<uint16_t, Lift> _lifts;
    Punch _punch;
    std::vector<MissionSound::Finished> _soundsFinished;

    // The original takes the maximum from a character record not yet
    // decoded; its health bar holds about 54 units, so 50 is assumed.
    int16_t _health = 50;
    int16_t _maxHealth = 50;
    int _touchedWall = -1;
    int _standingFlat = -1;
    int _passingFlat = -1;
    // SET_FLAT_GROUPS's pools: the surface's two sides and the bottom (flat object ids).
    struct Pool {
        uint16_t below = 0, above = 0, bottom = 0;
    };
    std::vector<Pool> _pools;
    // Per flat: 1 a surface from above, 2 from below, 3 a pool's bottom; its surface.
    std::vector<uint8_t> _flatMark;
    std::vector<uint16_t> _poolSurface;
    // 0 out of the water, 1 wading or afloat, 2 under it.
    int _waterState = 0;
    // Zones seen above the surface while afloat (0x43D765).
    uint64_t _waterZones = 0;
    // How far the eye goes either side of the surface before it counts (0x999A).
    static constexpr double kWaterBand = 0.6;
    int _billboard = -1;
    uint16_t _touchedActor = 0;
    std::vector<Actors::ScriptEvent> _actorEvents;
    bool _hasYaw = false;
    double _yaw = 0;
    double _currentYaw = 0;

    std::map<uint16_t, int> _missing;
};

} // namespace reluva::stgenerations

