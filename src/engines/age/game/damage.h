// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "common/types.h"
#include "engines/age/render/car_actor.h"
#include "engines/age/render/render_cache.h"
#include "engines/age/vecmath.h"

#include <array>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace reluva::age {

struct TuneNode;

/**
 * How particles are made: a birth rule, as a .vehCarDamage, the props'
 * .dgBangerData (its BirthRule block) and tune/effects/<name>.asBirthRule have
 * them.
 */
struct BirthRule {
    Vec3 positionVar{}; // metres either way
    // metres a second, and how far either way
    Vec3 velocity{0, 1, 0};
    Vec3 velocityVar{};
    // seconds
    float life = 1;
    float lifeVar = 0;
    // metres
    float radius = 0.4f;
    float radiusVar = 0;
    float dRadius = 0;    // growth, metres a frame (at 30 frames a second)
    float drag = 0;       // slowing, a share of the speed a second
    float dAlpha = 0;     // fading, in 255ths a frame
    float gravity = 0;    // metres a second squared, up
    int initialBlast = 0; // particles at once
    // then a second, for that long (0: no end)
    float spewRate = 0;
    float spewTime = 0;
    std::array<float, 4> color{1, 1, 1, 1};
    // TexFrameStart, TexFrameEnd
    int frame = 0;
    int frameEnd = 0;
};
using SmokeRule = BirthRule;
/** From a rule's block (the fields above; the others aren't used). */
BirthRule parseBirthRule(const TuneNode &n);

/** tune/vehicle/<car>.vehCarDamage: see docs/engines/age/games/mm2/systems/damage.md. */
struct DamageSetup {
    float maxDamage = 300000;     // wrecked
    float medDamage = 150000;     // smoking
    float impactThreshold = 1500; // knocks below this (kg m/s) do nothing
    float regenerateRate = 0;     // a second
    float textureRadius = 2.7f;   // TextelDamageRadius: metres round a knock that look damaged
    // car-relative
    Vec3 smokeOffset{};
    Vec3 smokeOffset2{};
    SmokeRule smoke;
};
/** Reads a .vehCarDamage's text; fields it leaves out keep their defaults. */
Error parseDamageSetup(std::string_view text, DamageSetup &out);
/** Reads the car's .vehCarDamage; without one, @p out holds the defaults and the error says why. */
Error loadDamageSetup(const SearchSet &files, const std::string &car, DamageSetup &out);

/** A car's damage: knocks over the threshold add what they're over it. */
class CarDamage {
public:
    /** Mends it: no damage. */
    void reset() { _damage = 0; }
    /** Sets the damage, as another player's machine says. */
    void set(float damage) { _damage = damage; }
    /** An impulse (kg m/s); what it added. */
    float knock(const DamageSetup &setup, float impulse);
    /** Mends it by the setup's RegenerateRate over @p dt seconds, down to 0. */
    void update(const DamageSetup &setup, float dt);
    /** In the setup's units: impulse over the threshold, up to MaxDamage. */
    float damage() const { return _damage; }
    /** 0..1 of the way to wrecked. */
    float fraction(const DamageSetup &setup) const {
        return setup.maxDamage > 0 ? std::min(1.0f, _damage / setup.maxDamage) : 0;
    }
    /** At MedDamage or more. */
    bool smoking(const DamageSetup &setup) const { return _damage >= setup.medDamage; }
    /** At MaxDamage. */
    bool wrecked(const DamageSetup &setup) const { return _damage >= setup.maxDamage; }

private:
    float _damage = 0;
};

class SmokePuffs;

/** A driven car's damage, as the Drive page and the race screen keep it. */
struct CarWear {
    DamageSetup setup;
    CarDamage damage;
    float puffs = 0; // owed
    // The dents made, for the other players to see: how many (counting
    // on), and the last four, newest first, in the car's frame.
    uint8_t dents = 0;
    std::array<Vec3, 4> recentDents{};

    /** Its setup (the defaults without one), mended. */
    void load(const SearchSet &files, const std::string &car, CarActor &actor);
    /** No damage, no puffs owed, and the body's dents gone. */
    void mend(CarActor &actor);
    /**
     * After the car's step: its knock (sim.impact) damages it and dents the
     * body there; smoking, it puffs.
     */
    void update(float dt, CarActor &actor, SmokePuffs *smoke);
    /** Its smoke alone, at the damage it has (another player's car). */
    void puff(float dt, const CarActor &actor, SmokePuffs *smoke);
    /** 0..1 of the way to wrecked. */
    float fraction() const { return damage.fraction(setup); }
    /** At its setup's MaxDamage. */
    bool wrecked() const { return damage.wrecked(setup); }
};

/**
 * Particles: billboards of one of the game's particle textures
 * (texture/fxpt<n>), rising, falling and fading as their rules say. The
 * cars' smoke is texture/fxpt8's (its four frames, two by two).
 */
class SmokePuffs {
public:
    /** Draws with @p renderer and @p textures, which must outlive it. */
    SmokePuffs(gfx::Renderer3D &renderer, TextureCache &textures);
    ~SmokePuffs();
    SmokePuffs(const SmokePuffs &) = delete;
    SmokePuffs &operator=(const SmokePuffs &) = delete;

    /**
     * A texture and how many frames across it (and down): 2 for the game's
     * 128-pixel sheets of four, else 1.
     */
    struct Look {
        gfx::TextureId texture = 0;
        int grid = 1;
    };
    /** A particle texture's look, loaded the first time it's asked for. */
    Look look(const std::string &texture);

    /**
     * One particle (the smoke's look by default), carried at half the car's
     * velocity; none past 600 alive.
     */
    void emit(const BirthRule &rule, const Vec3 &at, const Vec3 &carVelocity, const Look *look = nullptr);
    /**
     * A rule's InitialBlast at once (at least one), then its SpewRate for
     * its SpewTimeLimit.
     */
    void burst(const BirthRule &rule, const Vec3 &at, const Vec3 &velocity, const Look &look);
    /**
     * What a car's wheels throw up from what they're on (the surfaces'
     * particles): tune/effects/<name>.asBirthRule, by Reluva's reading of the
     * materials' effect numbers (see docs/engines/age/games/mm2/systems/damage.md).
     */
    void loadWheelEffects(const SearchSet &files);
    /** Throws up the particles of what the car's wheels slip, or roll, on. */
    void wheels(float dt, const CarSim &car);
    /** Moves, grows and fades the particles over @p dt seconds; keeps the spews going. */
    void update(float dt);
    /** Drops every particle, spew and what the wheels owe. */
    void clear() {
        _puffs.clear();
        _spews.clear();
        _wheelOwed.clear();
    }
    /** The particles alive. */
    size_t count() const { return _puffs.size(); }
    /** Each particle as a billboard facing @p eye, fading out over its life. */
    void draw(const Vec3 &eye) const;

private:
    struct Puff {
        Vec3 position;
        Vec3 velocity;
        float age = 0;
        float life = 1;
        float radius = 0.4f;
        float grow = 0;
        float gravity = 0;
        float drag = 0;
        float fade = 0;
        std::array<float, 4> color;
        int frame = 0;
        Look look;
    };
    struct Spew {
        BirthRule rule;
        Vec3 at;
        Vec3 velocity;
        Look look;
        float left = 0;
        float owed = 0;
    };
    gfx::Renderer3D &_renderer;
    TextureCache &_textures;
    gfx::MeshId _quad = 0;
    Look _smoke;
    std::map<std::string, Look> _looks;
    std::vector<Puff> _puffs;
    std::vector<Spew> _spews;
    struct WheelEffect {
        BirthRule rule;
        Look look;
        bool loaded = false;
    };
    std::array<WheelEffect, 8> _wheelEffects;
    std::map<const CarSim *, std::array<float, 8>> _wheelOwed; // particles owed, per wheel and threshold
    std::minstd_rand _random{7};
};

} // namespace reluva::age
