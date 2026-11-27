// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/storm3d/formats/odf.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/play/combat.h"
#include "engines/storm3d/formats/tech_tree.h"
#include "engines/storm3d/play/computer_player.h"
#include "engines/storm3d/play/hud.h"
#include "engines/storm3d/play/movement.h"
#include "engines/storm3d/play/strategy.h"
#include "engines/storm3d/scene/map_scene.h"
#include "graphics/gfx3d.h"
#include "image/image.h"

#include <cstdint>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
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
        bool stances = false; // a Medium or Hard computer, which turns defensive or offensive; Easy doesn't
        int team = 0;         // players on a team win together
        std::string name;     // as the Game Setup screen says them: "Computer Medium"
        std::string side;     // "Federation"
    };
    /** What a player did, as the debriefing counts it. */
    struct Tally {
        int collected = 0; // dilithium brought in by freighters
        int spent = 0;
        int shipsCreated = 0; // built, not started with
        int stationsCreated = 0;
        int shipsLost = 0;
        int stationsLost = 0;
        int shipsDestroyed = 0; // of other players'
        int stationsDestroyed = 0;
    };
    /** How a skirmish has ended for the player: not yet, or they've won or lost. */
    enum class Ending { kNone, kWon, kLost };
    /**
     * Starts a skirmish on `map`: each player's starting units as the game
     * makes them, in their colour; the camera over the human player's
     * Camera_<slot>; `cover`'s shroud and fog over what their units don't
     * see (its slot is theirs). Every player builds by the tech tree that
     * techlvl.odf names for `techLevel` (`techfile<level>`), not the map's.
     * @return a failure if the map can't be read
     */
    Error startSkirmish(gfx::Renderer3D *renderer, const std::string &map, const std::vector<Player> &players,
                        MapScene::Cover cover, const Hud::Resources &start, uint64_t millis, int techLevel = 0);
    /**
     * The player's counters at the start: `start`'s dilithium and crew; the
     * officers their units cost (officerCost), of `start`'s most and what
     * their units add (officerGain), as the game's first frame of play has it.
     */
    static Hud::Resources resourcesOf(const Hud::Resources &start, const std::vector<std::string> &units,
                                      const SearchSet &files);
    /**
     * The crew after `seconds` more of play: each of the player's
     * starbases adds 3.5 a second, whole ones to the counter, the rest
     * kept in `carry`, up to RTS_CFG.h's cfgMaxCrew.
     */
    static int grownCrew(int crew, double &carry, int starbases, double seconds);
    /**
     * What the minimap shows: each unit in its player's colour, a station
     * (one that doesn't move) bigger, and where the camera's view falls on
     * the plane at `plane`'s height.
     */
    static Hud::Minimap minimapOf(const BznMap &map, const std::vector<MapScene::Unit> &units,
                                  const std::vector<bool> &moves, const MapScene::Camera &camera, float plane);
    /** The map's objects that have a mark on the minimap: their definitions' mapIcon. */
    static std::vector<Hud::Minimap::Icon> mapIconsOf(const BznMap &map, const Odf::Loader &loader);
    /** The interface's layout's name for a race's definition ("fed" for "fed", "kli" for "kling"). */
    static std::string hudRaceOf(const std::string &race);
    /**
     * What the display shows of a unit, from its definition `odf` ("fbase")
     * as `loader` reads it: `hud` says which weapons are special (those with
     * a button sprite b_<weapon>), `pick` which of its possible names it has.
     */
    static Hud::Unit displayOf(const std::string &odf, const Odf::Loader &loader, const Hud &hud, size_t pick);
    /** Lets go of the renderer and what it made. */
    void stop();
    /** A key pressed or let go, the mouse moved or clicked. */
    void handleEvent(const Event &ev);
    /** The units selected, into the scene's units. */
    const std::vector<int> &selection() const { return _selection; }
    /**
     * The unit under a point of the screen, seen through `viewProj`: the
     * one whose middle is nearest it, of those whose box reaches it there;
     * -1 for none.
     */
    static int unitAt(const std::vector<MapScene::Unit> &units, const gfx::Mat4 &viewProj,
                      const MapScene::Camera &camera, int x, int y);
    /**
     * The units a box dragged from (x0, y0) to (x1, y1) selects: of
     * `slot`'s (-1: anyone's), those whose middle falls in it on the
     * screen; only those that move (`moves`, by unit), if any do.
     */
    static std::vector<int> boxed(const std::vector<MapScene::Unit> &units, const std::vector<bool> &moves,
                                  const gfx::Mat4 &viewProj, int slot, int x0, int y0, int x1, int y1);
    /**
     * A group's orders: each button any of them has, lit if any of them
     * can; build dark, as one unit builds at a time; their special
     * weapons once each.
     */
    static Hud::Unit ordersOf(const std::vector<Hud::Unit> &units);
    /**
     * Sends the units selected that move to a point on the plane: each to
     * the point plus its place from the group's middle, so a group keeps
     * its shape; round a unit that doesn't move (`units`, as `ships`) in
     * its way.
     */
    static void sendTo(std::vector<Movement::Ship> &ships, const std::vector<MapScene::Unit> &units,
                       const std::vector<int> &selection, const std::vector<bool> &moves, const gfx::Vec3 &point,
                       const std::vector<Movement::Obstacle> &fields = {});
    /**
     * The cinematic view's close-up of a unit: from above, behind and to
     * its left, looking down at its middle, as the original's steady shot
     * of a construction ship.
     */
    static MapScene::Camera closeUpOf(const MapScene::Unit &unit);
    /**
     * What a unit can build, by its definition's buildItem0, buildItem1
     * and on: the ships and stations, not the officers' upgrades (class
     * "upgrade"), which have a button of their own.
     */
    static std::vector<std::string> buildListOf(const std::string &odf, const Odf::Loader &loader);
    /**
     * The control panel while a unit's build menu is open: its items, a
     * button each from the first place on, its icon b_<item> in the build
     * border, dark where the player can't yet: where the map's tech tree
     * says the player hasn't all it needs (without one, a freighter
     * without a mining station, and a guess for the construction ship's
     * last four); the back button in the last place.
     */
    static Hud::Unit buildMenuOf(const std::vector<std::string> &items, const Odf::Loader &loader,
                                 const std::vector<std::string> &owned, const TechTree &tree = TechTree{});
    /** An item a unit builds: what, how long it takes, and what it costs. */
    struct Order {
        std::string item;
        double time = 0;    // buildTime, seconds
        double elapsed = 0; // seconds built so far
        int dilithium = 0;
        int crew = 0;
        int officers = 0;
    };
    /** An item's order, from its definition's buildTime and costs. */
    static Order orderOf(const std::string &item, const Odf::Loader &loader);
    /**
     * Pays for an order out of `resources`, as the original took it at
     * once: its dilithium and crew, and the officers it needs on the
     * count; false, and nothing taken, if there isn't enough.
     */
    static bool pay(Hud::Resources &resources, const Order &order);
    /** Where a ship built by a unit at `builder` flies to: out to its lower left, above the plane. */
    static gfx::Vec3 launchPointOf(const gfx::Vec3 &builder);
    /** A station a construction ship was sent to build: its builder, where, what; whether it's paid and begun. */
    struct Site {
        size_t builder = 0;
        gfx::Vec3 at{};
        Order order;
        bool started = false;
    };
    /** Where a builder waits while it builds at `site`: `distance` short of it, on the side of `from`. */
    static gfx::Vec3 standOffOf(const gfx::Vec3 &site, const gfx::Vec3 &from, float distance);
    /** A freighter's round: to the moon, mining, back to its station, unloading. */
    struct Freighter {
        enum class State { kToMoon, kMining, kToStation, kUnloading };
        size_t unit = 0;
        size_t station = 0;
        State state = State::kToMoon;
        float cargo = 0;  // dilithium aboard
        double timer = 0; // seconds into its mining, for its shots
        double carry = 0; // dilithium unloaded, not yet a whole one
    };
    /**
     * A freighter on by `seconds`, its ship `ship` (arrived when it has no
     * goal), mining from `moon` and unloading at `station`: to the moon's
     * far side, a shot of 15 each 1.5 seconds to its 150, back to the
     * station's middle, then 15 a second into `dilithium`.
     */
    static void work(Freighter &freighter, Movement::Ship &ship, const gfx::Vec3 &moon, const gfx::Vec3 &station,
                     int &dilithium, double seconds);
    /** What a station or an object to keep clear of takes: its box's middle and half sides on the plane, and its definition's footprintBuffer. */
    struct Footprint {
        float x = 0;
        float z = 0;
        float halfX = 0;
        float halfZ = 0;
        float buffer = 0;
    };
    /**
     * The placement grid's cells (RTS_CFG.h's STATION_PLACEMENT_GRID_SIZE,
     * 50) these take: those under each one's box widened by its buffer.
     */
    static std::set<std::pair<int, int>> takenCellsOf(const std::vector<Footprint> &footprints);
    /**
     * The cells a station being placed at `site` would take: an odd square
     * about the site's cell, as many across as its box widened by its
     * buffer, in whole cells (the cannon one, the mining station five).
     */
    static std::vector<std::pair<int, int>> siteCellsOf(const gfx::Vec3 &site, const Footprint &station);
    /**
     * How far from a station's site its builder waits: the station's
     * ring's half side and 37, as the original's ships stood off a Pulse
     * Cannon (53) and a Mining Station (100).
     */
    static float standOffDistanceOf(const Footprint &station);
    /** Where the selection's ring lies: its middle, and half its side, from a unit's box. */
    static gfx::Vec3 ringMiddleOf(const MapScene::Unit &unit);
    static float ringHalfOf(const MapScene::Unit &unit);
    /** A unit's health bar's place on the screen, from its box there: left, top and width. */
    static std::array<int, 3> healthBarOf(const MapScene::Unit &unit, const gfx::Mat4 &viewProj);
    /** Moves on to `millis` and draws. */
    void draw(uint64_t millis);
    /**
     * Shows a 2D screen in the mission's place (its menu, its debriefing),
     * the mission waiting meanwhile: its clock doesn't run on.
     */
    void drawScreen(const Surface &screen, uint64_t millis);
    /**
     * Where a ship at `from` chasing a target at `to` flies, at `height`:
     * the edge of its weapons' `reach` on the way to it; none while the
     * target is within reach, where it holds.
     */
    static std::optional<gfx::Vec3> chasePointOf(const gfx::Vec3 &from, const gfx::Vec3 &to, float reach, float height);
    /** The skirmish's players, in their slots' order; each one's tally, by slot. */
    const std::vector<Player> &players() const { return _players; }
    const std::map<int, Tally> &tallies() const { return _tallies; }
    /**
     * Whether it's over for the player: lost once they've surrendered or
     * have no units left; won once no other team has any.
     */
    Ending ending() const;
    /** The player gives up. */
    void surrender() { _surrendered = true; }
    /** Whether the MENU button was clicked since this was last asked (it's then forgotten). */
    bool takeMenuRequest() { return std::exchange(_menuWanted, false); }
    const Overview &overview() const { return _overview; }

private:
    const GameFiles &_files;
    MapScene _scene;
    gfx::Renderer3D *_renderer = nullptr;
    Overview _overview;
    Hud _hud;
    Surface _overlay;
    std::optional<Hud::Resources> _resources; // none: not known (a campaign's are its script's)
    double _crewCarry = 0;                    // crew grown, not yet a whole one
    std::set<int> _held;
    int _mouseX = 320;
    int _mouseY = 240;
    int _pressX = -1; // where the left button went down; -1 while it's up
    int _pressY = -1;
    std::vector<int> _selection;
    bool _dragging = false; // the left button held and moved past a click
    int _barPlayer = -1;    // the player whose unit the pointer was on last frame
    double _barLevel = 0;   // how long it's rested on their units, at most a second
    int _hover = -1;
    int _slot = -1;           // the player's, whose units a box selects; -1: anyone's
    std::vector<bool> _moves; // by unit: whether it has engines
    // By unit, those that move: how they fly, and where they are.
    std::vector<std::optional<Movement::Physics>> _physics;
    std::vector<Movement::Ship> _ships;
    std::vector<Hud::Minimap::Icon> _mapIcons; // the map's objects with a mapIcon
    std::vector<int> _displayed;               // the units _display is of
    std::vector<Hud::Unit> _display;
    Hud::Unit _orders;
    bool _building = false; // the build menu open, in place of the orders
    std::vector<std::string> _buildItems;
    Hud::Unit _buildMenu;                         // buildMenuOf's, made as it opens
    TechTree _techTree;                           // the map's
    std::map<size_t, std::vector<Order>> _queues; // by builder: what it's building, first first
    std::optional<std::string> _placing;          // a station being placed, from the build menu
    Footprint _placingFootprint;                  // its, about its site
    std::vector<Footprint> _footprints;           // the stations' and the map's objects to keep clear of
    std::vector<gfx::Vec3> _moons;                // the dilithium moons
    std::vector<Freighter> _freighters;
    // By unit: its shields and hull, its weapons, what it attacks (-1:
    // nothing), and whether it's been destroyed.
    std::vector<Combat::Vitals> _vitals;
    std::vector<std::vector<Combat::Weapon>> _weapons;
    std::vector<int> _targets;
    std::vector<std::vector<Combat::Special>> _specials; // each unit's special weapons
    std::set<std::string> _pods;                         // the research keys pods give (Combat::researchKeyOf)
    std::map<int, std::set<std::string>> _researched;    // each player's, by slot
    std::map<size_t, int> _podsAt;                       // pods fixed to each research station
    std::string _arming; // a special weapon's button pressed that wants a target: the next click's
    // A mine dropped: where, whose, and what it does, homing on the nearest
    // enemy ship in its sight.
    struct Mine {
        gfx::Vec3 at{};
        int slot = 0;
        Combat::Special special;
        double left = 0; // seconds of its life
        float speed = 0; // now, up to its shot's
    };
    std::vector<Mine> _mines;
    std::vector<bool> _cloaked;               // as _targets: under its cloak
    std::map<size_t, double> _revealedUntil;  // a cloaked unit seen by a detector, until the game's seconds
    std::map<size_t, double> _selfDestructAt; // counting down, to the game's seconds
    // A beam from a unit on another: what it does ("hold", "tow",
    // "drainShields", "giveShields", "assimilate"), until when, at what cost
    // a second, how far it reaches, and how much it moves a second.
    struct Hold {
        size_t unit = 0;
        size_t target = 0;
        double until = 0;
        float cost = 0;
        float range = 0;
        std::string kind = "hold";
        float rate = 0;
        gfx::Vec3 offset{}; // a towed ship's place from its tower
    };
    // What special weapons have done to a unit, each until the game's seconds.
    struct Effects {
        double enginesOff = 0;
        double weaponsOff = 0;
        double sensorsOff = 0;
        double frozen = 0;       // neither moving nor firing
        double slowed = 0;       // at half its speed
        double invulnerable = 0; // its shields take no damage
        double reflecting = 0;   // shots at it go back
        double confused = 0;     // it fires on its own side
        double boosted = 0;      // its shots do half again
        double scrambled = 0;    // it takes no orders
        double defending = 0;    // it shoots down shots at it
        double crewDrainUntil = 0;
        float crewDrain = 0; // crew a second, while it lasts
    };
    std::vector<Effects> _effects; // as _targets
    // A field a special weapon leaves on the map: an ion storm, a black
    // hole, a repulsion wave; what it hits a second, as it grows.
    struct Field {
        std::string kind;
        gfx::Vec3 at{};
        int slot = 0;
        double began = 0;
        double until = 0;
        float radius = 0; // now
        float grow = 0;   // units a second
        float damage = 0; // a second, or once as a wave passes
        std::set<size_t> hit;
    };
    std::vector<Field> _fields;
    std::map<size_t, double> _expiresAt; // Gemini Effect's copies, gone at the game's seconds
    // A unit taken over (Computer Override): whose it was, and until when.
    struct Takeover {
        int slot = 0;
        std::optional<gfx::Vec3> colour;
        double until = 0;
    };
    std::map<size_t, Takeover> _takenOver;
    // A Romulan Spy aboard a unit, showing its player what's round it.
    struct Spy {
        size_t target = 0;
        int slot = 0;
        double until = 0;
    };
    std::vector<Spy> _spies;
    // The map's places ships path round: its asteroid fields and black
    // holes (RTS_CFG.h's pathing), as obstacles.
    std::vector<Movement::Obstacle> _fieldsInWay;
    // Crew: a unit transporting to another (boarding it, or reinforcing
    // an ally), going near enough first; parties on their way; ships sent
    // to be repaired, and stations recrewing from the pool.
    bool _transporting = false;            // the transport button pressed: the next click's unit
    std::map<size_t, size_t> _transportTo; // by unit: its target
    std::map<size_t, double> _nextParty;   // by unit: when it can send another, the game's seconds
    struct Party {
        size_t from = 0;
        size_t target = 0;
        int slot = 0;
        float crew = 0;
        float boarding = 1;
        double arrives = 0;
    };
    std::vector<Party> _parties;
    std::map<size_t, size_t> _repairingAt; // by ship: the station it's repaired at
    std::set<size_t> _recrewing;           // stations taking crew from the pool
    std::map<size_t, double> _poolCarry;   // crew owed a unit from the pool, not yet a whole one
    std::vector<Hold> _holds;
    std::vector<bool> _chasing; // as _targets: its goal is its target's, set by pursue()
    std::vector<bool> _dead;
    std::vector<Combat::Shot> _shots;
    // A shield glowing where it was last hit: from where, when (the game's
    // seconds), and the shields left of their most.
    struct ShieldHit {
        size_t unit = 0;
        gfx::Vec3 from{};
        double at = 0;
        float left = 1;
    };
    std::vector<ShieldHit> _shieldHits;
    // A unit's explosion: its fireball's model, where, and when it began.
    struct Explosion {
        std::string model;
        gfx::Vec3 at{};
        double began = 0;
        // Its shockwave (the fireball's shockwave, xshock): when, how fast,
        // how far, and how it's drawn.
        double shockDelay = 0;
        float shockRate = 0; // 0: none
        float shockRadius = 0;
        float shockFade = 0; // the share of its way over which it fades out
        int shockRays = 16;
        std::string shockTexture;
        std::array<float, 3> shockColour{1, 1, 1};
    };
    std::vector<Explosion> _explosions;
    std::vector<Player> _players;
    std::map<int, Tally> _tallies; // by slot
    bool _surrendered = false;
    bool _menuWanted = false;
    bool _targeting = false;   // the attack button pressed: the next click picks the target
    uint32_t _roll = 12345;    // the shots' damage rolls
    int _cineUnit = -1;        // the cinematic view's unit while none is selected
    double _cineUntil = 0;     // the game's second it moves on from it
    uint32_t _cineRoll = 2024; // its choices' rolls
    std::vector<Site> _sites;
    gfx::MeshId _cellMesh = 0;
    gfx::TextureId _cellTaken = 0;
    gfx::TextureId _cellFree = 0;
    gfx::TextureId _cellBlocked = 0;
    gfx::MeshId _ringMesh = 0;
    gfx::TextureId _ringTexture = 0;
    MapScene::Camera camera() const;
    void pressControl(int slot);                          // a control panel's button pressed
    void build(double seconds);                           // the builders' queues on by `seconds`
    void launch(size_t builder, const std::string &item); // a unit built
    void place(int x, int y);                             // the station being placed, at a point of the screen
    void raise(const Site &site);                         // a station built
    void addFighter(const std::string &odf);
    void fight(double seconds);
    int liveUnitAt(const gfx::Mat4 &viewProj, int x, int y) const; // unitAt's, but not a destroyed one
    void destroy(size_t unit, int by);                             // by: the slot of the player whose shot it was
    bool attack(int target); // the selected units with weapons take it as their target: whether any did
    void pursue();           // ships whose target is out of their weapons' reach fly after it
    bool usable(size_t unit, const Combat::Special &special) const; // researched, made, charged and ready
    bool yardReady(const std::string &podTooltip) const; // whether the yard of the ship a pod refits can be built
    void useSpecial(const std::string &button,
                    int target); // the selected units with it fire it, at target if it wants one
    void fireSpecial(size_t unit, Combat::Special &special, int target);
    void moveMines(double seconds);
    // special_weapons.cpp: the special weapons past the first few.
    static bool madeKind(const Combat::Special &special);   // whether Reluva makes it
    static bool wantsPoint(const Combat::Special &special); // fired at a point of the map: the transwarps
    bool fireMore(size_t unit, Combat::Special &special, int target);
    void firePoint(const std::string &button, const gfx::Vec3 &point);
    void runEffects(double seconds); // the fields, beams, drains and what wears off
    // crew.cpp: transporting, boarding and capture, repair and recrew.
    void transport(size_t unit, size_t target);
    void runCrew(double seconds);
    void fromPool(size_t unit, Hud::Resources &bank, double seconds); // recrewing, 20 a second
    void capture(size_t unit, int slot, float crew);
    std::optional<size_t> repairStationFor(size_t ship) const;
    bool disabled(size_t unit) const; // without crew, it neither moves nor fires
    bool active(double until) const { return double(_last) / 1000 < until; }
    std::vector<int> obeying(const std::vector<int> &selection) const; // those not scrambled
    std::optional<size_t> addShip(const std::string &odf, const gfx::Vec3 &at, float heading,
                                  const MapScene::Unit &owner);
    void makeCells();
    void drawShots(const MapScene::Camera &view);
    void drawEffects(double now); // the shields' glows and the explosions, at the game's seconds
    gfx::MeshId _shotMesh = 0;
    // By sprite: its texture, and its rectangle in it (u0, v0, u1, v1).
    std::map<std::string, std::pair<gfx::TextureId, std::array<float, 4>>> _shotLooks;
    std::optional<Footprint> footprintOf(const std::string &odf, const gfx::Vec3 &at, float fallback);
    bool placeable(const gfx::Vec3 &site, const Footprint &station) const;
    Hud::Resources *bankOf(int slot); // a player's counters: the player's, or a computer's
    void startSite(size_t builder, const gfx::Vec3 &at, const std::string &item, const Footprint &station);
    void think(double seconds);             // the computer players'
    Combat::Pace paceOf(size_t unit) const; // how it moves, for a shot's chance
    // A computer player: its slot, its build list, its counters.
    struct Computer {
        int slot = 0;
        std::string race;
        bool stances = false;
        ComputerPlayer::Stance stance = ComputerPlayer::Stance::kNormal;
        std::vector<ComputerPlayer::Element> list;
        Hud::Resources bank;
        double crewCarry = 0;
        double think = 0; // seconds to its next look
        double plan = 0;  // seconds to its next goals
        Strategy::Personality personality;
        std::vector<bool> explored; // by strategic cell, row by row: seen
        // Where its stations go by (AI_Base_Building's m_base_x, m_base_z):
        // its starbase's spot, then a moon for each mining station's base.
        std::vector<gfx::Vec3> bases;
        std::vector<size_t> baseMoons; // into _moons, by base past the first
        size_t lastBase = 0;           // the base it built by last (m_current_base)
        // Spots lately taken, by base and their place in its rings: the looks
        // left before they're tried again (m_location_off_limits).
        std::map<std::pair<size_t, int>, int> offLimits;
    };
    void plan(Computer &c); // its ships' goals
    // Where its station goes, by the base `e` names (0x4182f0).
    std::optional<gfx::Vec3> siteFor(Computer &c, const ComputerPlayer::Element &e, const Footprint &station);
    void weighStances(); // each Medium or Hard one's, by its strength against the others'
    void takeStance(Computer &c, ComputerPlayer::Stance stance);
    ConfigFile::Loader aiLoader() const; // reads AI/AIPs
    double _stanceClock = 0;             // seconds to the next weighing
    void engage();                       // armed units take the nearest enemy in reach
    std::vector<float> _scans;           // by unit: how far it sees (rangeScan)
    std::vector<Computer> _computers;
    std::vector<std::string> _classes; // by unit: its classLabel, lower case
    void drawPlacement(const MapScene::Camera &view);
    std::vector<std::string> ownedOdfs() const; // the player's units' definitions
    void makeRing();
    uint64_t _last = 0;
};

} // namespace reluva::storm3d
