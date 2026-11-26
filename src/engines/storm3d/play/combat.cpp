// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/combat.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

// The hull takes more than the shields do: the original's construction
// ship lasted the volley after its shields went and not the next, 277
// and 437 through them, its hull 700: from 1.6 to 2.5 times; twice is
// a guess within that.
constexpr float kHullFactor = 2;

} // namespace

Combat::Vitals Combat::vitalsOf(const Odf &unit) {
    // Its shields' bar fell a quarter a volley of eight 22s, and came back
    // 3.5 a second, its shieldRate: 700 of them, its maxHealth.
    Vitals v;
    v.maxShields = v.shields = float(unit.number("maxHealth"));
    v.maxHull = v.hull = float(unit.number("maxHealth"));
    v.shieldRate = float(unit.number("shieldRate"));
    return v;
}

std::vector<Combat::Weapon> Combat::weaponsOf(const Odf &unit, const Odf::Loader &loader) {
    std::vector<Weapon> weapons;
    for (int i = 1;; ++i) {
        const std::string name = unit.string("weapon" + std::to_string(i));
        if (name.empty())
            break;
        Odf weapon;
        if (weapon.load(name + ".odf", loader) || weapon.number("special") > 0)
            continue;
        Odf ordnance;
        if (ordnance.load(weapon.string("ordName") + ".odf", loader))
            continue;
        Weapon w;
        w.range = float(weapon.number("range"));
        w.delay = weapon.number("shotDelay");
        w.damage = float(ordnance.number("damageBase"));
        w.variance = float(ordnance.number("damageVariance"));
        w.speed = float(ordnance.number("shotSpeed"));
        w.sprite = ordnance.string("Sprite");
        weapons.push_back(w);
    }
    return weapons;
}

void Combat::hit(Vitals &vitals, float damage) {
    const float onShields = std::min(vitals.shields, damage);
    vitals.shields -= onShields;
    vitals.hull = std::max(0.0f, vitals.hull - (damage - onShields) * kHullFactor);
}

void Combat::regenerate(Vitals &vitals, double seconds) {
    if (vitals.alive())
        vitals.shields = std::min(vitals.maxShields, vitals.shields + float(vitals.shieldRate * seconds));
}

float Combat::damageOf(const Weapon &weapon, float roll) {
    return weapon.damage + weapon.variance * (2 * std::clamp(roll, 0.0f, 1.0f) - 1);
}

} // namespace reluva::storm3d
