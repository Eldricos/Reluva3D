// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/odf.h"
#include "graphics/gfx3d.h"

#include <array>
#include <map>
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
        float offline = 0;      // seconds it's knocked out for, by a special weapon (TimedDisable)
        bool out() const { return maxHitPoints > 0 && hitPoints <= 0; }
        bool working() const { return !out() && offline <= 0; }
    };
    /** A unit's shields, its maxHealth (the original has no hull of points), its crew and systems. */
    struct Vitals {
        float shields = 0;
        float maxShields = 0;
        bool destroyed = false; // by a critical hit
        float heldShields = -1; // shields to give back when its generator comes back on, if a disruptor took them
        float carry = 0;        // damage under a whole point, kept for its next hit
        float shieldRate = 0;   // shields regained a second
        float energy = 0;       // special energy, for the special weapons
        float maxEnergy = 0;    // maxSpecialEnergy
        float energyRate = 0;   // specialEnergyRate, regained a second
        float crew = 0;
        float maxCrew = 0;
        float lifeSupportLoss = 0; // of its crew, in percent a second, while its life support is out
        // The share of the hits past the shields after the systems', on the
        // hull alone (hullHitPercent); what's left of the hundred is
        // critical, destroying it, whatever criticalHitPercent says.
        float hullHitPercent = 0;
        // Its race's (fed.odf, kling.odf...) or its own: how its crew
        // boards and repairs, and the shares of its crew below which its
        // repairs take repairYellow and repairRed times as long.
        float boarding = 1;
        float repairStrength = 1;
        float crewYellow = 0;
        float crewRed = 0;
        float repairYellow = 1;
        float repairRed = 1;
        float crewRetreat = 0; // crewRetreatRatio: the share of its crew its transporters keep aboard
        std::array<SystemState, kSystems> systems{};
        bool alive() const { return !destroyed; }
        /** Without crew: its systems off, it neither moves nor fires, and its player is out with only such (0x463410). */
        bool derelict() const { return maxCrew > 0 && crew <= 0; }
        /** Whether any of its systems is out (its fires burn). */
        bool damaged() const;
    };
    /**
     * A unit's, from its definition: its shields its maxHealth, back at its
     * shieldRate; its special energy full.
     */
    static Vitals vitalsOf(const Odf &unit);
    /** The race's definition a unit's `race` names ("federation": fed.odf); empty if none. */
    static std::string raceFileOf(const std::string &race);
    /** Its crew's strengths and repair rates from its race's definition, where its own doesn't give them. */
    static void raceInto(Vitals &vitals, const Odf &unit, const Odf &race);
    /** How a boarding party fares: what's left of it if it took the ship, or of the crew if it didn't. */
    struct Boarding {
        bool taken = false;
        float left = 0;
    };
    /**
     * A party of `party` crew, boarding at `boarding` strength, against
     * `crew` defending at `defending`: each side's crew times its strength,
     * the bigger winning with the difference, in its own crew.
     */
    static Boarding board(float party, float boarding, float crew, float defending);

    /** One of a unit's weapons that fires at what it attacks (not its special weapons). */
    struct Weapon {
        float range = 0;     // the weapon's
        double delay = 0;    // shotDelay, seconds between its shots
        float damage = 0;    // its ordnance's damageBase
        float variance = 0;  // and damageVariance, either way
        float speed = 0;     // the shot's, units a second
        std::string sprite;  // the shot's sprite: "pphaser"
        float hitChance = 1; // of a shot hitting
        // Added to it by how the shooter moves (stopToHitModifier,
        // slowToHitModifier, warpToHitModifier) and the target
        // (toHitStopModifier...), by Pace.
        std::array<float, 3> shooterModifier{};
        std::array<float, 3> targetModifier{};
        float shieldCrew = 0; // its ordnance's shieldCrewModifier: crew lost a point of damage on the shields
        float hullCrew = 0;   // and hullCrewModifier, a point on the hull
        float threshold = 0;  // and damageThreshold, which a hit on the shields must pass to cost crew
        double ready = 0;     // seconds until it can fire again
    };
    /** How a unit moves, for a shot's chance: stopped, slower than impulse speed, or faster. */
    enum class Pace { kStopped, kSlow, kWarp };
    /**
     * A weapon's chance of hitting, its hitChance with the modifiers for
     * the shooter's pace and the target's added, as its definition's
     * comments say ("changes the percent chance (from hitChance)").
     */
    static float hitChanceOf(const Weapon &w, Pace shooter, Pace target);
    /** A weapon's definition's six modifiers into `w`. */
    static void readModifiers(const Odf &weapon, Weapon &w);
    /** A unit's weapons from weapon1 on, their ordnance read through `loader`; those with `special` left out. */
    static std::vector<Weapon> weaponsOf(const Odf &unit, const Odf::Loader &loader);

    /**
     * What a hit takes besides hit points: the percent of the crew lost a
     * point of damage on the shields past `threshold`, and a point on the
     * hull (the ordnance's shieldCrewModifier, hullCrewModifier and
     * damageThreshold).
     */
    struct CrewCost {
        float shields = 0;
        float hull = 0;
        float threshold = 0;
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
        // The rest of its weapon's and ordnance's numbers that its kind
        // reads (chainRadius, repulsionForce, hitCondition...), by key.
        std::map<std::string, float> values;
        float value(const std::string &key, float fallback) const {
            auto it = values.find(key);
            return it == values.end() ? fallback : it->second;
        }
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
     * Damage taken, as the game takes it (0x461c60): all on the shields,
     * crew lost by `crew.shields` past `crew.threshold` while they're up;
     * what they hadn't lands by `roll` (0 to 1, as a whole 0 to 99) and
     * the hit percents on a system, taking whole points and, brought to
     * nothing, its crewLoss; on the hull alone, taking crew by `crew.hull`;
     * or, past them all, critically, destroying it. The crew lost is a
     * share of those aboard; under one, the unit is derelict.
     */
    static void hit(Vitals &vitals, float damage, const CrewCost &crew, float roll);
    /** Damage taken by the shields alone, without crew, systems or a critical. */
    static void hit(Vitals &vitals, float damage) { vitals.shields = std::max(0.0f, vitals.shields - damage); }
    /**
     * Over `seconds`, while the unit lives: shields and special energy
     * regained up to their most, systems repaired over their repair time,
     * and crew lost while its life support is out.
     */
    static void regenerate(Vitals &vitals, double seconds);
    /**
     * A system knocked out for `seconds`, or longer if it already is, as
     * the program's TimedDisable (0x556e80); `dropShields` with the shield
     * generator also takes the shields, given back as it comes on unless
     * it's been damaged meanwhile (its kind 3).
     */
    static void disable(Vitals &vitals, System system, float seconds, bool dropShields = false);
    /** A shot's damage: its base, give or take its variance, by `roll` (0 to 1). */
    static float damageOf(const Weapon &weapon, float roll);
};

} // namespace reluva::storm3d
