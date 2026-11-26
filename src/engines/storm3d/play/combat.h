// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/odf.h"
#include "graphics/gfx3d.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * How units fight: their weapons' shots, and the shields and hull they
 * hit, by their definitions and as the original's starbase fought one
 * of its construction ships. See
 * docs/engines/storm3d/games/armada/systems/combat.md.
 */
class Combat {
public:
    /** A unit's shields and hull, in its maxHealth's points. */
    struct Vitals {
        float shields = 0;
        float maxShields = 0;
        float hull = 0;
        float maxHull = 0;
        float shieldRate = 0; // shields regained a second
        bool alive() const { return hull > 0; }
    };
    /** A unit's, from its definition: shields and hull both its maxHealth, shields back at its shieldRate. */
    static Vitals vitalsOf(const Odf &unit);

    /** One of a unit's weapons that fires at what it attacks (not its special weapons). */
    struct Weapon {
        float range = 0;    // the weapon's
        double delay = 0;   // shotDelay, seconds between its shots
        float damage = 0;   // its ordnance's damageBase
        float variance = 0; // and damageVariance, either way
        float speed = 0;    // the shot's, units a second
        std::string sprite; // the shot's sprite: "pphaser"
        double ready = 0;   // seconds until it can fire again
    };
    /** A unit's weapons from weapon1 on, their ordnance read through `loader`; those with `special` left out. */
    static std::vector<Weapon> weaponsOf(const Odf &unit, const Odf::Loader &loader);

    /** A shot on its way. */
    struct Shot {
        size_t target = 0;
        gfx::Vec3 at{};
        float speed = 0;
        float damage = 0;
        std::string sprite;
        gfx::Vec3 from{}; // where it was fired from
    };

    /** Damage taken: by the shields while they last, then by the hull, twice as much. */
    static void hit(Vitals &vitals, float damage);
    /** Shields regained over `seconds`, up to their most, while the unit lives. */
    static void regenerate(Vitals &vitals, double seconds);
    /** A shot's damage: its base, give or take its variance, by `roll` (0 to 1). */
    static float damageOf(const Weapon &weapon, float roll);
};

} // namespace reluva::storm3d
