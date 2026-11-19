// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include "engines/stgenerations/formats/level.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// Moves the player through a level the way the original does (0x4B46A0):
//
// - The player is a cylinder of radius 1.2 whose feet are 6 units below
//   the eye, reaching 0.51 above it. Walls lower than 2 units above the
//   feet are stepped over.
// - A move is swept against wall lines and wall ends (0x4B9940). On a hit
//   the player stops at the wall and slides along it with what is left of
//   the move, once; a second hit ends the move there (0x4B9870).
// - A wall blocks only from its front, the side its normal points to,
//   unless it is flagged solid on both sides.
// - The player stands on the highest floor no more than the step height
//   above the feet, and falls when there is none below.
// - The floor stood on decides which zones are active. Only walls and
//   floors in those zones count, and the renderer only draws them. With
//   no floor underfoot, every zone is active.
//
// Distances are in world units (not fixed point).
class Walker {
public:
    // The original's defaults (0x4BE150); scripts can change them.
    static constexpr double kEyeHeight = 6.0;
    static constexpr double kHeadroom = 0.51;
    static constexpr double kStepHeight = 2.0;
    static constexpr double kRadius = 1.2;
    // How much slimmer than the player wall ends are as posts.
    static constexpr double kPostSlack = 0.1;
    // The original's defaults (game+0x10C): gravity 64.4 units/s/s (2 x
    // 32.2), the fastest fall 200 units/s, and a drop of up to 0x4000 (a
    // quarter unit) taken without falling (0x4B4904).
    static constexpr double kGravity = 64.4;
    static constexpr double kMaxFallSpeed = 200.0;
    static constexpr double kStepDown = 0.25;
    // Crouching: the eye 2.5 above the feet (0x28000), moving half a unit a frame.
    static constexpr double kCrouchEye = 2.5;
    static constexpr double kCrouchSpeed = 12.0;
    // The save version that first holds the step-down (see sync()).
    static const uint32_t kStepDownSaveVersion = 6;

    static const uint64_t kAllZones = ~(uint64_t)0;

    explicit Walker(const LevelFile &level);
    // Copies share the level's wall lines, so walkers for actors are cheap.
    Walker(const Walker &other) = default;

    // Put the player's feet at (x, y, feet) and settle onto the floor there.
    void place(double x, double y, double feet);
    // Walls a door moved: take their new places, for this walker and every copy of it.
    void refreshWalls(const std::vector<int> &walls);
    // Save or load the position and the settings scripts change.
    void sync(Serializer &s);

    // Walk by (dx, dy) over the given time, colliding and following floors.
    void move(double dx, double dy, double seconds);

    // A round obstacle, such as an actor.
    struct Obstacle {
        double x, y, radius;
        double bottom, top;
    };

    // Obstacles to collide with besides walls; the vector must outlive the
    // walker's moves. ignore is an index to skip (the mover itself), or -1.
    void setObstacles(const std::vector<Obstacle> *obstacles, int ignore = -1) {
        _obstacles = obstacles;
        _ignoredObstacle = ignore;
    }

    // Without falling, the walker drops straight onto lower floors, as actors do.
    void setFalls(bool falls) { _falls = falls; }
    // Gravity in units/s/s and the fastest fall in units/s (SET_GRAVITY, LOW_GRAVITY).
    void setGravity(double gravity, double maxFallSpeed) {
        _gravity = gravity;
        _maxFallSpeed = maxFallSpeed;
    }
    double getGravity() const { return _gravity; }
    // Falling speed in units/s, negative while rising.
    double getFallSpeed() const { return _fallSpeed; }
    double getStepHeight() const { return _stepHeight; }
    double getMaxFallSpeed() const { return _maxFallSpeed; }
    // The drop onto a lower floor taken in one step, without falling (SET_GRAVITY's third value).
    void setStepDown(double height) { _stepDown = height; }
    double getStepDown() const { return _stepDown; }
    // How fast a fall hit the floor since the last call, or 0 (for fall damage).
    double takeLanding() {
        double speed = _landingSpeed;
        _landingSpeed = 0;
        return speed;
    }
    // The floor the last landing was on (index into the flats), or -1.
    int getLandingFloor() const { return _landingFloor; }
    // How fast a jump hit a ceiling since the last call, or 0; and which ceiling.
    double takeCeilingHit(int &ceiling) {
        double speed = _ceilingSpeed;
        ceiling = _ceilingFlat;
        _ceilingSpeed = 0;
        return speed;
    }

    // Floors with any of these flags are ignored; LevelFile::kFlagPassThrough always is.
    void setIgnoredFloors(uint32_t flags) { _ignoredFloors = flags | LevelFile::kFlagPassThrough; }

    // With collision off the player walks through walls but keeps to floors.
    void setCollisionEnabled(bool enabled) { _collision = enabled; }
    bool isCollisionEnabled() const { return _collision; }

    double getX() const { return _x; }
    double getY() const { return _y; }
    double getFeet() const { return _feet; }
    // The eye, lowered under a low ceiling (see _ducked) or crouching.
    double getEye() const { return _feet + _eyeHeight - std::max(_ducked, _crouch); }

    // Start a jump at this speed upwards (units per second).
    void jump(double speed);
    // Rising or falling, not standing on a floor.
    bool isAirborne() const;
    // Crouch (or stand up again) over this time: the eye eases down to kCrouchEye.
    void crouch(bool down, double seconds);
    // Whether (x, y) is inside the outline of a flat (index into the level's flats).
    bool flatContains(int flat, double x, double y) const;
    // Move straight up or down, between the floor and the ceiling (no falling).
    void moveVertically(double dz);

    void setEyeHeight(double height) { _eyeHeight = height; }
    void setStepHeight(double height) { _stepHeight = height; }
    void setHeadroom(double height) { _headroom = height; }
    void setRadius(double radius) { _radius = radius; }
    double getRadius() const { return _radius; }

    // The wall (index into the level's walls) the last move ran into, or
    // -1. Like the original, standing still touches nothing.
    int getContactWall() const { return _contactWall; }
    // The obstacle the last move ran into, or -1.
    int getContactObstacle() const { return _contactObstacle; }
    // Whether the last move was stopped short by anything.
    bool wasBlocked() const { return _contactWall >= 0 || _contactObstacle >= 0; }
    uint64_t getActiveZones() const { return _zones; }
    // Index of the flat underfoot, or -1.
    int getFloor() const { return _floor; }

    // The highest floor under (x, y) in the given zones that is no higher
    // than maxHeight, or -1.
    int findFloor(double x, double y, double maxHeight, uint64_t zones) const {
        return findFloor(x, y, maxHeight, zones, _ignoredFloors);
    }
    // The same, skipping floors with any of ignoredFlags instead of setIgnoredFloors()'s.
    int findFloor(double x, double y, double maxHeight, uint64_t zones, uint32_t ignoredFlags) const;
    // The lowest ceiling over (x, y) in the zones above minHeight, or a huge height; which in index.
    double findCeiling(double x, double y, double minHeight, uint64_t zones, int *index = nullptr) const;
    // Set _ducked for where the player stands.
    void duck();
    void followFloorOnly(double seconds);

    // Whether nothing blocks the view from a to b, as the original's test
    // for actors seeing the player (0x4BB900): visible walls and floors in
    // the given zones block, with a quarter unit of slack on walls; nothing
    // steeper than twice as high as it is long is ever seen.
    bool lineOfSight(double ax, double ay, double az, double bx, double by, double bz, uint64_t zones) const;

    struct Hit {
        // Fraction of the move before touching.
        double t;
        // Unit direction along the obstacle, to slide along.
        double slideX, slideY;
        // Index into the level's walls, or -1.
        int wall;
        // Index into the obstacles, or -1.
        int obstacle;
    };

    // Sweep the player from (x, y) by (dx, dy) and report the first wall
    // touched. Returns false if the way is clear.
    bool sweep(double x, double y, double dx, double dy, Hit &hit) const;

private:
    struct WallLine {
        double x1, y1, x2, y2;
        double nx, ny, d;
        double top, bottom;
        uint64_t zones;
        bool bothSides;
        // kWallNoCollision: never in the way.
        bool passable;
        int index;
    };

    void makeLine(int index, WallLine &line) const;
    void collideAndSlide(double dx, double dy);
    void followFloor(double seconds);

    const LevelFile &_level;
    std::shared_ptr<std::vector<WallLine>> _walls;
    double _x = 0, _y = 0, _feet = 0;
    double _fallSpeed = 0;
    double _gravity = kGravity, _maxFallSpeed = kMaxFallSpeed;
    double _stepDown = kStepDown;
    double _landingSpeed = 0;
    int _landingFloor = -1;
    double _ceilingSpeed = 0;
    int _ceilingFlat = -1;
    int _floor = -1;
    int _contactWall = -1;
    int _contactObstacle = -1;
    const std::vector<Obstacle> *_obstacles = nullptr;
    int _ignoredObstacle = -1;
    bool _falls = true;
    uint32_t _ignoredFloors = LevelFile::kFlagPassThrough;
    double _eyeHeight = kEyeHeight;
    // How far the eye is lowered to keep the head (eye + headroom) under
    // the ceiling, as 0x4B46A0 does; AMARGOS1's upper doorways are no
    // taller than that.
    double _ducked = 0;
    // How far crouching has brought the eye down.
    double _crouch = 0;
    double _stepHeight = kStepHeight;
    double _headroom = kHeadroom;
    double _radius = kRadius;
    uint64_t _zones = kAllZones;
    bool _collision = true;
};

} // namespace reluva::stgenerations
