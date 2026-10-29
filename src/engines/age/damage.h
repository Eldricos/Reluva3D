// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "common/types.h"
#include "engines/age/car_actor.h"
#include "engines/age/render_cache.h"
#include "engines/age/vecmath.h"

#include <array>
#include <random>
#include <string>
#include <vector>

namespace reluva::age {

// How a car's smoke puffs are made: the birth rules of a .vehCarDamage
// and tune/effects/*.asBirthRule (the fields Reluva uses).
struct SmokeRule {
    Vec3 velocity{0, 1, 0}, velocityVar{}; // metres a second, and how far either way
    float life = 1, lifeVar = 0;           // seconds
    float radius = 0.4f, radiusVar = 0;    // metres
    float dRadius = 0;                     // growth, metres a frame (at 30 frames a second)
    float gravity = 0;                     // metres a second squared, up
    std::array<float, 4> color{1, 1, 1, 1};
    int frame = 0; // TexFrameStart
};

// tune/vehicle/<car>.vehCarDamage: see docs/engines/age-driving.md.
struct DamageSetup {
    float maxDamage = 300000;           // wrecked
    float medDamage = 150000;           // smoking
    float impactThreshold = 1500;       // knocks below this (kg m/s) do nothing
    float regenerateRate = 0;           // a second
    float textureRadius = 2.7f;         // TextelDamageRadius: metres round a knock that look damaged
    Vec3 smokeOffset{}, smokeOffset2{}; // car-relative
    SmokeRule smoke;
};
Error parseDamageSetup(std::string_view text, DamageSetup& out);
Error loadDamageSetup(const SearchSet& files, const std::string& car, DamageSetup& out);

// A car's damage: knocks over the threshold add what they're over it.
class CarDamage {
public:
    void reset() { damage_ = 0; }
    void set(float damage) { damage_ = damage; } // as another player's machine says
    // An impulse (kg m/s); what it added.
    float knock(const DamageSetup& setup, float impulse);
    void update(const DamageSetup& setup, float dt);
    float damage() const { return damage_; }
    // 0..1 of the way to wrecked.
    float fraction(const DamageSetup& setup) const {
        return setup.maxDamage > 0 ? std::min(1.0f, damage_ / setup.maxDamage) : 0;
    }
    bool smoking(const DamageSetup& setup) const { return damage_ >= setup.medDamage; }
    bool wrecked(const DamageSetup& setup) const { return damage_ >= setup.maxDamage; }

private:
    float damage_ = 0;
};

class SmokePuffs;

// A driven car's damage, as the Drive and Race pages keep it.
struct CarWear {
    DamageSetup setup;
    CarDamage damage;
    float puffs = 0; // owed
    // The dents made, for the other players to see: how many (counting
    // on), and the last four, newest first, in the car's frame.
    uint8_t dents = 0;
    std::array<Vec3, 4> recentDents{};

    // Its setup (the defaults without one), mended.
    void load(const SearchSet& files, const std::string& car, CarActor& actor);
    void mend(CarActor& actor);
    // After the car's step: its knock (sim.impact) damages it and dents the
    // body there; smoking, it puffs.
    void update(float dt, CarActor& actor, SmokePuffs* smoke);
    // Its smoke alone, at the damage it has (another player's car).
    void puff(float dt, const CarActor& actor, SmokePuffs* smoke);
    float fraction() const { return damage.fraction(setup); }
    bool wrecked() const { return damage.wrecked(setup); }
};

// Smoke puffs: billboards of texture/fxpt8 (its four frames, two by two),
// rising and fading.
class SmokePuffs {
public:
    SmokePuffs(gfx::Renderer3D& renderer, TextureCache& textures);
    ~SmokePuffs();
    SmokePuffs(const SmokePuffs&) = delete;
    SmokePuffs& operator=(const SmokePuffs&) = delete;

    void emit(const SmokeRule& rule, const Vec3& at, const Vec3& carVelocity);
    void update(float dt);
    void clear() { puffs_.clear(); }
    size_t count() const { return puffs_.size(); }
    void draw(const Vec3& eye) const;

private:
    struct Puff {
        Vec3 position, velocity;
        float age = 0, life = 1, radius = 0.4f, grow = 0, gravity = 0;
        std::array<float, 4> color;
        int frame = 0;
    };
    gfx::Renderer3D& renderer_;
    gfx::MeshId quad_ = 0;
    gfx::TextureId texture_ = 0;
    std::vector<Puff> puffs_;
    std::minstd_rand random_{7};
};

} // namespace reluva::age
