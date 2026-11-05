// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <climits>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/game/projectiles.h"
#include "engines/stgenerations/game/render3d.h"
#include "engines/stgenerations/formats/sprite.h"
#include "engines/stgenerations/game/walker.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// The actors of a level (GAME.LV2 type 6): their AI, movement, animation
// and sprites, after the original's actor update (0x4C3C10) and animation
// timer (0x4C4230).
//
// Each actor is in one of these modes, which scripts set:
//
// | Mode | Behaviour | Animation |
// |------|-----------|-----------|
// | 0 idle | stands | stand (sequence 0) |
// | 1 | stands | pain (sequence 2) |
// | 2 face | turns to face its target or the player; event 36 when facing | stand |
// | 3 attack | as 2, and fires at the start of each animation cycle | attack (3) |
// | 4, 5 | stands | deaths (4, 5) |
// | 6 wander | walks around its home or the player, turning at random | walk (1) |
// | 7 walk to | walks straight at its target or the player; event 36 while within reach | walk (1) |
// | 8 hidden | removed from the level | |
//
// Every animation cycle ends with event 37. Nothing changes mode by itself;
// scripts react to the events. Actors move with the player's collision
// (walls, other actors, step height) and snap onto floors rather than fall.
class Actors {
public:
    enum Mode {
        kModeIdle = 0,
        kModePain = 1,
        kModeFace = 2,
        kModeAttack = 3,
        kModeDeathA = 4,
        kModeDeathB = 5,
        kModeWander = 6,
        kModeWalkTo = 7,
        kModeHidden = 8
    };

    // Script events actors raise, sent to the actor's object.
    enum Event {
        kEventArrived = 36,
        kEventCycle = 37,
        kEventPlayerNear = 29,
        kEventPlayerLost = 31,
        kEventPlayerInRange = 33,
        kEventPlayerOutOfRange = 35,
        // The outcome of a hit (0x45747E).
        kEventStunned = 38,
        kEventGrazed = 39,
        kEventHurt = 40,
        kEventKnockedOut = 41,
        kEventKilled = 42
    };

    // An actor object's toughness (SET_ACTOR_COMBAT_STATS). A hit with
    // power above fullThreshold takes damage from both hit points and stun
    // points; above stunThreshold from stun points only; above
    // noticeThreshold it is only noticed.
    struct CombatStats {
        int32_t fullThreshold = INT32_MAX;
        int32_t stunThreshold = INT32_MAX;
        int32_t noticeThreshold = INT32_MAX;
        int32_t stunMax = 0;
        int32_t stun = 0;
        int32_t hitPointsMax = 0;
        int32_t hitPoints = 0;
    };
    // In SET_ACTOR_COMBAT_STATS, this keeps a value as it is.
    static const int32_t kKeep = 0x8000;

    struct ScriptEvent {
        // The actor's script object id (id | 0xA000).
        uint16_t object;
        uint16_t event;
    };

    // Loads sprite set NNN.gs2; returns false if it can't.
    typedef std::function<bool(uint16_t number, SpriteSet& set)> SpriteLoader;

    static const uint16_t kObjectType = 0xA000;

    // Places every actor on its floor and loads the sprite sets they use.
    Actors(const LevelFile& level, const Walker& prototype, const SpriteLoader& loader);
    ~Actors();

    int getCount() const { return (int)_actors.size(); }

    // Advance by ms milliseconds (the original clamps this to 8-1000): run
    // each actor's mode and animation, and collect the events raised. player
    // is the player's walker; actors standing in zones it can't see are
    // frozen, as in the original.
    void update(uint32_t ms, const Walker& player, std::vector<ScriptEvent>& events);

    // Actors as obstacles, for the player's walker. Updated in place.
    const std::vector<Walker::Obstacle>& getObstacles() const { return _obstacles; }
    // The script object of the actor behind an obstacle.
    uint16_t getObstacleObject(int obstacle) const;

    // The sprites to draw, seen from (cameraX, cameraY).
    void getSprites(double cameraX, double cameraY, uint64_t visibleZones, std::vector<SpriteDraw>& sprites) const;

    // Script control. id is an actor's script object id; ids without type
    // bits are taken as actor ids, as the original's lookup (game.vt[0x15C]) does.
    bool isActor(uint16_t id) const;
    // Set the mode; target is the object to face, attack or walk to (0 = the player).
    void setMode(uint16_t id, int mode, uint16_t target = 0);
    // ACTOR_GO_TO: mode 7 towards a point (flyers climb or sink to its height).
    void goTo(uint16_t id, double x, double y, double z);
    // Set or clear a flag on every part (0x400000 always active, 0x1000000 flyer).
    void setFlag(uint16_t id, uint32_t flag, bool on);
    // GET_ALERTED_COUNT (0xEF): how many actors see the player now.
    int getAlertedCount() const;
    // SET_ACTOR_TARGET (0x5B): a new target, in the same mode.
    void setTarget(uint16_t id, uint16_t target);
    // SET_ACTOR_ANIM_SPEED (0x5C): an animation's speed word (8.8).
    void setAnimationSpeed(uint16_t id, int animation, uint16_t speed);
    // SET_ACTOR_CLIMB_ANGLE.
    void setClimbAngle(uint16_t id, int degrees);
    // Position in world units and heading in 1/65536 turns.
    bool getPosition(uint16_t id, double& x, double& y, double& z, uint16_t& heading) const;
    void setPosition(uint16_t id, double x, double y, double z, uint16_t heading);
    void setHeading(uint16_t id, uint16_t heading);
    void setStepHeight(uint16_t id, double height);
    void setRadius(uint16_t id, double radius);
    void setBodyHeight(uint16_t id, double height);
    void setReach(uint16_t id, double reach);
    void setAnimationRate(uint16_t id, int mode, uint8_t rate);
    // Wander settings (SET_ACTOR_COMBAT_PARAMS).
    void setWander(uint16_t id, bool followPlayer, uint16_t minTurn, uint16_t maxTurn, double innerRadius,
                   double homeRadius, double outerRadius, double minWalk, double maxWalk);
    // Distances for the proximity events between actors of class
    // actorClass and the player (targetClass 0): radii[1] near, [2] lost,
    // [3] in range, [4] out of range. radii[0] is unused, as in the original.
    void setProximity(int actorClass, int targetClass, const double radii[5]);

    // Save or load every actor's state, and the settings scripts gave them.
    void sync(Serializer& s);

    // Set the stats; values of kKeep leave a stat as it is. Order as CombatStats.
    void setCombatStats(uint16_t id, const int32_t values[7]);
    CombatStats getCombatStats(uint16_t id) const;
    // Actors on the same team don't hurt each other.
    void setTeam(uint16_t id, int16_t team);
    int16_t getTeam(uint16_t id) const;
    // Take a hit (0x45747E): returns the event the actor's object gets
    // (kEventHurt and so on), or 0 if it doesn't notice.
    uint16_t takeHit(uint16_t id, int damage, int power);
    // The radius it collides and is hit with.
    double getRadius(uint16_t id) const;
    // The projectile type it fires in attack mode, or -1 for none.
    void setWeapon(uint16_t id, int weapon);

    // A shot an actor fired: where from and which way (radians, as Projectiles::fire).
    struct Shot {
        uint16_t shooter;
        int weapon;
        double x, y, z, yaw, pitch;
    };
    // Move the shots fired since the last call into shots.
    void takeShots(std::vector<Shot>& shots);
    // The actors, as things shots can hit.
    void getTargets(std::vector<Projectiles::Target>& targets) const;

    // Headings: 65536 is a full turn, 0 faces +y and 16384 faces -x.
    static uint16_t headingTowards(double dx, double dy);

private:
    struct Actor;

    std::vector<int> findAll(uint16_t id) const;
    bool getTarget(const Actor& actor, const Walker& player, double& x, double& y, double& z) const;
    // Advance the animation; returns the distance walked with it.
    double runAnimation(Actor& actor, uint32_t ms, std::vector<ScriptEvent>& events);
    void runProximity(Actor& actor, const Walker& player, bool frozen, std::vector<ScriptEvent>& events);
    void runWander(Actor& actor, int index, double step, const Walker& player, uint32_t ms);
    void runWalkTo(Actor& actor, double step, const Walker& player, uint32_t ms, std::vector<ScriptEvent>& events);
    void moveForward(Actor& actor, double distance, uint32_t ms);
    bool turnTowards(Actor& actor, uint16_t goal, uint32_t ms);
    Shot aim(const Actor& actor, const Walker& player) const;
    uint32_t random();

    const LevelFile& _level;
    std::vector<std::unique_ptr<Actor>> _actors;
    std::map<uint16_t, std::unique_ptr<SpriteSet>> _sprites;
    std::vector<Walker::Obstacle> _obstacles;
    // (actor class << 8 | target class) -> radii.
    std::map<uint16_t, std::vector<double>> _proximity;
    // By script object id: stats belong to the object, not its parts.
    std::map<uint16_t, CombatStats> _combat;
    std::map<uint16_t, int16_t> _teams;
    std::vector<Shot> _shots;
    uint32_t _seed = 1;
};

} // namespace reluva::stgenerations

