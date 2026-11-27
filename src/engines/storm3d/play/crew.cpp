// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"

#include <algorithm>
#include <cmath>

// Crew moved between units: boarding parties sent by transporter, which
// take a ship whose crew they beat; ships repaired and recrewed at a
// shipyard; stations recrewed from the pool. The transporters' numbers are
// RTS_CFG.h's, the crews' strengths their races'; the rest guessed. See
// docs/engines/storm3d/games/armada/systems/combat.md.

namespace reluva::storm3d {

namespace {

// RTS_CFG.h's: how far a transporter reaches, how long between parties and
// before one arrives.
constexpr float kTransporterRange = 500;
constexpr double kTransporterDelay = 1;
constexpr double kTransporterActionDelay = 1;

float distanceOf(const gfx::Vec3 &a, const gfx::Vec3 &b) {
    return std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

} // namespace

bool MissionView::disabled(size_t unit) const {
    return unit < _vitals.size() && _vitals[unit].maxCrew > 0 && _vitals[unit].crew <= 0;
}

void MissionView::transport(size_t unit, size_t target) {
    if (unit == target || unit >= _vitals.size() || target >= _vitals.size() || _vitals[unit].crew < 1)
        return;
    _transportTo[unit] = target;
}

std::optional<size_t> MissionView::repairStationFor(size_t ship) const {
    // The nearest of its player's shipyards and starbases *(guessed: the
    // tooltip says shipyard; no yard or starbase sets repairFacility 0)*.
    const std::vector<MapScene::Unit> &units = _scene.units();
    std::optional<size_t> best;
    float nearest = 1e30f;
    for (size_t j = 0; j < units.size() && j < _classes.size(); ++j) {
        if (_dead[j] || units[j].slot != units[ship].slot || (_classes[j] != "shipyard" && _classes[j] != "starbase"))
            continue;
        const float d = distanceOf(units[j].middle(), units[ship].middle());
        if (d < nearest)
            nearest = d, best = j;
    }
    return best;
}

void MissionView::capture(size_t unit, int slot, float crew) {
    // Its new player's, in their colour, what it was doing forgotten.
    const MapScene::Unit &by = *std::find_if(_scene.units().begin(), _scene.units().end(),
                                             [slot](const MapScene::Unit &u) { return u.slot == slot; });
    const int was = _scene.units()[unit].slot;
    _scene.setOwner(unit, slot, by.colour);
    _vitals[unit].crew = crew;
    _targets[unit] = -1;
    for (int &t : _targets)
        if (t == int(unit))
            t = -1;
    if (unit < _ships.size()) {
        _ships[unit].goal.reset();
        _ships[unit].speed = 0;
    }
    _transportTo.erase(unit);
    _repairingAt.erase(unit);
    std::erase(_selection, int(unit));
    const bool ship = unit < _moves.size() && _moves[unit];
    ++(ship ? _tallies[was].shipsLost : _tallies[was].stationsLost);
    ++(ship ? _tallies[slot].shipsDestroyed : _tallies[slot].stationsDestroyed);
}

void MissionView::fromPool(size_t unit, Hud::Resources &bank, double seconds) {
    // 20 a second, whole crew from the pool as they add up, to its most.
    Combat::Vitals &v = _vitals[unit];
    double &carry = _poolCarry[unit];
    carry += 20 * seconds;
    const int whole = std::min({int(std::floor(carry)), bank.crew, int(std::ceil(v.maxCrew - v.crew))});
    if (whole <= 0)
        return;
    carry -= whole;
    v.crew = std::min(v.maxCrew, v.crew + float(whole));
    bank.crew -= whole;
}

void MissionView::runCrew(double seconds) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const double now = double(_last) / 1000;
    // Transporting: near enough first, then a party each second, a fifth
    // of its crew, while it keeps its race's crewRetreatRatio and the
    // target's still to take or short of crew *(guessed: a party's size)*.
    for (auto it = _transportTo.begin(); it != _transportTo.end();) {
        const size_t unit = it->first;
        const size_t target = it->second;
        if (unit >= units.size() || target >= units.size() || _dead[unit] || _dead[target]) {
            it = _transportTo.erase(it);
            continue;
        }
        Combat::Vitals &mine = _vitals[unit];
        const Combat::Vitals &theirs = _vitals[target];
        const bool ally = units[target].slot == units[unit].slot;
        if (mine.crew <= std::max(1.0f, mine.maxCrew * 0.1f) || (ally && theirs.crew >= theirs.maxCrew)) {
            it = _transportTo.erase(it);
            continue;
        }
        const float d = distanceOf(units[unit].middle(), units[target].middle());
        if (d > kTransporterRange) {
            if (unit < _physics.size() && _physics[unit])
                if (auto point = chasePointOf(units[unit].middle(), units[target].middle(), kTransporterRange,
                                              _ships[unit].at[1]))
                    _ships[unit].goal = point;
            ++it;
            continue;
        }
        if (now >= _nextParty[unit]) {
            const float party = std::max(1.0f, std::floor(mine.crew * 0.2f));
            mine.crew -= party;
            _parties.push_back({unit, target, units[unit].slot, party, mine.boarding, now + kTransporterActionDelay});
            _nextParty[unit] = now + kTransporterDelay;
        }
        ++it;
    }
    // Parties arriving: reinforcing an ally, or fighting the crew, the
    // stronger side left with what's over (Combat::board).
    for (size_t p = 0; p < _parties.size();) {
        Party &party = _parties[p];
        if (now < party.arrives) {
            ++p;
            continue;
        }
        if (party.target < units.size() && !_dead[party.target]) {
            Combat::Vitals &v = _vitals[party.target];
            if (units[party.target].slot == party.slot) {
                v.crew = std::min(v.maxCrew, v.crew + party.crew);
            } else {
                const Combat::Boarding fight = Combat::board(party.crew, party.boarding, v.crew, v.boarding);
                if (fight.taken)
                    capture(party.target, party.slot, std::min(v.maxCrew, std::max(1.0f, fight.left)));
                else
                    v.crew = fight.left;
            }
        }
        _parties.erase(_parties.begin() + std::ptrdiff_t(p));
    }
    // Repair and Recrew: to the nearest station that repairs, then hull,
    // systems and shields back over half a minute by the crew's
    // repairStrength, and crew from the pool, 20 a second, to its
    // crewCost *(guessed)*.
    for (auto it = _repairingAt.begin(); it != _repairingAt.end();) {
        const size_t ship = it->first;
        const size_t station = it->second;
        if (ship >= units.size() || station >= units.size() || _dead[ship] || _dead[station]) {
            it = _repairingAt.erase(it);
            continue;
        }
        Combat::Vitals &v = _vitals[ship];
        const float reach = units[station].radius() + units[ship].radius() + 60;
        if (distanceOf(units[ship].middle(), units[station].middle()) > reach) {
            if (!_ships[ship].goal)
                _ships[ship].goal = units[station].at;
            ++it;
            continue;
        }
        _ships[ship].goal.reset();
        const float pace = float(seconds) / 30 * v.repairStrength;
        v.hull = std::min(v.maxHull, v.hull + v.maxHull * pace);
        v.shields = std::min(v.maxShields, v.shields + v.maxShields * pace);
        for (Combat::SystemState &s : v.systems)
            s.hitPoints = std::min(s.maxHitPoints, s.hitPoints + s.maxHitPoints * pace);
        if (Hud::Resources *bank = bankOf(units[ship].slot))
            fromPool(ship, *bank, seconds);
        const bool whole = v.hull >= v.maxHull && v.crew >= v.maxCrew && !v.damaged();
        it = whole ? _repairingAt.erase(it) : std::next(it);
    }
    // Recrew: a station's crew from the pool, 20 a second, to its most.
    for (auto it = _recrewing.begin(); it != _recrewing.end();) {
        const size_t station = *it;
        Hud::Resources *bank = station < units.size() ? bankOf(units[station].slot) : nullptr;
        if (!bank || _dead[station] || _vitals[station].crew >= _vitals[station].maxCrew || bank->crew <= 0) {
            it = _recrewing.erase(it);
            continue;
        }
        fromPool(station, *bank, seconds);
        ++it;
    }
}

} // namespace reluva::storm3d
