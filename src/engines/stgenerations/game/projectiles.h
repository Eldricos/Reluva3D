// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <memory>
#include <vector>

#include "engines/stgenerations/game/random.h"
#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/game/render3d.h"
#include "engines/stgenerations/game/walker.h"

namespace reluva::stgenerations {

// Shots in flight: phaser beams, energy bolts and the sparks they throw
// up, after the original's projectile types (built from GAME.BIN section
// 4 at 0x43FE9C) and their update (0x4C4C70).
//
// Shots fly straight, without gravity. Each frame a shot sweeps from where
// it was to where it is (0x4BA9E0) and stops at the nearest thing in the
// way: a wall seen from its front (or a wall solid on both sides), a floor
// or ceiling it crosses, a billboard, an actor other than the one that
// fired it, or the player.
//
// - A **beam** is a head followed by a trail of sprites a fixed distance
//   apart, strung out from the muzzle. When the head hits, the trail runs
//   on into the hit point and vanishes there.
// - A **bolt** is a single sprite.
// - A **burst** throws sparks in a cone. They bounce, and do no harm.
//
// Beams and bolts report what they hit; a type can name another (sparks)
// to throw where it hits or runs out.
class Projectiles {
public:
    enum Kind { kKindNone = 0, kKindBeam = 1, kKindBolt = 2, kKindBurst = 3 };

    // A section 4 record (100 bytes).
    struct Type {
        int kind = kKindNone;
        // Animation: frame = age * rate / 65536 ms, wrapped by frames.
        uint32_t animationRate = 0;
        uint32_t lifetime = 0;
        uint16_t texture = 0;
        uint16_t frames = 1;
        // Sprite half width and height, centred on the shot; the half width is also its radius.
        double halfWidth = 0, height = 0;
        bool keyed = false, translucent = false;
        // Units per millisecond.
        double speed = 0;
        int fireSound = -1, impactSound = -1;
        int16_t damage = 0;
        int16_t team = 0;
        // Beams: trail sprites and their spacing. Bursts: sparks and the cone's half angle (radians).
        int count = 0;
        double spacing = 0;
        // Type thrown where it hits or runs out, or -1.
        int impact = -1;
    };

    // Who fired: an actor's object id, or one of these.
    static const int kShooterPlayer = -1;
    static const int kShooterNone = -2;

    // What a shot ran into.
    struct Hit {
        enum Target { kNothing, kWall, kFlat, kBillboard, kActor, kPlayer };
        Target target = kNothing;
        // Index into the level's walls, flats or billboards, or the actor's object id.
        int index = -1;
        double x = 0, y = 0, z = 0;
        int type = -1;
    };

    // Something the mission should know about.
    struct Report {
        enum What {
            kFired, // type's fire sound at (x, y, z)
            kHit    // a beam or bolt hit something
        };
        What what;
        Hit hit;
    };

    // An actor as something shots can hit.
    struct Target {
        uint16_t object;
        double x, y, bottom, top, radius;
    };

    Projectiles(const LevelFile &level, const std::vector<uint8_t> &section4);
    ~Projectiles();

    int getTypeCount() const { return (int)_types.size(); }
    const Type &getType(int type) const { return _types[type]; }
    // Weapon items set the damage of the type they fire (0x44A03A); everything firing it is affected.
    void setDamage(int type, int16_t damage) { _types[type].damage = damage; }

    // Fire a shot of the given type from (x, y, z) towards heading yaw
    // (radians, 0 along +x, pi/2 along +y) and pitch (radians, up), with
    // extraSpeed (units per ms) added, as impact sparks are.
    void fire(int type, double x, double y, double z, double yaw, double pitch, int shooter, double extraSpeed = 0);

    // Advance by ms. zones are the zones in play, for walls, floors and
    // billboards; targets are the actors; the player stands at (playerX,
    // playerY) with eyes at playerEye and the given radius.
    void update(uint32_t ms, uint64_t zones, const std::vector<Target> &targets, double playerX, double playerY,
                double playerEye, double playerRadius, std::vector<Report> &reports);

    // Sprites for everything in flight.
    void getSprites(std::vector<SpriteDraw> &sprites);

    int getCount() const { return (int)_shots.size(); }
    // Walls a door moved: take their new places.
    void refreshWalls(const std::vector<int> &walls);
    // Drop every shot in flight (loading a saved game).
    void clear() { _shots.clear(); }

    // Below and above the eye, the part of the player shots hit (0x57D98C, 0x57D990).
    static const double kPlayerBelowEye;
    static const double kPlayerAboveEye;

private:
    struct Shot {
        int type;
        int shooter;
        double x, y, z;
        // Units per ms.
        double vx, vy, vz;
        uint32_t age = 0;
        uint32_t phase = 0;
        // Beams: where the head set out, how far it has gone along (dx,
        // dy, dz), and where it hit (or -1).
        double originX = 0, originY = 0, originZ = 0;
        double travelled = 0, hitAt = -1;
        bool dead = false;
    };

    struct Line {
        double x1, y1, x2, y2;
        double nx, ny, d;
    };

    void makeLine(int index);
    Hit sweep(const Shot &shot, double x0, double y0, double z0, double x1, double y1, double z1, uint64_t zones,
              const std::vector<Target> &targets, double playerX, double playerY, double playerEye, double playerRadius,
              double &t, double &normalX, double &normalY) const;
    void spawnBurst(const Type &type, double x, double y, double z, double yaw, double pitch, double extraSpeed);
    void finish(Shot &shot, const Hit &hit, double yaw, double pitch, std::vector<Report> &reports);
    const Canvas *getTexture(uint16_t id);

    const LevelFile &_level;
    std::vector<Type> _types;
    std::vector<Line> _walls;
    std::vector<Shot> _shots;
    std::map<uint16_t, std::unique_ptr<LevelFile::Texture>> _textures;
    RandomSource _random;
};

} // namespace reluva::stgenerations
