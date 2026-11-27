// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"

#include <algorithm>
#include <cmath>

// The special weapons past the mines, the plain shots and the cloaks: each
// made from its definition's description and numbers, what it does beyond
// them guessed. See docs/engines/storm3d/games/armada/systems/combat.md.

namespace reluva::storm3d {

namespace {

float distanceOf(const gfx::Vec3 &a, const gfx::Vec3 &b) {
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

// How long an effect lasts: its ordnance's specialValue in seconds where it
// gives one (Engine Overload's 25, the Sensor Jammer's 20), else `fallback`.
double lastingOf(const Combat::Special &special, double fallback) {
    return special.modifier > 0 ? double(special.modifier) : fallback;
}

} // namespace

bool MissionView::madeKind(const Combat::Special &special) {
    // Left out: those the files mark unfinished ("THIS IS A BUG", "WRITE
    // ME"), and the Omega particle's tractor beam, a mission's.
    static const char *const kOrdnance[] = {
        "blackholecreator", "borgbore",       "chainpulsar",      "deathchant",          "esconverter",
        "fedrepairteam",    "gravmine",       "holoemitter",      "klingoncommandoteam", "manheimeffect",
        "nanitesordnance",  "psychonicblast", "override",         "polerontorpedo",      "posijammer",
        "remodulation",     "rfield",         "romulanspy",       "repulsionwave",       "shieldinv",
        "temporal",         "utribeam",       "warpcoreoverload", "ionstorm_tp",         "transwarpdrive"};
    if (special.kind == "wormholegen" || special.kind == "reflectweapon" || special.kind == "pointdefenselaser")
        return true;
    if (special.weapon == "gtracbm")
        return true;
    for (const char *o : kOrdnance)
        if (special.ordnance == o)
            return true;
    return false;
}

bool MissionView::wantsPoint(const Combat::Special &special) {
    return special.kind == "transwarpconduit" || special.kind == "wormholegen";
}

std::vector<int> MissionView::obeying(const std::vector<int> &selection) const {
    std::vector<int> out;
    for (int i : selection)
        if (i < 0 || size_t(i) >= _effects.size() || !active(_effects[size_t(i)].scrambled))
            out.push_back(i);
    return out;
}

std::vector<size_t> MissionView::areaTargetsOf(size_t unit, const Combat::Special &special) const {
    // hitCondition 2, the units of others; 1, its own side's; 0, any; the
    // gap between them, less both their sizes, against the range squared,
    // as the program compares it (0x4ff55a), so all but the whole map.
    const std::vector<MapScene::Unit> &units = _scene.units();
    const int condition = int(special.value("hitCondition", 0));
    const float reach = special.range * special.range;
    const size_t most = size_t(std::max(1, special.maxTargets));
    std::vector<size_t> out;
    for (size_t j = 0; j < units.size() && j < _dead.size() && out.size() < most; ++j) {
        if (_dead[j] || j == unit)
            continue;
        const bool own = units[j].slot == units[unit].slot;
        if ((condition == 2 && own) || (condition == 1 && !own))
            continue;
        const float gap =
            distanceOf(units[j].middle(), units[unit].middle()) - units[j].radius() - units[unit].radius();
        if (gap <= reach)
            out.push_back(j);
    }
    return out;
}

bool MissionView::fireMore(size_t unit, Combat::Special &special, int target) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const double now = double(_last) / 1000;
    const std::string &shot = special.ordnance;
    const int slot = units[unit].slot;
    const gfx::Vec3 from = units[unit].middle();
    const bool aimed = target >= 0 && size_t(target) < units.size() && !_dead[size_t(target)];
    // The units of others, or of its own side, within `reach` of a point.
    auto near = [&](const gfx::Vec3 &at, float reach, bool others) {
        std::vector<size_t> out;
        for (size_t j = 0; j < units.size() && j < _dead.size(); ++j)
            if (!_dead[j] && (units[j].slot != slot) == others && distanceOf(units[j].middle(), at) <= reach)
                out.push_back(j);
        return out;
    };
    const float reach = special.range > 0 ? special.range : 400;

    // Beams, on until pressed again, or until their own ends (ToggleWeapon,
    // 0x502280): their cost once to turn on, and a second while they run
    // where their class charges it; their reach the gap between the two,
    // less their sizes, against the weapon's range, and their work their
    // class's.
    if (special.kind == "toggleweapon") {
        if (std::erase_if(_holds, [unit](const Hold &x) { return x.unit == unit; }) > 0 && shot != "blackholecreator")
            return true;
        if (!aimed || size_t(target) == unit)
            return true;
        _vitals[unit].energy -= special.energy;
        Hold h{unit, size_t(target), now + 1e9, 0, reach};
        if (shot == "holding_bm") {
            // Pins another's unit, its engines out while held, for its cost a
            // second; lost past 1.33 times its range; a party of boardingSize
            // after boardingDelay, then each boardingInterval (0x4d7170).
            if (units[size_t(target)].slot == slot)
                return true;
            h.kind = "hold";
            h.cost = special.energy;
            h.range = reach * 1.33f;
            h.boardSize = special.value("boardingSize", 0);
            h.boardEvery = special.value("boardingInterval", 0);
            if (h.boardSize > 0)
                h.boardAt = now + special.value("boardingDelay", 0);
        } else if (shot == "tractor_bm") {
            // A ship, not a starbase, whose engines are out, that's derelict
            // or slowed, pulled to its targetRange from the holder (0x4e1c20).
            const Combat::Vitals &v = _vitals[size_t(target)];
            if (!_moves[size_t(target)] || _classes[size_t(target)] == "starbase" ||
                (v.systems[Combat::kEngines].working() && !v.derelict() && !active(_effects[size_t(target)].slowed)))
                return true;
            h.kind = "tow";
            h.pull = special.value("targetRange", 60);
            h.range = 1e9f;
        } else if (shot == "shieldinv") {
            // Its drainRate of the target's shields a second to the holder's,
            // for its cost a second; lost past 1.5 times its range (0x4dfe40).
            h.kind = "drainShields";
            h.rate = special.value("drainRate", 10);
            h.cost = special.energy;
            h.range = reach * 1.5f;
        } else if (shot == "esconverter") {
            // Its specialValue of the holder's special energy a second into
            // the target's shields, till they're whole (0x4d6020).
            h.kind = "giveShields";
            h.rate = special.modifier > 0 ? special.modifier : 50;
            h.range = reach * 1.5f;
        } else if (shot == "borgbore") {
            // Its specialValue of crew a second from another's unit, for its
            // cost a second, within its attackRange (0x4d3be0).
            if (units[size_t(target)].slot == slot)
                return true;
            h.kind = "assimilate";
            h.rate = special.modifier > 0 ? special.modifier : 5;
            h.cost = special.energy;
            h.range = special.value("attackRange", reach);
        } else if (shot == "blackholecreator") {
            // The Spacial Rift: the ship gives itself up, and a rift opens
            // where it was, growing from initialScale to maxScale times its
            // radius over expansionTime, staying timeAtMaxSize, shrinking
            // over retractionTime (0x4d32b0).
            Field rift{"rift", from, slot, now, now, 0, 0, special.damage, {}};
            rift.size = special.value("Radius", 20);
            rift.shape = {special.value("initialScale", 0.1f), special.value("maxScale", 10.1f),
                          special.value("expansionTime", 10), special.value("timeAtMaxSize", 5),
                          special.value("retractionTime", 5)};
            rift.until = now + double(rift.shape[2] + rift.shape[3] + rift.shape[4]);
            rift.variance = special.variance;
            rift.crew = special.crew;
            _fields.push_back(rift);
            destroy(unit, slot);
            return true;
        }
        _holds.push_back(h);
        return true;
    }
    // Reflection and point defence: on while the ship pays their cost a
    // second, off when pressed again or it can't (0x501870, 0x501510).
    if (special.kind == "reflectweapon" || special.kind == "pointdefenselaser") {
        const auto key = std::pair{unit, special.kind};
        if (!_running.erase(key))
            _running[key] = {special.energy, 0};
        return true;
    }
    _vitals[unit].energy -= special.energy;
    if (shot == "warpcoreoverload" || shot == "posijammer" || shot == "holoemitter") {
        // Fired at each of its area's targets: engines out for its
        // specialValue (0x4e2ea0), or sensors (0x4dc2f0); a hologram,
        // on another's unit but a starbase, takes which side it fights
        // for and stops its engines while it lasts (0x4d75a0).
        const float lasting = float(lastingOf(special, 20));
        for (size_t j : areaTargetsOf(unit, special)) {
            if (shot == "warpcoreoverload") {
                Combat::disable(_vitals[j], Combat::kEngines, lasting);
            } else if (shot == "posijammer") {
                Combat::disable(_vitals[j], Combat::kSensors, lasting);
            } else if (units[j].slot != slot && _classes[j] != "starbase" && !active(_effects[j].confused)) {
                _effects[j].confused = now + lasting;
                Combat::disable(_vitals[j], Combat::kEngines, lasting);
                _ships[j].goal.reset();
            }
        }
        return true;
    }
    if (shot == "deathchant") {
        // Its area's targets reload at its FireModifier (1.5) times the
        // pace for its specialValue (0x4d5980, TimedHyper).
        for (size_t j : areaTargetsOf(unit, special)) {
            _effects[j].fireFaster = now + lastingOf(special, 15);
            _effects[j].fireBy = special.value("FireModifier", 1.5f);
        }
        return true;
    }
    if (shot == "temporal") {
        // Everything of others within its Radius of the target, still.
        // Everything not of its side within its Radius (400) of where it's
        // aimed, paused for its lifeSpan (45) (0x4e13f0).
        if (!aimed)
            return true;
        for (size_t j : near(units[size_t(target)].middle(), special.value("Radius", 400), true))
            _effects[j].frozen = now + (special.life > 0 ? special.life : 45);
        return true;
    }
    if (shot == "ionstorm_tp") {
        if (aimed)
            _fields.push_back(
                {"ionstorm", units[size_t(target)].middle(), slot, now, now + special.life, 200, 0, 30, {}});
        return true;
    }
    if (shot == "repulsionwave") {
        _fields.push_back({"wave", from, slot, now, now + special.life, 0, 150, 200, {}});
        return true;
    }
    if (shot == "utribeam") {
        // Up to four of the nearest ships of others within range.
        std::vector<size_t> them = near(from, reach, true);
        std::sort(them.begin(), them.end(), [&](size_t a, size_t b) {
            return distanceOf(units[a].middle(), from) < distanceOf(units[b].middle(), from);
        });
        for (size_t n = 0; n < them.size() && n < 4; ++n)
            _shots.push_back({them[n], from, 400, 200, special.sprite, from, slot, special.crew, false, int(unit)});
        return true;
    }
    if (shot == "chainpulsar") {
        // Laid, not aimed: to the nearest unit not of its side within its
        // chainRadius of the ship, then on from each it hits to the nearest
        // not yet hit within that of it, up to maxHits; each hit its damage
        // and chainDamageModifier more for each so far (0x4d4710, 0x4d4900).
        const float chain = special.value("chainRadius", 300);
        const int most = int(special.value("maxHits", 6));
        const float more = special.value("chainDamageModifier", 25);
        std::set<size_t> struck;
        gfx::Vec3 at = from;
        for (int hits = 1; hits <= most; ++hits) {
            int next = -1;
            float best = chain;
            for (size_t j : near(at, chain, true))
                if (!struck.count(j) && distanceOf(units[j].middle(), at) <= best)
                    next = int(j), best = distanceOf(units[j].middle(), at);
            if (next < 0)
                break;
            const size_t j = size_t(next);
            struck.insert(j);
            _roll = _roll * 1664525u + 1013904223u;
            Combat::Weapon as;
            as.damage = special.damage;
            as.variance = special.variance;
            const float damage = Combat::damageOf(as, float(_roll >> 8) / float(1u << 24)) + float(hits) * more;
            _roll = _roll * 1664525u + 1013904223u;
            Combat::hit(_vitals[j], damage, special.crew, float(_roll >> 8) / float(1u << 24));
            if (!_vitals[j].alive() && !_dead[j])
                destroy(j, slot);
            at = units[j].middle();
        }
        return true;
    }
    if (shot == "gravmine") {
        // A mine as the Antimatter Mines, that slows what it finds.
        _mines.push_back({from, slot, special, special.life, 0});
        return true;
    }
    if (shot == "manheimeffect") {
        // A copy of an allied ship, its shields, crew and systems as they
        // are, for its effectTimer (0x4d8fe0); where it stands, beside it.
        if (aimed && units[size_t(target)].slot == slot && _moves[size_t(target)]) {
            const MapScene::Unit host = units[size_t(target)];
            gfx::Vec3 at = host.at;
            at[0] += host.radius() * 2;
            if (auto copy = addShip(host.odf, at, host.heading, host)) {
                _vitals[*copy] = _vitals[size_t(target)];
                _expiresAt[*copy] = now + special.value("effectTimer", 180);
            }
        }
        return true;
    }
    if (!aimed)
        return true;
    Effects &e = _effects[size_t(target)];
    Combat::Vitals &v = _vitals[size_t(target)];
    if (shot == "fedrepairteam") {
        // Its shields up to shieldsReturned (half) of their most, every
        // system whole and back on (0x4d65a0); not a derelict.
        if (!v.derelict()) {
            for (Combat::SystemState &sys : v.systems) {
                sys.hitPoints = sys.maxHitPoints;
                sys.offline = 0;
            }
            v.shields = std::max(v.shields, v.maxShields * special.value("shieldsReturned", 0.5f));
        }
    } else if (shot == "remodulation") {
        e.invulnerable = now + lastingOf(special, 20);
    } else if (shot == "rfield") {
        // Not a starbase: its damage, its engines out for its specialValue,
        // and pushed away from the ship at its repulsionForce (0x4dd680),
        // for as long *(inferred: how long the push lasts)*.
        if (_classes[size_t(target)] != "starbase") {
            _roll = _roll * 1664525u + 1013904223u;
            Combat::Weapon as;
            as.damage = special.damage;
            as.variance = special.variance;
            const float damage = Combat::damageOf(as, float(_roll >> 8) / float(1u << 24));
            _roll = _roll * 1664525u + 1013904223u;
            Combat::hit(v, damage, special.crew, float(_roll >> 8) / float(1u << 24));
            const float lasting = float(lastingOf(special, 20));
            Combat::disable(v, Combat::kEngines, lasting);
            const gfx::Vec3 to = units[size_t(target)].middle();
            const float d = std::max(1.0f, std::hypot(to[0] - from[0], to[2] - from[2]));
            const float force = special.value("repulsionForce", 10);
            e.drift = {(to[0] - from[0]) / d * force, 0, (to[2] - from[2]) / d * force};
            e.driftUntil = now + lasting;
            _ships[size_t(target)].goal.reset();
            if (!v.alive())
                destroy(size_t(target), slot);
        }
    } else if (shot == "polerontorpedo") {
        // No damage: one of its engines, weapons, life support and sensors,
        // whichever it has of five rolls, out for its specialValue (0x4dbe90).
        static constexpr Combat::System kChoices[] = {Combat::kEngines, Combat::kWeapons, Combat::kLifeSupport,
                                                      Combat::kSensors};
        for (int tries = 0; tries < 5; ++tries) {
            _roll = _roll * 1664525u + 1013904223u;
            const Combat::System sys = kChoices[(_roll >> 8) % 4];
            if (v.systems[sys].maxHitPoints > 0) {
                Combat::disable(v, sys, float(lastingOf(special, 20)));
                break;
            }
        }
    } else if (shot == "psychonicblast") {
        // Its percentage of the crew killed at once (0x4dce80).
        if (v.maxCrew >= 1) {
            v.crew -= v.crew * special.value("percentage", 0);
            if (v.crew < 1)
                v.crew = 0;
        }
    } else if (shot == "klingoncommandoteam") {
        // Its partySize, or what crew the ship has, off it, boarding a
        // timeDelay later (0x4d8410, 0x4d8820).
        const float party = std::min(special.value("partySize", 40), _vitals[unit].crew);
        _vitals[unit].crew -= party;
        _parties.push_back(
            {unit, size_t(target), slot, party, _vitals[unit].boarding, now + special.value("timeDelay", 5)});
    } else if (shot == "nanitesordnance") {
        e.scrambled = now + lastingOf(special, 45);
    } else if (shot == "romulanspy") {
        _spies.push_back({size_t(target), slot, now + special.life});
    } else if (shot == "override") {
        // Its player's for a while, in its colour.
        Takeover t{units[size_t(target)].slot, units[size_t(target)].colour, now + lastingOf(special, 20)};
        if (!_takenOver.count(size_t(target)))
            _takenOver[size_t(target)] = t;
        _scene.setOwner(size_t(target), slot, units[unit].colour);
        _targets[size_t(target)] = -1;
    }
    return true;
}

void MissionView::firePoint(const std::string &button, const gfx::Vec3 &point) {
    // Transwarp: the ship there at once; a transwarp conduit, its player's
    // ships near it too. Only to a place seen.
    if (_scene.covered(point) && _scene.cover().shroud)
        return;
    for (int i : obeying(_selection)) {
        if (size_t(i) >= _specials.size())
            continue;
        for (Combat::Special &sp : _specials[size_t(i)]) {
            if (sp.button != button || !wantsPoint(sp) || !usable(size_t(i), sp))
                continue;
            _vitals[size_t(i)].energy -= sp.energy;
            sp.ready = sp.delay;
            const gfx::Vec3 from = _scene.units()[size_t(i)].at;
            for (size_t j = 0; j < _ships.size() && j < _physics.size(); ++j) {
                const bool going =
                    j == size_t(i) || (sp.kind == "wormholegen" && _physics[j] && _scene.units()[j].slot == _slot &&
                                       !_dead[j] && distanceOf(_scene.units()[j].at, from) <= 300);
                if (!going)
                    continue;
                for (size_t k : {size_t(0), size_t(2)})
                    _ships[j].at[k] = point[k] + (_scene.units()[j].at[k] - from[k]);
                _ships[j].goal.reset();
                _ships[j].speed = 0;
                _scene.moveUnit(j, _ships[j].at, _ships[j].heading);
            }
        }
    }
}

void MissionView::runEffects(double seconds) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const double now = double(_last) / 1000;
    std::vector<std::pair<size_t, int>> killed;
    // The fields: a storm and a black hole hurt what's in them a second; a
    // wave, what it passes, once.
    std::erase_if(_fields, [now](const Field &f) { return now >= f.until; });
    for (Field &f : _fields) {
        f.radius += f.grow * float(seconds);
        if (f.kind == "rift") {
            // Every unit in it, its own side's too, taking its damage, give
            // or take its variance, a second (0x4d32b0).
            const float t = float(now - f.began);
            const auto &[first, most, grow, stay, shrink] = f.shape;
            const float scale = t < grow          ? first + (most - first) * t / std::max(grow, 1e-3f)
                                : t < grow + stay ? most
                                                  : most - (most - first) * (t - grow - stay) / std::max(shrink, 1e-3f);
            f.radius = f.size * std::max(scale, first);
            for (size_t j = 0; j < units.size() && j < _dead.size(); ++j) {
                if (_dead[j] || distanceOf(units[j].middle(), f.at) > f.radius)
                    continue;
                _roll = _roll * 1664525u + 1013904223u;
                const float damage =
                    (f.damage + f.variance * (2 * float(_roll >> 8) / float(1u << 24) - 1)) * float(seconds);
                _roll = _roll * 1664525u + 1013904223u;
                Combat::hit(_vitals[j], damage, f.crew, float(_roll >> 8) / float(1u << 24));
                if (!_vitals[j].alive())
                    killed.emplace_back(j, f.slot);
            }
            continue;
        }
        for (size_t j = 0; j < units.size() && j < _dead.size(); ++j) {
            if (_dead[j] || units[j].slot == f.slot)
                continue;
            const float d = distanceOf(units[j].middle(), f.at);
            if (f.kind == "wave") {
                if (d <= f.radius && !f.hit.count(j)) {
                    f.hit.insert(j);
                    Combat::hit(_vitals[j], f.damage);
                }
            } else if (d <= f.radius) {
                Combat::hit(_vitals[j], f.damage * float(seconds));
            }
            if (!_vitals[j].alive())
                killed.emplace_back(j, f.slot);
        }
    }
    // The beams that do something each second.
    for (Hold &h : _holds) {
        if (h.unit >= units.size() || h.target >= units.size())
            continue;
        Combat::Vitals &mine = _vitals[h.unit];
        Combat::Vitals &theirs = _vitals[h.target];
        const float amount = h.rate * float(seconds);
        if (h.kind == "drainShields") {
            // To the holder 1:1, its own most no bound (VampireEffect).
            const float took = std::min(theirs.shields, amount);
            theirs.shields -= took;
            mine.shields += took;
        } else if (h.kind == "giveShields") {
            // The holder's special energy into the target's shields, 1:1.
            const float gave = std::min(mine.energy, amount);
            mine.energy -= gave;
            theirs.shields = std::min(theirs.maxShields, theirs.shields + gave);
        } else if (h.kind == "assimilate") {
            // To the holder's crew, to its most, the rest to its player's
            // pool.
            const float took = std::min(theirs.crew, amount);
            theirs.crew -= took;
            if (theirs.crew < 1)
                theirs.crew = 0;
            const float room = std::max(0.0f, mine.maxCrew - mine.crew);
            mine.crew += std::min(room, took);
            if (Hud::Resources *bank = bankOf(units[h.unit].slot))
                bank->crew += int(std::lround(std::max(0.0f, took - room)));
        } else if (h.kind == "tow") {
            // Pulled at the distance past its targetRange a second.
            Movement::Ship &towed = _ships[h.target];
            const gfx::Vec3 to = _ships[h.unit].at;
            const float d = std::hypot(to[0] - towed.at[0], to[2] - towed.at[2]);
            if (d > h.pull) {
                const float step = std::min(d - h.pull, (d - h.pull) * float(seconds));
                for (size_t k : {size_t(0), size_t(2)})
                    towed.at[k] += (to[k] - towed.at[k]) / d * step;
                _scene.moveUnit(h.target, towed.at, towed.heading);
            }
        } else if (h.kind == "hold") {
            // Its engines out while held; a boarding party each interval.
            Combat::disable(theirs, Combat::kEngines, 1);
            if (h.boardAt >= 0 && now >= h.boardAt) {
                if (mine.crew > h.boardSize + 5 && theirs.maxCrew > 0) {
                    mine.crew -= h.boardSize;
                    _parties.push_back({h.unit, h.target, units[h.unit].slot, h.boardSize, mine.boarding, now});
                }
                h.boardAt = now + std::max(1.0f, h.boardEvery);
            }
        }
    }
    // The specials left on: their cost a second, off when it can't be paid;
    // regeneration its regenModifier of shields a second and every system
    // whole, off once the shields are (0x501bf0).
    for (auto it = _running.begin(); it != _running.end();) {
        const auto &[key, run] = *it;
        const size_t j = key.first;
        if (j >= _dead.size() || j >= _vitals.size() || _dead[j]) {
            it = _running.erase(it);
            continue;
        }
        Combat::Vitals &v = _vitals[j];
        const float cost = run.cost * float(seconds);
        if (v.energy < cost) {
            v.energy = std::max(0.0f, v.energy - cost);
            if (key.second == "phasecloakdevice" && j < _cloaked.size() && _cloaked[j]) {
                _cloaked[j] = false;
                _scene.setCloaked(j, false);
            }
            it = _running.erase(it);
            continue;
        }
        v.energy -= cost;
        if (key.second == "regeneration") {
            v.shields = std::min(v.maxShields, v.shields + run.rate * float(seconds));
            for (Combat::SystemState &sys : v.systems)
                sys.hitPoints = sys.maxHitPoints;
            if (v.shields >= v.maxShields - 0.01f) {
                it = _running.erase(it);
                continue;
            }
        } else if (key.second == "reflectweapon") {
            _effects[j].reflecting = now + 0.1;
        } else if (key.second == "pointdefenselaser") {
            _effects[j].defending = now + 0.1;
        }
        ++it;
    }
    // Crew killing each other, or boarders, a second; special energy
    // drained away (TimedDrain); pushed by a repulsion field.
    for (size_t j = 0; j < _effects.size() && j < _vitals.size(); ++j) {
        Effects &e = _effects[j];
        if (active(e.crewDrainUntil))
            _vitals[j].crew = std::max(0.0f, _vitals[j].crew - e.crewDrain * float(seconds));
        if (active(e.energyDrainUntil))
            _vitals[j].energy = std::max(0.0f, _vitals[j].energy - e.energyDrain * float(seconds));
        if (active(e.driftUntil) && j < _ships.size() && !_dead[j]) {
            for (size_t k : {size_t(0), size_t(2)})
                _ships[j].at[k] += e.drift[k] * float(seconds);
            _scene.moveUnit(j, _ships[j].at, _ships[j].heading);
        }
    }
    // Shots at a ship shooting them down, gone.
    std::erase_if(_shots, [&](const Combat::Shot &s) {
        return s.target < _effects.size() && active(_effects[s.target].defending) && s.slot != units[s.target].slot &&
               distanceOf(s.at, units[s.target].middle()) <= 150;
    });
    // Gemini's copies gone, and takeovers given back.
    std::vector<size_t> expired;
    for (const auto &[unit, at] : _expiresAt)
        if (now >= at)
            expired.push_back(unit);
    for (size_t unit : expired) {
        _expiresAt.erase(unit);
        if (!_dead[unit]) {
            _dead[unit] = true;
            _scene.hideUnit(unit);
            std::erase(_selection, int(unit));
        }
    }
    for (auto it = _takenOver.begin(); it != _takenOver.end();) {
        if (now < it->second.until) {
            ++it;
            continue;
        }
        _scene.setOwner(it->first, it->second.slot, it->second.colour);
        _targets[it->first] = -1;
        it = _takenOver.erase(it);
    }
    std::erase_if(_spies, [now](const Spy &s) { return now >= s.until; });
    for (const auto &[k, by] : killed)
        if (!_dead[k])
            destroy(k, by);
}

void MissionView::uncloakHit() {
    // A cloaked unit hit since the last frame shows itself (0x4f5e90): its
    // shields or crew are down.
    for (size_t i = 0; i < _cloaked.size() && i < _vitals.size(); ++i) {
        if (!_cloaked[i])
            continue;
        const Combat::Vitals &v = _vitals[i];
        const auto [shields, crew] = _cloakedWith[i];
        if (v.shields < shields - 0.001f || v.crew < crew - 0.001f) {
            _cloaked[i] = false;
            _scene.setCloaked(i, false);
            _running.erase(std::pair{i, std::string("phasecloakdevice")});
        }
        _cloakedWith[i] = {v.shields, v.crew};
    }
}

} // namespace reluva::storm3d
