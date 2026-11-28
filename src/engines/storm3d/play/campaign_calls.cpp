// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the campaign's other missions ask of the game beyond Federation 1's
// calls (script_calls.cpp): players and their computer players, units'
// state, finding and counting, the people aboard ships, and the world
// (docs/engines/storm3d/games/armada/systems/mission-scripts.md).

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace reluva::storm3d {

namespace {

constexpr int32_t kObjectHandles = 0x100000;

uint32_t bitsOf(float f) {
    return std::bit_cast<uint32_t>(f);
}

// What the program gives for a shield's share it can't read (0x5bdb4c).
constexpr float kNoShields = -33.0f;

} // namespace

bool MissionView::hostile(int slot, int other) const {
    if (slot == other)
        return false;
    auto stance = [this](int a, int b) {
        auto it = _relations.find({a, b});
        return it == _relations.end() ? 0 : it->second;
    };
    return std::min(stance(slot, other), stance(other, slot)) == 0;
}

void MissionView::runPatrols() {
    // Round its path again each time it's at the end *(inferred)*.
    for (auto it = _patrols.begin(); it != _patrols.end();) {
        const size_t unit = it->first;
        auto path = _paths.find(it->second);
        if (unit >= _dead.size() || _dead[unit] || path == _paths.end() || path->second.empty()) {
            it = _patrols.erase(it);
            continue;
        }
        Movement::Ship &ship = _ships[unit];
        if (!ship.goal && _targets[unit] < 0) {
            ship.route.assign(path->second.begin(), path->second.end() - 1);
            ship.goal = path->second.back();
            ship.waiting = Movement::kSetOff;
        }
        ++it;
    }
}

std::optional<uint32_t> MissionView::moreScriptCalls(const std::string &name, const MissionScripts::Arguments &args) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    const double now = double(_last) / 1000;
    auto pathPoint = [this](const std::string &path, int32_t point) -> std::optional<gfx::Vec3> {
        auto it = _paths.find(path);
        if (it == _paths.end() || point < 0 || size_t(point) >= it->second.size())
            return std::nullopt;
        return it->second[size_t(point)];
    };
    auto ordered = [this, &args](int n) -> std::optional<size_t> {
        auto unit = unitOfHandle(args.integer(n));
        if (!unit || !_scripted.count(*unit))
            return std::nullopt;
        return unit;
    };
    auto perceived = [this, &units](size_t unit) {
        auto it = _perceived.find(unit);
        return it == _perceived.end() ? units[unit].slot : it->second;
    };
    // A player's sight: the human's is the scene's; the computers' aren't
    // kept, so they see everything *(Reluva's own)*.
    auto seenBy = [this](size_t unit, int slot) { return slot < 0 || slot != _slot || _scene.shown(unit); };
    // CountUnits' tests (0x435f60): living, not derelict, of the player
    // (-1 any) and the definition (none any).
    auto counted = [this, &units](size_t i, int32_t player, const std::string &odf) {
        return !_dead[i] && !_vitals[i].derelict() && (player < 0 || units[i].slot == player) &&
               (odf.empty() || text::equalsIgnoreCase(units[i].odf, odf));
    };
    auto across = [](const gfx::Vec3 &a, const gfx::Vec3 &b) { return std::hypot(a[0] - b[0], a[2] - b[2]); };
    auto remove = [this](size_t unit) {
        _dead[unit] = true;
        _scene.hideUnit(unit);
        forget(unit);
    };
    auto computerOf = [this](int slot) -> Computer * {
        for (Computer &c : _computers)
            if (c.slot == slot)
                return &c;
        return nullptr;
    };

    // The players' counters (0x437ab0 on): set or added to, the officers
    // to their limit; the new value given back.
    if (name == "SetTeamCrew" || name == "AddCrew") {
        Hud::Resources *bank = bankOf(args.integer(0));
        if (!bank)
            return bitsOf(0);
        bank->crew = int(name == "AddCrew" ? float(bank->crew) + args.real(1) : args.real(1));
        return bitsOf(float(bank->crew));
    }
    if (name == "SetOfficers" || name == "AddOfficers") {
        Hud::Resources *bank = bankOf(args.integer(0));
        if (!bank)
            return bitsOf(0);
        const int n = int(args.real(1));
        bank->officers = std::min(name == "AddOfficers" ? bank->officers + n : n, bank->maxOfficers);
        return bitsOf(float(bank->officers));
    }
    if (name == "AddDilithium") {
        Hud::Resources *bank = bankOf(args.integer(0));
        if (!bank)
            return 0u;
        bank->dilithium += args.integer(1);
        return uint32_t(bank->dilithium);
    }

    // The computer players: on or off, and their personality (0x418980,
    // 0x434b70 on).
    if (name == "AI_Team_Load_AIP") {
        if (args.kinds.size() == 2 && args.kinds[0] == 'i') {
            const int slot = args.integer(0);
            _teamsOff.erase(slot);
            if (Computer *c = computerOf(slot)) {
                std::string file = text::toLower(args.text(1));
                // Its settings over the team's, as the race's own are (0x41a770).
                const Strategy::Personality p = Strategy::personalityOf(file, aiLoader());
                c->personality = p;
            }
        }
        return 0u;
    }
    if (name == "EnableTeam" || name == "DisableTeam") {
        if (name == "EnableTeam")
            _teamsOff.erase(args.integer(0));
        else
            _teamsOff.insert(args.integer(0));
        return 0u;
    }
    if (name == "SetIsAITeam") {
        // Both by reference.
        const int slot = int32_t(args.cpu.read32(args.word(0)));
        const bool on = (args.cpu.read8(args.word(1)) & 0xff) != 0;
        if (on && !computerOf(slot)) {
            Computer c;
            c.slot = slot;
            for (const MapScene::Unit &u : units)
                if (u.slot == slot) {
                    Odf def;
                    if (!def.load(u.odf + ".odf", odfLoader()))
                        c.race = Combat::raceFileOf(def.string("race"));
                    break;
                }
            takeStance(c, ComputerPlayer::Stance::kNormal);
            _computers.push_back(c);
        } else if (!on) {
            std::erase_if(_computers, [slot](const Computer &c) { return c.slot == slot; });
        }
        return 0u;
    }
    if (name == "SetSpecialWeaponAttackInterval" || name == "EnableWormholeTravel" ||
        name == "AllowMultipleSpecialWeaponFire")
        return 0u; // kept by the program for its computer players' choices, which Reluva's don't make yet
    if (name == "SetRelation") {
        // One player's stance alone; the two together decide (0x438ca0).
        _relations[{args.integer(0), args.integer(1)}] = args.integer(2);
        return 0u;
    }
    if (name == "GetAllyShipAttacks" || name == "GetAllyStationAttacks")
        return 0u; // an ally's attacks on the player aren't counted yet

    // Owners.
    if (name == "GetRealTeam") {
        auto unit = unitOfHandle(args.integer(0));
        return unit ? uint32_t(units[*unit].slot) : 0u;
    }
    if (name == "SetRealTeam") {
        auto unit = unitOfHandle(args.integer(0));
        const int slot = args.integer(1);
        if (unit) {
            std::optional<gfx::Vec3> colour = units[*unit].colour;
            for (const MapScene::Unit &u : units)
                if (u.slot == slot) {
                    colour = u.colour;
                    break;
                }
            _scene.setOwner(*unit, slot, colour);
            _targets[*unit] = -1;
        }
        return 0u;
    }
    if (name == "SetPerceivedTeam") {
        if (auto unit = unitOfHandle(args.integer(0)))
            _perceived[*unit] = args.integer(1);
        return 0u;
    }
    if (name == "SetRace" || name == "EnableLight" || name == "SetRedAlert" || name == "SetYellowAlert" ||
        name == "SetGreenAlert")
        return 0u; // kept by the program; what they change in play isn't known

    // A unit's state.
    if (name == "IsAlive" || name == "Exists") {
        // A unit not destroyed, or a map object (0x436d50, 0x436ce0).
        const int32_t h = args.integer(0);
        return unitOfHandle(h) || (h >= kObjectHandles && placeOfHandle(h)) ? 1u : 0u;
    }
    if (name == "DoesNotExist") {
        const int32_t h = args.integer(0);
        return unitOfHandle(h) || (h >= kObjectHandles && placeOfHandle(h)) ? 0u : 1u;
    }
    if (name == "IsExploding")
        return 0u; // destroyed is gone at once here
    if (name == "IsDerelict") {
        auto unit = unitOfHandle(args.integer(0));
        return !unit || _vitals[*unit].derelict() ? 1u : 0u; // a bad handle reads derelict (0x436db0)
    }
    if (name == "GetShieldPercent" || name == "GetRealShieldPercent") {
        // A share, 0 to 1; or, under a disruptor, what it holds back.
        auto unit = unitOfHandle(args.integer(0));
        if (!unit)
            return bitsOf(kNoShields);
        const Combat::Vitals &v = _vitals[*unit];
        const float share = v.maxShields > 0 ? v.shields / v.maxShields : 0;
        if (name == "GetRealShieldPercent" && share < 0.01f && v.heldShields > 0)
            return bitsOf(v.heldShields);
        return bitsOf(share);
    }
    if (name == "SetMaxShields") {
        if (auto unit = unitOfHandle(args.integer(0)); unit && args.real(1) >= 0)
            _vitals[*unit].maxShields = _vitals[*unit].shields = args.real(1);
        return 0u;
    }
    if (name == "SetSpecialEnergy") {
        if (auto unit = unitOfHandle(args.integer(0)))
            _vitals[*unit].energy = args.real(1);
        return 0u;
    }
    if (name == "GetCrew") {
        auto unit = unitOfHandle(args.integer(0));
        return bitsOf(unit ? _vitals[*unit].crew : -1.0f);
    }
    if (name == "SetCrew") {
        // Within its most; with none it's derelict, unless it can't die,
        // which keeps 10 (0x463bc0, 0x463410).
        if (auto unit = unitOfHandle(args.integer(0))) {
            Combat::Vitals &v = _vitals[*unit];
            v.crew = std::clamp(args.real(1), 0.0f, v.maxCrew);
            if (v.crew < 1 && v.maxCrew > 0)
                v.crew = _cannotDie.count(*unit) ? 10 : 0;
        }
        return 0u;
    }
    if (name == "DestroyShieldGenerator" || name == "DestroyEngines" || name == "DestroySensors") {
        const Combat::System system = name == "DestroyEngines"   ? Combat::kEngines
                                      : name == "DestroySensors" ? Combat::kSensors
                                                                 : Combat::kShieldGenerator;
        if (auto unit = unitOfHandle(args.integer(0)))
            _vitals[*unit].systems[system].hitPoints = 0;
        return 0u;
    }
    if (name == "SetImpulseSpeed") {
        if (auto unit = unitOfHandle(args.integer(0)); unit && _physics[*unit])
            _physics[*unit]->cornerSpeed = args.real(1);
        return 0u;
    }
    if (name == "ResetWarpSpeed") {
        if (auto unit = unitOfHandle(args.integer(0))) {
            auto it = _warpSpeeds.find(*unit);
            if (it != _warpSpeeds.end() && _physics[*unit]) {
                _physics[*unit]->topSpeed = it->second;
                _warpSpeeds.erase(it);
            }
        }
        return 0u;
    }
    if (name == "UnderAttack") {
        // Hit by another player's within the seconds (0x434ed0).
        auto unit = unitOfHandle(args.integer(0));
        auto it = unit ? _hitAt.find(*unit) : _hitAt.end();
        return it != _hitAt.end() && now - it->second < double(args.real(1)) ? 1u : 0u;
    }
    if (name == "InStopMode") {
        // Stopped: going nowhere and fighting nothing *(inferred)*.
        auto unit = unitOfHandle(args.integer(0));
        return unit && !_ships[*unit].goal && _targets[*unit] < 0 ? 1u : 0u;
    }
    if (name == "OutOfSpecialAttackMode") {
        // Its special weapon's order over *(inferred)*: none is ever held
        // here, so any unit with an order is out of it, an idle one not.
        auto unit = unitOfHandle(args.integer(0));
        return unit && (_ships[*unit].goal || _targets[*unit] >= 0) ? 1u : 0u;
    }
    if (name == "GetDilithiumOnFreighter") {
        auto unit = unitOfHandle(args.integer(0));
        if (unit)
            for (const Freighter &f : _freighters)
                if (f.unit == *unit)
                    return bitsOf(f.cargo);
        return bitsOf(-1.0f);
    }
    if (name == "IsThereARomulanSpyOnShip") {
        auto unit = unitOfHandle(args.integer(0));
        return unit && std::any_of(_spies.begin(), _spies.end(), [&](const Spy &s) { return s.target == *unit; }) ? 1u
                                                                                                                  : 0u;
    }
    if (name == "GetAttackLocation") {
        // Returned in the place the script gives first: the attacker's rim
        // towards the target (0x435800).
        const uint32_t out = args.word(0);
        auto a = unitOfHandle(args.integer(1));
        gfx::Vec3 at{};
        if (a) {
            at = _ships[*a].at;
            const float r = ringHalfOf(units[*a]);
            if (auto t = placeOfHandle(args.integer(2))) {
                const float dx = (*t)[0] - at[0];
                const float dy = (*t)[1] - at[1];
                const float dz = (*t)[2] - at[2];
                const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > 0)
                    at = {at[0] + dx / d * r, at[1] + dy / d * r, at[2] + dz / d * r};
            } else {
                at[0] += r;
            }
        }
        for (uint32_t k = 0; k < 3; ++k)
            args.cpu.write32(out + 4 * k, bitsOf(at[k]));
        return out;
    }

    // Orders, to a unit the scripts have taken.
    if (name == "RecruitSpecialForces") {
        // The first of the player's of that definition, idle and not taken
        // yet, taken (0x434ca0).
        const std::string odf = args.text(0);
        args.cpu.write32(args.word(2), 0);
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && units[i].slot == args.integer(1) && text::equalsIgnoreCase(units[i].odf, odf) &&
                !_scripted.count(i) && !_ships[i].goal) {
                _scripted.insert(i);
                args.cpu.write32(args.word(2), uint32_t(i + 1));
                return 1u;
            }
        return 0u;
    }
    if (name == "StopAllShips") {
        // Of the player (-1 any), the definition (none any), only those
        // taken if it says, stations left out if it says (0x4350d0).
        const std::string odf = args.text(1);
        for (size_t i = 0; i < units.size(); ++i) {
            if (_dead[i] || _vitals[i].derelict() || (args.integer(0) >= 0 && units[i].slot != args.integer(0)) ||
                (!odf.empty() && !text::equalsIgnoreCase(units[i].odf, odf)) || (args.flag(2) && !_scripted.count(i)) ||
                (args.flag(3) && !_moves[i]))
                continue;
            _ships[i].goal.reset();
            _ships[i].route.clear();
            _targets[i] = -1;
            _patrols.erase(i);
        }
        return 0u;
    }
    if (name == "Patrol") {
        if (auto unit = ordered(0)) {
            _patrols[*unit] = args.text(1);
            _ships[*unit].goal.reset();
            _targets[*unit] = -1;
        }
        return 0u;
    }
    if (name == "Build") {
        // Built where it stands, as a station's queue does *(inferred)*.
        if (auto unit = ordered(0)) {
            const std::string item = text::toLower(args.text(1));
            Odf def;
            if (!def.load(item + ".odf", odfLoader()))
                _queues[*unit].push_back(orderOf(item, odfLoader()));
        }
        return 0u;
    }
    if (name == "Special_Attack") {
        // Its last special weapon of that definition (8 letters, as the
        // program compares them), fired at the target, or none (0x435250).
        auto unit = ordered(0);
        if (!unit)
            return 0u;
        const std::string weapon = args.text(args.kinds.size() - 1).substr(0, 8);
        Combat::Special *chosen = nullptr;
        for (Combat::Special &sp : _specials[*unit])
            if (sp.weapon.substr(0, 8) == weapon)
                chosen = &sp;
        if (!chosen)
            return 0u;
        // At a point: a transwarp's jump there (0x435460).
        if (args.kinds.size() == 3 && args.kinds[1] == 'v') {
            if (wantsPoint(*chosen))
                transwarp(*unit, *chosen, args.point(1));
            return 0u;
        }
        int target = -1;
        if (args.kinds.size() == 3 && args.kinds[1] == 'i')
            if (auto t = unitOfHandle(args.integer(1)))
                target = int(*t);
        if (!chosen->needTarget || target >= 0)
            fireSpecial(*unit, *chosen, target);
        return 0u;
    }
    if (name == "Cloak" || name == "ImmediateCloak" || name == "Decloak") {
        // As the player's cloak button (0x435a40): on if it's off, off if on.
        auto unit = ordered(0);
        if (!unit || (name == "Decloak") != bool(_cloaked[*unit]))
            return 0u;
        for (Combat::Special &sp : _specials[*unit])
            if (sp.kind == "cloakingdevice" || sp.kind == "phasecloakdevice") {
                fireSpecial(*unit, sp, -1);
                break;
            }
        return 0u;
    }
    if (name == "SearchAndDestroy") {
        // After the nearest unit it's at war with, anywhere *(guessed)*.
        auto unit = ordered(0);
        if (!unit)
            return 0u;
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && hostile(units[*unit].slot, perceived(i)) && _classes[i] != "wormhole_obj") {
                const float d = across(_ships[i].at, _ships[*unit].at);
                if (d < bestDistance)
                    best = int(i), bestDistance = d;
            }
        _targets[*unit] = best;
        return 0u;
    }
    if (name == "AttackMainEnemyBase") {
        // To the nearest starbase of a player it's at war with *(guessed:
        // the program's is where its computer player thinks the base is)*.
        auto unit = ordered(0);
        if (!unit)
            return 0u;
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && _classes[i] == "starbase" && hostile(units[*unit].slot, units[i].slot)) {
                const float d = across(_ships[i].at, _ships[*unit].at);
                if (d < bestDistance)
                    best = int(i), bestDistance = d;
            }
        _targets[*unit] = best;
        return 0u;
    }
    if (name == "Mine") {
        // A freighter: to work for its player's nearest mining station
        // *(inferred: the target moon isn't kept apart)*.
        auto unit = ordered(0);
        if (!unit || _classes[*unit] != "freighter" ||
            std::any_of(_freighters.begin(), _freighters.end(), [&](const Freighter &f) { return f.unit == *unit; }))
            return 0u;
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && _classes[i] == "mining" && units[i].slot == units[*unit].slot) {
                const float d = across(_ships[i].at, _ships[*unit].at);
                if (d < bestDistance)
                    best = int(i), bestDistance = d;
            }
        if (best >= 0) {
            Freighter f;
            f.unit = *unit;
            f.station = size_t(best);
            _freighters.push_back(f);
        }
        return 0u;
    }
    if (name == "RemoveAllCraft") {
        // The player's units (-1 all), stations only if it says (0x437960).
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && (args.integer(1) < 0 || units[i].slot == args.integer(1)) && (args.flag(0) || _moves[i]) &&
                _classes[i] != "wormhole_obj")
                remove(i);
        return 0u;
    }
    if (name == "RemoveAllObjects") {
        // Every unit (0x437910); the map's own objects stay drawn here.
        for (size_t i = 0; i < units.size(); ++i)
            if (!_dead[i] && !text::equalsIgnoreCase(units[i].odf, "dlight"))
                remove(i);
        return 0u;
    }
    if (name == "ReplaceObject") {
        // Another definition in its place, for its player, nothing else
        // kept (0x4375d0).
        auto unit = unitOfHandle(args.integer(0));
        if (!unit)
            return 0u;
        const MapScene::Unit was = units[*unit];
        const gfx::Vec3 at = _ships[*unit].at;
        const float heading = _ships[*unit].heading;
        remove(*unit);
        auto made = addShip(text::toLower(args.text(1)), at, heading, was);
        return made ? uint32_t(*made + 1) : 0u;
    }

    // Finding and counting.
    if (name == "CountUnitsNearPoint") {
        auto at = pathPoint(args.text(0), args.integer(4));
        if (!at)
            return 0u;
        uint32_t n = 0;
        for (size_t i = 0; i < units.size(); ++i)
            n += counted(i, args.integer(2), args.text(3)) && across(_ships[i].at, *at) <= args.real(1);
        return n;
    }
    if (name == "CountVisibleUnits") {
        // The one seeing first (0x4360b0).
        uint32_t n = 0;
        for (size_t i = 0; i < units.size(); ++i)
            n += counted(i, -1, args.text(3)) &&
                 (args.integer(1) < 0 || (args.flag(2) ? perceived(i) : units[i].slot) == args.integer(1)) &&
                 seenBy(i, args.integer(0));
        return n;
    }
    if (name == "CountVisibleUnitsNearObject") {
        // Seen by the unit's own player (0x4361d0).
        auto unit = unitOfHandle(args.integer(0));
        if (!unit)
            return 0u;
        uint32_t n = 0;
        for (size_t i = 0; i < units.size(); ++i)
            n += counted(i, -1, args.text(4)) &&
                 (args.integer(2) < 0 || (args.flag(3) ? perceived(i) : units[i].slot) == args.integer(2)) &&
                 seenBy(i, units[*unit].slot) && across(_ships[i].at, _ships[*unit].at) <= args.real(1);
        return n;
    }
    if (name == "IsVisibleToTeam") {
        auto unit = unitOfHandle(args.integer(0));
        return unit && seenBy(*unit, args.integer(1)) ? 1u : 0u;
    }
    if (name == "GetNearestObjectNearPosition" || name == "GetNearestObjectNearMe") {
        // The nearest across the plane, units and the map's objects, by
        // the tests asked; a radius under 0 is no limit; ties to the later
        // (0x435d50, 0x435ee0).
        const bool me = name == "GetNearestObjectNearMe";
        gfx::Vec3 from{};
        int32_t exclude = 0;
        if (me) {
            auto at = placeOfHandle(args.integer(0));
            if (!at)
                return 0u;
            from = *at;
            exclude = args.integer(0);
        } else {
            for (uint32_t k = 0; k < 3; ++k)
                from[k] = std::bit_cast<float>(args.cpu.read32(args.word(0) + 4 * k));
            exclude = args.integer(6);
        }
        const float radius = args.real(1);
        const int32_t player = args.integer(2);
        const std::string odf = args.text(3);
        const int32_t seer = args.integer(4);
        const bool looks = args.flag(5);
        int32_t best = 0;
        float bestDistance = std::numeric_limits<float>::max();
        auto consider = [&](int32_t handle, const gfx::Vec3 &at) {
            const float d = across(at, from);
            if ((radius < 0 || d <= radius) && d <= bestDistance)
                best = handle, bestDistance = d;
        };
        for (size_t i = 0; i < units.size(); ++i) {
            if (_dead[i] || int32_t(i + 1) == exclude || _vitals[i].derelict() ||
                (player >= 0 && (units[i].slot != player || (looks && perceived(i) != player))) ||
                (!odf.empty() && !text::equalsIgnoreCase(units[i].odf, odf)) || (seer >= 0 && !seenBy(i, seer)))
                continue;
            consider(int32_t(i + 1), _ships[i].at);
        }
        if (player < 0)
            for (size_t k = 0; k < _mapObjects.size() && k < _mapObjectOdfs.size(); ++k)
                if (odf.empty() || text::equalsIgnoreCase(_mapObjectOdfs[k], odf))
                    if (kObjectHandles + int32_t(k) != exclude &&
                        std::none_of(units.begin(), units.end(),
                                     [k](const MapScene::Unit &u) { return u.object == k; }))
                        consider(kObjectHandles + int32_t(k), _mapObjects[k].second);
        return uint32_t(best);
    }
    if (name == "GetNearestTranswarpGate" || name == "GetTranswarpExitWormhole")
        return 0u; // the transwarp gates' wormholes aren't made yet

    // The people aboard ships (0x438e00 on).
    if (name == "CreateSpecialFlag") {
        auto unit = unitOfHandle(args.integer(0));
        if (!unit)
            return 0u;
        SpecialFlag flag;
        flag.name = args.text(1);
        flag.holder = int(*unit);
        _flags[_nextFlag] = flag;
        return uint32_t(_nextFlag++);
    }
    if (name == "TransferFlag") {
        // Only one carried already moves; the restrictions aren't asked (0x439060).
        auto it = _flags.find(args.integer(0));
        auto to = unitOfHandle(args.integer(1));
        if (it != _flags.end() && it->second.holder >= 0 && to)
            it->second.holder = int(*to);
        return 0u;
    }
    if (name == "RestrictSpecialFlag") {
        auto it = _flags.find(args.integer(0));
        if (it == _flags.end())
            return 0u;
        it->second.restrictions |= args.word(1);
        if (args.kinds.size() == 3 && args.kinds[2] == 's')
            it->second.names.push_back(args.text(2));
        else if (args.kinds.size() == 3)
            it->second.handles.push_back(args.integer(2));
        return 0u;
    }
    if (name == "ClearRestrictions") {
        auto it = _flags.find(args.integer(0));
        if (it != _flags.end()) {
            it->second.restrictions = 0;
            it->second.names.clear();
            it->second.handles.clear();
        }
        return 0u;
    }
    if (name == "GetLocationOfFlag" || name == "GetRealTeamOfFlag" || name == "GetPercievedTeamOfFlag") {
        auto it = _flags.find(args.integer(0));
        const int holder = it == _flags.end() ? -1 : it->second.holder;
        if (holder >= 0 && _dead[size_t(holder)])
            it->second.holder = -1;
        const bool held = holder >= 0 && !_dead[size_t(holder)];
        if (name == "GetLocationOfFlag")
            return held ? uint32_t(holder + 1) : 0u;
        if (!held)
            return uint32_t(-1);
        return uint32_t(name == "GetRealTeamOfFlag" ? units[size_t(holder)].slot : perceived(size_t(holder)));
    }

    // The world.
    if (name == "PauseSimulation" || name == "UnpauseSimulation") {
        _paused = name == "PauseSimulation";
        return 0u;
    }
    if (name == "CenterCamera") {
        // On the path's point, or the map's middle without one (0x4372f0).
        const BznMap &map = _scene.map();
        gfx::Vec3 at{(map.area[0] + map.area[1]) / 2, 0, (map.area[4] + map.area[5]) / 2};
        auto it = _paths.find(args.text(0));
        if (it != _paths.end() && args.integer(1) >= 0 && size_t(args.integer(1)) < it->second.size())
            at = it->second[size_t(args.integer(1))];
        _overview.target[0] = at[0];
        _overview.target[2] = at[2];
        return 0u;
    }
    if (name == "SetWormholeTeam") {
        if (auto hole = wormholeOfHandle(args.integer(0)))
            _wormholes[*hole].team = args.integer(1);
        return 0u;
    }
    if (name == "AddTransporterEffect" || name == "RemoveBillboardEffect" ||
        name == "Starfield_Load_Background_Geometry" || name == "StartCredits" || name == "EndCredits")
        return 0u; // drawn only: not made yet
    return std::nullopt;
}

} // namespace reluva::storm3d
