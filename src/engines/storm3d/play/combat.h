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
        float energy = 0;     // special energy, for the special weapons
        float maxEnergy = 0;  // maxSpecialEnergy
        float energyRate = 0; // specialEnergyRate, regained a second
        bool alive() const { return hull > 0; }
    };
    /**
     * A unit's, from its definition: shields and hull both its maxHealth,
     * shields back at its shieldRate; its special energy full.
     */
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

    /** One of a unit's special weapons (`special` 1), fired by its button. */
    struct Special {
        std::string weapon;   // its definition: "gamine"
        std::string kind;     // its classLabel: "minelayer", "cannon"
        std::string ordnance; // its ordnance's classLabel: "antimine"
        std::string button;   // the rail's sprite: "b_gamine"
        std::string research; // researchKeyOf its tooltip, which its pod's matches
        std::string model;    // its shot's (shotGeometry, without ".sod"): "wantmine"
        std::string sprite;   // or its sprite
        float range = 0;
        double delay = 0;
        float energy = 0; // its ordnance's specialEnergyCost
        float damage = 0;
        float variance = 0;
        float speed = 0;
        float detect = 0; // a mine's detectRadius: how far it sees a ship to go for
        double life = 0;  // its shot's lifeSpan, seconds
        bool needTarget = false;
        double ready = 0; // seconds until it can fire again
    };
    /** A unit's special weapons, their ordnance read through `loader`. */
    static std::vector<Special> specialsOf(const Odf &unit, const Odf::Loader &loader);
    /**
     * A tooltip's words without its hotkey marks and key ("Antimatter
     * Mines (^F^1) - Defiant": "Antimatter Mines - Defiant"), by which a
     * research pod names the special weapon it gives.
     */
    static std::string researchKeyOf(const std::string &tooltip);

    /** A shot on its way. */
    struct Shot {
        size_t target = 0;
        gfx::Vec3 at{};
        float speed = 0;
        float damage = 0;
        std::string sprite;
        gfx::Vec3 from{}; // where it was fired from
        int slot = 0;     // whose it is: the player's slot
    };

    /** Damage taken: by the shields while they last, then by the hull, twice as much. */
    static void hit(Vitals &vitals, float damage);
    /** Shields and special energy regained over `seconds`, up to their most, while the unit lives. */
    static void regenerate(Vitals &vitals, double seconds);
    /** A shot's damage: its base, give or take its variance, by `roll` (0 to 1). */
    static float damageOf(const Weapon &weapon, float roll);
};

} // namespace reluva::storm3d
