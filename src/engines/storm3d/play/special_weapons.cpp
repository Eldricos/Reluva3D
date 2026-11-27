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

    // Beams that run while their target's in range: their cost a second.
    if (special.kind == "toggleweapon") {
        if (!aimed || size_t(target) == unit)
            return true;
        Hold h{unit, size_t(target), now + (special.life > 0 ? special.life : 30), special.energy, reach};
        if (shot == "holding_bm")
            h.kind = "hold";
        else if (shot == "tractor_bm") {
            // Only a ship whose engines are out can be towed.
            if (!_vitals[size_t(target)].systems[Combat::kEngines].out() &&
                !active(_effects[size_t(target)].enginesOff))
                return true;
            h.kind = "tow";
            for (size_t k = 0; k < 3; ++k)
                h.offset[k] = units[size_t(target)].at[k] - units[unit].at[k];
        } else if (shot == "shieldinv") {
            h.kind = "drainShields";
            h.rate = 50;
        } else if (shot == "esconverter") {
            h.kind = "giveShields";
            h.rate = special.modifier > 0 ? special.modifier : 50;
        } else if (shot == "borgbore") {
            h.kind = "assimilate";
            h.rate = special.modifier > 0 ? special.modifier : 5;
        } else if (shot == "blackholecreator") {
            // Not a beam: a black hole where it's aimed.
            _fields.push_back({"blackhole", units[size_t(target)].middle(), slot, now, now + 10, 0, 30, 75, {}});
            return true;
        }
        std::erase_if(_holds, [unit](const Hold &x) { return x.unit == unit; });
        _holds.push_back(h);
        return true;
    }
    _vitals[unit].energy -= special.energy;

    if (special.kind == "reflectweapon") {
        _effects[unit].reflecting = now + 10;
        return true;
    }
    if (special.kind == "pointdefenselaser") {
        _effects[unit].defending = now + 10;
        return true;
    }
    if (shot == "warpcoreoverload" || shot == "posijammer" || shot == "holoemitter") {
        // On the ships of others within its range: their engines, their
        // sensors, or which side they take for their enemy.
        for (size_t j : near(from, reach, true)) {
            Effects &e = _effects[j];
            const double until = now + lastingOf(special, 20);
            (shot == "warpcoreoverload" ? e.enginesOff : shot == "posijammer" ? e.sensorsOff : e.confused) = until;
        }
        return true;
    }
    if (shot == "deathchant") {
        // Its own side within range fight half again as hard.
        for (size_t j : near(from, reach, false))
            _effects[j].boosted = now + lastingOf(special, 15);
        return true;
    }
    if (shot == "temporal") {
        // Everything of others within its Radius of the target, still.
        if (!aimed)
            return true;
        for (size_t j : near(units[size_t(target)].middle(), 400, true))
            _effects[j].frozen = now + 20;
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
        // From its target to the nearest other in its chain's reach, five
        // times, each hit 10 harder.
        if (!aimed)
            return true;
        std::set<size_t> struck;
        size_t at = size_t(target);
        float damage = special.damage;
        for (int hop = 0; hop < 5; ++hop) {
            struck.insert(at);
            Combat::hit(_vitals[at], damage);
            if (!_vitals[at].alive() && !_dead[at])
                destroy(at, slot);
            damage += 10;
            int next = -1;
            float best = 300;
            for (size_t j : near(units[at].middle(), 300, true))
                if (!struck.count(j) && distanceOf(units[j].middle(), units[at].middle()) <= best)
                    next = int(j), best = distanceOf(units[j].middle(), units[at].middle());
            if (next < 0)
                break;
            at = size_t(next);
        }
        return true;
    }
    if (shot == "gravmine") {
        // A mine as the Antimatter Mines, that slows what it finds.
        _mines.push_back({from, slot, special, special.life, 0});
        return true;
    }
    if (shot == "manheimeffect") {
        // A copy of an allied ship beside it, for a minute.
        if (aimed && units[size_t(target)].slot == slot) {
            const MapScene::Unit host = units[size_t(target)];
            gfx::Vec3 at = host.at;
            at[0] += host.radius() * 2;
            if (auto copy = addShip(host.odf, at, host.heading, host))
                _expiresAt[*copy] = now + 60;
        }
        return true;
    }
    if (!aimed)
        return true;
    Effects &e = _effects[size_t(target)];
    Combat::Vitals &v = _vitals[size_t(target)];
    if (shot == "fedrepairteam") {
        // Every system whole, and half its shields back.
        for (Combat::SystemState &sys : v.systems)
            sys.hitPoints = sys.maxHitPoints;
        v.shields = std::min(v.maxShields, v.shields + v.maxShields / 2);
    } else if (shot == "remodulation") {
        e.invulnerable = now + lastingOf(special, 20);
    } else if (shot == "rfield") {
        // Pushed away, its engines out.
        Movement::Ship &ship = _ships[size_t(target)];
        const gfx::Vec3 to = units[size_t(target)].middle();
        const float d = std::max(1.0f, distanceOf(to, from));
        for (size_t k : {size_t(0), size_t(2)})
            ship.at[k] += (to[k] - from[k]) / d * 150;
        ship.goal.reset();
        _scene.moveUnit(size_t(target), ship.at, ship.heading);
        e.enginesOff = now + lastingOf(special, 20);
    } else if (shot == "polerontorpedo") {
        // Through its shields: its damage on the hull, and a system out.
        v.hull = std::max(0.0f, v.hull - special.damage * 2);
        _roll = _roll * 1664525u + 1013904223u;
        Combat::SystemState &sys = v.systems[(_roll >> 8) % Combat::kSystems];
        sys.hitPoints = 0;
        if (!v.alive())
            destroy(size_t(target), slot);
    } else if (shot == "psychonicblast") {
        e.crewDrain = std::max(1.0f, v.crew * 0.05f);
        e.crewDrainUntil = now + 10;
    } else if (shot == "klingoncommandoteam") {
        e.crewDrain = 20;
        e.crewDrainUntil = now + 5;
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
    for (const Hold &h : _holds) {
        if (h.unit >= units.size() || h.target >= units.size())
            continue;
        Combat::Vitals &mine = _vitals[h.unit];
        Combat::Vitals &theirs = _vitals[h.target];
        const float amount = h.rate * float(seconds);
        if (h.kind == "drainShields") {
            const float took = std::min(theirs.shields, amount);
            theirs.shields -= took;
            mine.shields = std::min(mine.maxShields, mine.shields + took);
        } else if (h.kind == "giveShields") {
            const float gave = std::min(mine.shields, amount);
            mine.shields -= gave;
            theirs.shields = std::min(theirs.maxShields, theirs.shields + gave);
        } else if (h.kind == "assimilate") {
            // Into its player's crew: the Borg's drone pool.
            const float took = std::min(theirs.crew, amount);
            theirs.crew -= took;
            if (Hud::Resources *bank = bankOf(units[h.unit].slot))
                bank->crew += int(std::lround(took));
        } else if (h.kind == "tow") {
            Movement::Ship &towed = _ships[h.target];
            for (size_t k : {size_t(0), size_t(2)})
                towed.at[k] = _ships[h.unit].at[k] + h.offset[k];
            _scene.moveUnit(h.target, towed.at, towed.heading);
        }
    }
    // Crew killing each other, or boarders, a second.
    for (size_t j = 0; j < _effects.size() && j < _vitals.size(); ++j)
        if (active(_effects[j].crewDrainUntil))
            _vitals[j].crew = std::max(0.0f, _vitals[j].crew - _effects[j].crewDrain * float(seconds));
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

} // namespace reluva::storm3d
