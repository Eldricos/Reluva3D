// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/combat.h"

#include "common/text.h"

#include <algorithm>
#include <map>

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
    v.maxEnergy = v.energy = float(unit.number("maxSpecialEnergy"));
    v.energyRate = float(unit.number("specialEnergyRate"));
    // It starts with its crewCost aboard, of its maximumCrew: the
    // original's starbase 800 of 1500.
    v.crew = float(unit.number("crewCost"));
    v.maxCrew = float(unit.number("maximumCrew", v.crew));
    v.lifeSupportLoss = float(unit.number("lifeSupportLoss"));
    v.hullHitPercent = float(unit.number("hullHitPercent"));
    v.criticalHitPercent = float(unit.number("criticalHitPercent"));
    static const char *const kNames[kSystems] = {"shieldGenerator", "engines", "weapons", "lifeSupport", "sensors"};
    for (size_t i = 0; i < kSystems; ++i) {
        const std::string name = kNames[i];
        SystemState &s = v.systems[i];
        s.maxHitPoints = s.hitPoints = float(unit.number(name + "HitPoints"));
        s.hitPercent = float(unit.number(name + "HitPercent"));
        s.repairTime = float(unit.number(name + "RepairTime"));
        s.crewLoss = float(unit.number(name + "CrewLoss"));
    }
    return v;
}

void Combat::readModifiers(const Odf &weapon, Weapon &w) {
    static constexpr const char *kPaces[3] = {"stop", "slow", "warp"};
    static constexpr const char *kTargets[3] = {"Stop", "Slow", "Warp"};
    for (size_t p = 0; p < 3; ++p) {
        w.shooterModifier[p] = float(weapon.number(std::string(kPaces[p]) + "ToHitModifier"));
        w.targetModifier[p] = float(weapon.number(std::string("toHit") + kTargets[p] + "Modifier"));
    }
}

float Combat::hitChanceOf(const Weapon &w, Pace shooter, Pace target) {
    return w.hitChance + w.shooterModifier[size_t(shooter)] + w.targetModifier[size_t(target)];
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
        w.hitChance = float(weapon.number("hitChance", 1));
        readModifiers(weapon, w);
        w.shieldCrew = float(ordnance.number("shieldCrewModifier"));
        w.hullCrew = float(ordnance.number("hullCrewModifier"));
        w.threshold = float(ordnance.number("damageThreshold"));
        weapons.push_back(w);
    }
    return weapons;
}

std::vector<Combat::Special> Combat::specialsOf(const Odf &unit, const Odf::Loader &loader) {
    std::vector<Special> specials;
    for (int i = 1;; ++i) {
        const std::string name = unit.string("weapon" + std::to_string(i));
        if (name.empty())
            break;
        Odf weapon;
        if (weapon.load(name + ".odf", loader) || weapon.number("special") <= 0)
            continue;
        Odf ordnance;
        const bool shot =
            !weapon.string("ordName").empty() && !ordnance.load(weapon.string("ordName") + ".odf", loader);
        Special s;
        s.weapon = text::toLower(name);
        s.kind = text::toLower(weapon.string("classLabel"));
        s.button = "b_" + s.weapon;
        s.research = researchKeyOf(weapon.string("tooltip"));
        s.range = float(weapon.number("range"));
        s.delay = weapon.number("shotDelay");
        s.needTarget = weapon.number("needTarget", 1) != 0;
        s.radius = float(weapon.number("radius"));
        s.timer = weapon.number("timer", weapon.number("detectTimer"));
        s.modifier = float(weapon.number("regenModifier"));
        s.maxTargets = int(weapon.number("maxTargets"));
        if (shot) {
            s.ordnance = text::toLower(ordnance.string("classLabel"));
            s.energy = float(ordnance.number("specialEnergyCost"));
            s.damage = float(ordnance.number("damageBase"));
            s.variance = float(ordnance.number("damageVariance"));
            s.speed = float(ordnance.number("shotSpeed"));
            s.detect = float(ordnance.number("detectRadius"));
            s.accel = float(ordnance.number("shotAccel"));
            if (s.modifier <= 0)
                s.modifier = float(ordnance.number("specialValue"));
            s.crew = {float(ordnance.number("shieldCrewModifier")), float(ordnance.number("hullCrewModifier")),
                      float(ordnance.number("damageThreshold"))};
            s.life = ordnance.number("lifeSpan");
            s.sprite = ordnance.string("Sprite");
            s.model = ordnance.string("shotGeometry");
            if (text::hasSuffixIgnoreCase(s.model, ".sod"))
                s.model.resize(s.model.size() - 4);
        }
        // Some keep their cost on the weapon itself (the cloaks).
        if (s.energy <= 0)
            s.energy = float(weapon.number("specialEnergyCost"));
        specials.push_back(s);
    }
    return specials;
}

std::string Combat::researchKeyOf(const std::string &tooltip) {
    // "^" marks the hotkey's letter; "(^F^1)" is the key that fires it.
    std::string out;
    int depth = 0;
    for (char c : tooltip) {
        if (c == '(')
            ++depth;
        else if (c == ')')
            depth = std::max(0, depth - 1);
        else if (depth == 0 && c != '^')
            out += c;
    }
    // The spaces left round the key, as one.
    std::string squeezed;
    for (char c : out)
        if (c != ' ' || (!squeezed.empty() && squeezed.back() != ' '))
            squeezed += c;
    while (!squeezed.empty() && squeezed.back() == ' ')
        squeezed.pop_back();
    return text::toLower(squeezed);
}

std::string Combat::raceFileOf(const std::string &race) {
    static const std::map<std::string, std::string> kFiles = {
        {"federation", "fed"}, {"klingon", "kling"}, {"romulan", "rom"}, {"borg", "borg"}};
    auto it = kFiles.find(text::toLower(race));
    return it == kFiles.end() ? std::string() : it->second;
}

void Combat::raceInto(Vitals &vitals, const Odf &unit, const Odf &race) {
    auto value = [&](const char *key, double fallback) { return float(unit.number(key, race.number(key, fallback))); };
    vitals.boarding = value("boardingStrength", 1);
    vitals.repairStrength = value("repairStrength", 1);
    vitals.crewYellow = value("crewYellowStatus", 0);
    vitals.crewRed = value("crewRedStatus", 0);
    vitals.repairYellow = value("repairYellow", 1);
    vitals.repairRed = value("repairRed", 1);
}

Combat::Boarding Combat::board(float party, float boarding, float crew, float defending) {
    const float attack = party * boarding;
    const float defence = crew * defending;
    if (attack > defence)
        return {true, boarding > 0 ? (attack - defence) / boarding : 0};
    return {false, defending > 0 ? (defence - attack) / defending : 0};
}

bool Combat::Vitals::damaged() const {
    for (const SystemState &s : systems)
        if (s.out())
            return true;
    return false;
}

void Combat::hit(Vitals &vitals, float damage, const CrewCost &crew, float roll) {
    // Each loss a percent of the crew aboard, and a whole one more, at most.
    auto lose = [&vitals](float percent, float by) {
        vitals.crew = std::max(0.0f, vitals.crew - (vitals.crew * percent * by / 100 + std::min(by, 1.0f)));
    };
    // The ordnance's own words: its threshold "must be exceeded to cause
    // crew casualties prior to shield exhaustion", what exceeds it times
    // its shieldCrewModifier the percent lost (0x461d46).
    if (vitals.shields > 0 && crew.threshold > 0 && damage > crew.threshold)
        lose(crew.shields, damage - crew.threshold);
    const float onShields = std::min(vitals.shields, damage);
    vitals.shields -= onShields;
    const float past = damage - onShields;
    if (past <= 0)
        return;
    vitals.hull = std::max(0.0f, vitals.hull - past * kHullFactor);
    // It lands by the hit percents: on a system, the hull alone, or
    // critically (0x461930, 0x461e4c); the rest of the hundred, nowhere.
    float at = std::clamp(roll, 0.0f, 1.0f) * 100;
    for (SystemState &s : vitals.systems) {
        if (s.maxHitPoints <= 0 || at >= s.hitPercent) {
            at -= s.hitPercent;
            continue;
        }
        const bool was = s.out();
        s.hitPoints = std::max(0.0f, s.hitPoints - past);
        if (!was && s.out())
            lose(s.crewLoss, 1);
        return;
    }
    if (at < vitals.hullHitPercent) {
        lose(crew.hull, past);
        return;
    }
    at -= vitals.hullHitPercent;
    // A critical hit of a whole point or more destroys it.
    if (at < vitals.criticalHitPercent && past >= 1)
        vitals.hull = 0;
}

void Combat::regenerate(Vitals &vitals, double seconds) {
    if (!vitals.alive())
        return;
    // Without its shield generator, its shields don't come back.
    if (!vitals.systems[kShieldGenerator].out())
        vitals.shields = std::min(vitals.maxShields, vitals.shields + float(vitals.shieldRate * seconds));
    vitals.energy = std::min(vitals.maxEnergy, vitals.energy + float(vitals.energyRate * seconds));
    if (vitals.systems[kLifeSupport].out())
        vitals.crew = std::max(0.0f, vitals.crew - vitals.crew * vitals.lifeSupportLoss / 100 * float(seconds));
    // Repairs, by its crew: faster by its race's repairStrength, slower by
    // repairYellow or repairRed with its crew below those shares, none
    // without crew *(inferred: the keys' names)*.
    const float share = vitals.maxCrew > 0 ? vitals.crew / vitals.maxCrew : 1;
    const float slower = share < vitals.crewRed      ? vitals.repairRed
                         : share < vitals.crewYellow ? vitals.repairYellow
                                                     : 1;
    const float pace = vitals.crew > 0 || vitals.maxCrew <= 0 ? vitals.repairStrength / std::max(0.01f, slower) : 0;
    for (SystemState &s : vitals.systems)
        if (s.maxHitPoints > 0 && s.repairTime > 0)
            s.hitPoints = std::min(s.maxHitPoints, s.hitPoints + s.maxHitPoints / s.repairTime * pace * float(seconds));
}

float Combat::damageOf(const Weapon &weapon, float roll) {
    return weapon.damage + weapon.variance * (2 * std::clamp(roll, 0.0f, 1.0f) - 1);
}

} // namespace reluva::storm3d
