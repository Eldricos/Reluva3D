// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/combat.h"

#include "common/text.h"

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
    v.maxEnergy = v.energy = float(unit.number("maxSpecialEnergy"));
    v.energyRate = float(unit.number("specialEnergyRate"));
    v.maxCrew = v.crew = float(unit.number("maximumCrew", unit.number("crewCost")));
    v.lifeSupportLoss = float(unit.number("lifeSupportLoss"));
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
        w.shieldCrew = float(ordnance.number("shieldCrewModifier"));
        w.hullCrew = float(ordnance.number("hullCrewModifier"));
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
            s.crew = {float(ordnance.number("shieldCrewModifier")), float(ordnance.number("hullCrewModifier"))};
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

bool Combat::Vitals::damaged() const {
    for (const SystemState &s : systems)
        if (s.out())
            return true;
    return false;
}

void Combat::hit(Vitals &vitals, float damage, const CrewCost &crew, float roll) {
    const float onShields = std::min(vitals.shields, damage);
    vitals.shields -= onShields;
    const float onHull = (damage - onShields) * kHullFactor;
    vitals.hull = std::max(0.0f, vitals.hull - onHull);
    float lost = onShields * crew.shields + onHull * crew.hull;
    // What gets past the shields lands on a system by their hit percents,
    // the rest of the hundred on the hull alone.
    if (onHull > 0) {
        float at = std::clamp(roll, 0.0f, 1.0f) * 100;
        for (SystemState &s : vitals.systems) {
            if (s.maxHitPoints <= 0 || at >= s.hitPercent) {
                at -= s.hitPercent;
                continue;
            }
            s.hitPoints = std::max(0.0f, s.hitPoints - onHull);
            lost += vitals.crew * s.crewLoss / 100;
            break;
        }
    }
    vitals.crew = std::max(0.0f, vitals.crew - lost);
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
    for (SystemState &s : vitals.systems)
        if (s.maxHitPoints > 0 && s.repairTime > 0)
            s.hitPoints = std::min(s.maxHitPoints, s.hitPoints + s.maxHitPoints / s.repairTime * float(seconds));
}

float Combat::damageOf(const Weapon &weapon, float roll) {
    return weapon.damage + weapon.variance * (2 * std::clamp(roll, 0.0f, 1.0f) - 1);
}

} // namespace reluva::storm3d
