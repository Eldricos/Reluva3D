// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/odf.h"
#include "graphics/gfx3d.h"

#include <array>
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
    /** A unit's systems, in the order of the info panel's icons. */
    enum System { kShieldGenerator, kEngines, kWeapons, kLifeSupport, kSensors, kSystems };
    /** One of them: its hit points now and most, how much of a hull hit lands on it, and its repairs. */
    struct SystemState {
        float hitPoints = 0;
        float maxHitPoints = 0; // 0: the unit hasn't it
        float hitPercent = 0;   // of the hits past the shields, which land on it
        float repairTime = 0;   // seconds from none to whole
        float crewLoss = 0;     // of its crew, in percent, as it's hit
        bool out() const { return maxHitPoints > 0 && hitPoints <= 0; }
    };
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
        float crew = 0;
        float maxCrew = 0;
        float lifeSupportLoss = 0; // of its crew, in percent a second, while its life support is out
        std::array<SystemState, kSystems> systems{};
        bool alive() const { return hull > 0; }
        /** Whether any of its systems is out (its fires burn). */
        bool damaged() const;
    };
    /**
     * A unit's, from its definition: shields and hull both its maxHealth,
     * shields back at its shieldRate; its special energy full.
     */
    static Vitals vitalsOf(const Odf &unit);

    /** One of a unit's weapons that fires at what it attacks (not its special weapons). */
    struct Weapon {
        float range = 0;      // the weapon's
        double delay = 0;     // shotDelay, seconds between its shots
        float damage = 0;     // its ordnance's damageBase
        float variance = 0;   // and damageVariance, either way
        float speed = 0;      // the shot's, units a second
        std::string sprite;   // the shot's sprite: "pphaser"
        float hitChance = 1;  // of a shot hitting
        float shieldCrew = 0; // its ordnance's shieldCrewModifier: crew lost a point of damage on the shields
        float hullCrew = 0;   // and hullCrewModifier, a point on the hull
        double ready = 0;     // seconds until it can fire again
    };
    /** A unit's weapons from weapon1 on, their ordnance read through `loader`; those with `special` left out. */
    static std::vector<Weapon> weaponsOf(const Odf &unit, const Odf::Loader &loader);

    /** What a hit takes besides hit points: crew a point of damage, on the shields and on the hull. */
    struct CrewCost {
        float shields = 0;
        float hull = 0;
    };
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
        float accel = 0;  // shotAccel: how fast it gets up to its speed, units a second each second
        float detect = 0; // a mine's detectRadius: how far it sees a ship to go for
        double life = 0;  // its shot's lifeSpan, seconds
        bool needTarget = false;
        float radius = 0;   // the weapon's: how far a self-destruct reaches
        double timer = 0;   // the weapon's timer (a self-destruct's countdown), or detectTimer
        float modifier = 0; // regenModifier, or its ordnance's specialValue
        int maxTargets = 0; // an area missile's, within its range
        CrewCost crew{};    // what its hits take of the crew
        double ready = 0;   // seconds until it can fire again
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
        gfx::Vec3 from{};  // where it was fired from
        int slot = 0;      // whose it is: the player's slot
        CrewCost crew{};   // what its hits take of the crew
        bool miss = false; // it flies past, by its weapon's hit chance
        int shooter = -1;  // the unit that fired it, if any (a reflector sends it back)
    };

    /**
     * Damage taken: by the shields while they last, then by the hull, twice
     * as much; crew with it, by `crew`; and what gets past the shields, by
     * `roll` (0 to 1), lands on one of the systems by their hit percents,
     * taking its hit points and its share of the crew.
     */
    static void hit(Vitals &vitals, float damage, const CrewCost &crew, float roll);
    /** Damage taken without crew or systems: the shields', then the hull's. */
    static void hit(Vitals &vitals, float damage) { hit(vitals, damage, CrewCost{}, 1); }
    /**
     * Over `seconds`, while the unit lives: shields and special energy
     * regained up to their most, systems repaired over their repair time,
     * and crew lost while its life support is out.
     */
    static void regenerate(Vitals &vitals, double seconds);
    /** A shot's damage: its base, give or take its variance, by `roll` (0 to 1). */
    static float damageOf(const Weapon &weapon, float roll);
};

} // namespace reluva::storm3d
