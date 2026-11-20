// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "engines/age/game/car.h"
#include "engines/age/render/render_cache.h"

#include <string>

namespace reluva::age {

/**
 * Where a car's dashboard goes in front of the in-car camera
 * (`tune/<car>_dash.asnode`): the dash, roof and steering wheel's places
 * (camera space: x right, y up, z back), the needles' and wheel's pivots
 * and how far each turns (radians). See
 * docs/engines/age/games/mm2/systems/cameras.md.
 */
struct DashSetup {
    Vec3 dashPos{};
    Vec3 roofPos{};
    Vec3 wheelPos{};
    // the *PivotOffset numbers
    Vec3 speedPivot{};
    Vec3 tachPivot{};
    Vec3 damagePivot{};
    Vec3 wheelPivot{};
    Vec3 gearPivot{};
    float wheelFact = 0.9f;
    float rpmRotMin = 0;
    float rpmRotMax = 3;
    float speedRotMin = 0;
    float speedRotMax = 3;
    float damageRotMin = 0;
    float damageRotMax = 3;
};
/** Reads a car's dashboard settings. Fails without them. */
Error loadDashSetup(const SearchSet &files, const std::string &car, DashSetup &out);

/**
 * The dashboard (`geometry/<car>_dash.pkg`: flat parts, dash, roof, wheel,
 * speed_needle, tach_needle, damage_needle, gear_indicator), drawn over
 * the view from inside the car.
 */
class Dashboard {
public:
    /** False if the car has none. */
    bool load(const SearchSet &files, ModelCache &models, TextureCache &textures, const std::string &car);
    /** Whether load() found one. */
    bool loaded() const { return _ok; }
    /**
     * `eye`, `forward`, `up`: the camera; `aspect` the view's width over
     * its height (it's made for 4:3, and stretched across to fill a wider
     * view); `damage` 0..1. Clears the depth first, so it's over everything
     * drawn.
     */
    void draw(gfx::Renderer3D &renderer, ModelCache &models, const CarSim &sim, float damage, const Vec3 &eye,
              const Vec3 &forward, const Vec3 &up, float aspect, const SceneLight &light) const;

private:
    std::string _model;
    DashSetup _setup;
    Mtx _speed{};
    Mtx _tach{};
    Mtx _damage{};
    Mtx _wheel{};
    Mtx _gear{};
    // the gear indicator's: texture/d, n and r
    gfx::TextureId _drive = 0;
    gfx::TextureId _neutral = 0;
    gfx::TextureId _reverse = 0;
    bool _ok = false;
};

} // namespace reluva::age
