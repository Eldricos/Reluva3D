// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "engines/age/car.h"
#include "engines/age/render_cache.h"

#include <string>

namespace reluva::age {

// Where a car's dashboard goes in front of the in-car camera
// (tune/<car>_dash.asnode): the dash, roof and steering wheel's places
// (camera space: x right, y up, z back), the needles' and wheel's pivots
// and how far each turns. See docs/engines/age-driving.md.
struct DashSetup {
    Vec3 dashPos{}, roofPos{}, wheelPos{};
    Vec3 speedPivot{}, tachPivot{}, damagePivot{}, wheelPivot{}, gearPivot{}; // the *PivotOffset numbers
    float wheelFact = 0.9f;
    float rpmRotMin = 0, rpmRotMax = 3, speedRotMin = 0, speedRotMax = 3, damageRotMin = 0, damageRotMax = 3;
};
Error loadDashSetup(const SearchSet& files, const std::string& car, DashSetup& out);

// The dashboard (geometry/<car>_dash.pkg: flat parts, dash, roof, wheel,
// speed_needle, tach_needle, damage_needle, gear_indicator), drawn over
// the view from inside the car.
class Dashboard {
public:
    // False if the car has none.
    bool load(const SearchSet& files, ModelCache& models, TextureCache& textures, const std::string& car);
    bool loaded() const { return ok_; }
    // `eye`, `forward`, `up`: the camera; `aspect` the view's width over
    // its height (it's made for 4:3, and stretched across to fill a wider view);
    // `damage` 0..1. Clears the depth first, so it's over everything drawn.
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const CarSim& sim, float damage, const Vec3& eye,
              const Vec3& forward, const Vec3& up, float aspect, const SceneLight& light) const;

private:
    std::string model_;
    DashSetup setup_;
    Mtx speed_{}, tach_{}, damage_{}, wheel_{}, gear_{};
    gfx::TextureId drive_ = 0, neutral_ = 0, reverse_ = 0; // the gear indicator's: texture/d, n and r
    bool ok_ = false;
};

} // namespace reluva::age
