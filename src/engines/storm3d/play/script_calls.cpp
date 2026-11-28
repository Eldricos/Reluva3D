// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What a campaign mission's scripts ask of the game: the functions they
// import from Armada.exe, as its code does them
// (docs/engines/storm3d/games/armada/systems/mission-scripts.md).

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace reluva::storm3d {

namespace {

// A unit's handle: its place in play, from 1, as 0 is none to the scripts.
// The map's other objects (its planets) come after, for their names.
constexpr int32_t kObjectHandles = 0x100000;

// The distance the program gives when there's none to give (0x7149f2ca).
constexpr float kFar = 1.0e30f;

uint32_t bitsOf(float f) {
    return std::bit_cast<uint32_t>(f);
}

} // namespace

std::optional<size_t> MissionView::unitOfHandle(int32_t handle) const {
    if (handle < 1 || handle >= kObjectHandles)
        return std::nullopt;
    const size_t unit = size_t(handle - 1);
    if (unit >= _scene.units().size() || unit >= _dead.size() || _dead[unit])
        return std::nullopt;
    return unit;
}

std::optional<gfx::Vec3> MissionView::placeOfHandle(int32_t handle) const {
    if (auto unit = unitOfHandle(handle))
        return *unit < _ships.size() ? _ships[*unit].at : _scene.units()[*unit].at;
    if (handle >= kObjectHandles && size_t(handle - kObjectHandles) < _mapObjects.size())
        return _mapObjects[size_t(handle - kObjectHandles)].second;
    return std::nullopt;
}

void MissionView::runScripts(double seconds) {
    if (!_missionScripts || !_scriptFault.empty())
        return;
    if (Error err = _missionScripts->tick(float(seconds))) {
        _scriptFault = err.message;
        return;
    }
    // The cut scene's camera after the rules, its flags for the next pass
    // (0x43db6f).
    _cine.update(float(seconds), locateForCine());
    _missionScripts->setCineractive(_cine.done(), _cine.skipped());
}

Cineractive::Locate MissionView::locateForCine() {
    // A unit by its origin and heading (+0x12c, +0x7c); a map object facing +z.
    return [this](int32_t handle) -> std::optional<Cineractive::Placed> {
        if (auto unit = unitOfHandle(handle)) {
            const Movement::Ship &ship = _ships[*unit];
            return Cineractive::Placed{ship.at, {std::sin(ship.heading), 0, std::cos(ship.heading)}};
        }
        if (auto at = placeOfHandle(handle))
            return Cineractive::Placed{*at, {0, 0, 1}};
        return std::nullopt;
    };
}

std::optional<uint32_t> MissionView::scriptCall(const std::string &name, const MissionScripts::Arguments &args) {
    const std::vector<MapScene::Unit> &units = _scene.units();
    auto pathPoint = [this](const std::string &path, int32_t point) -> std::optional<gfx::Vec3> {
        auto it = _paths.find(path);
        if (it == _paths.end() || point < 0 || size_t(point) >= it->second.size())
            return std::nullopt;
        return it->second[size_t(point)];
    };
    // The orders work only on a unit the scripts have taken (0x435b20).
    auto ordered = [this, &args](int n) -> std::optional<size_t> {
        auto unit = unitOfHandle(args.integer(n));
        if (!unit || !_scripted.count(*unit))
            return std::nullopt;
        return unit;
    };
    auto counted = [&](size_t i, int32_t player, const std::string &odf) {
        return !_dead[i] && !_vitals[i].derelict() && (player < 0 || units[i].slot == player) &&
               (odf.empty() || text::equalsIgnoreCase(units[i].odf, odf));
    };

    if (name == "GetHandle") {
        // The first object of that name, exact (0x437310): the units first,
        // the map's own after.
        const std::string wanted = args.text(0);
        for (size_t i = 0; i < units.size(); ++i)
            if (units[i].object && *units[i].object < _mapObjects.size() &&
                _mapObjects[*units[i].object].first == wanted && !_dead[i])
                return uint32_t(i + 1);
        for (size_t k = 0; k < _mapObjects.size(); ++k) {
            const bool aUnit =
                std::any_of(units.begin(), units.end(), [k](const MapScene::Unit &u) { return u.object == k; });
            if (_mapObjects[k].first == wanted && !aUnit)
                return uint32_t(kObjectHandles + int32_t(k));
        }
        return 0u;
    }
    if (name == "IsValid")
        return args.integer(0) != 0 ? 1u : 0u; // the handle alone, not whether it's alive (0x437390)
    if (name == "SetSpecialForcesFlag") {
        if (auto unit = unitOfHandle(args.integer(0))) {
            if (args.flag(1))
                _scripted.insert(*unit);
            else
                _scripted.erase(*unit);
        }
        return 0u;
    }
    if (name == "Goto") {
        auto unit = ordered(0);
        if (!unit || !_moves[*unit])
            return 0u;
        Movement::Ship &ship = _ships[*unit];
        if (args.kinds.size() > 1 && args.kinds[1] == 's') {
            // Along the path, from its first point *(inferred)*.
            auto it = _paths.find(args.text(1));
            if (it == _paths.end() || it->second.empty())
                return 0u;
            ship.route.assign(it->second.begin(), it->second.end() - 1);
            ship.goal = it->second.back();
        } else if (auto at = placeOfHandle(args.integer(1))) {
            ship.route.clear();
            ship.goal = *at;
        }
        ship.waiting = Movement::kSetOff;
        _targets[*unit] = -1;
        return 0u;
    }
    if (name == "Stop") {
        if (auto unit = ordered(0)) {
            _ships[*unit].goal.reset();
            _ships[*unit].route.clear();
            _targets[*unit] = -1;
        }
        return 0u;
    }
    if (name == "Attack" || name == "AI_Attack") {
        auto unit = ordered(0);
        auto target = unitOfHandle(args.integer(1));
        if (unit && target)
            _targets[*unit] = int(*target);
        return 0u;
    }
    if (name == "SetPosition") {
        auto unit = unitOfHandle(args.integer(0));
        auto at = pathPoint(args.text(1), 0);
        if (unit && at) {
            _ships[*unit].at = *at;
            _ships[*unit].goal.reset();
            _ships[*unit].route.clear();
            _scene.moveUnit(*unit, *at, _ships[*unit].heading);
        }
        return 0u;
    }
    if (name == "PointAt") {
        auto unit = unitOfHandle(args.integer(0));
        auto at = placeOfHandle(args.integer(1));
        if (unit && at) {
            const gfx::Vec3 &from = _ships[*unit].at;
            _ships[*unit].heading = std::atan2((*at)[0] - from[0], (*at)[2] - from[2]);
            _scene.moveUnit(*unit, from, _ships[*unit].heading);
        }
        return 0u;
    }
    if (name == "BuildObject") {
        // At once, at the path's point, for the player (0x437550).
        auto at = pathPoint(args.text(2), args.integer(3));
        if (!at)
            return 0u;
        MapScene::Unit owner;
        owner.slot = args.integer(1);
        for (const MapScene::Unit &u : units)
            if (u.slot == owner.slot) {
                owner.colour = u.colour;
                break;
            }
        auto unit = addShip(text::toLower(args.text(0)), *at, 0, owner);
        return unit ? uint32_t(*unit + 1) : 0u;
    }
    if (name == "RemoveObject") {
        if (auto unit = unitOfHandle(args.integer(0))) {
            _dead[*unit] = true;
            _scene.hideUnit(*unit);
            forget(*unit);
        }
        return 0u;
    }
    if (name == "CraftCannotDie") {
        if (auto unit = unitOfHandle(args.integer(0))) {
            if (args.flag(1))
                _cannotDie.insert(*unit);
            else
                _cannotDie.erase(*unit);
        }
        return 0u;
    }
    if (name == "DisableWeapons" || name == "DisableEngines" || name == "DisableShieldGenerator") {
        // Off until turned on again (each system's byte 0x30).
        const Combat::System system = name == "DisableWeapons"   ? Combat::kWeapons
                                      : name == "DisableEngines" ? Combat::kEngines
                                                                 : Combat::kShieldGenerator;
        if (auto unit = unitOfHandle(args.integer(0)))
            _vitals[*unit].systems[system].offline = args.flag(1) ? std::numeric_limits<float>::max() : 0;
        return 0u;
    }
    if (name == "DestroyWeapons" || name == "DestroyLifeSupport") {
        if (auto unit = unitOfHandle(args.integer(0)))
            _vitals[*unit].systems[name == "DestroyWeapons" ? Combat::kWeapons : Combat::kLifeSupport].hitPoints = 0;
        return 0u;
    }
    if (name == "DestroyCritical") {
        // A critical hit: the unit gone *(inferred)*.
        if (auto unit = unitOfHandle(args.integer(0)))
            destroy(*unit, -1);
        return 0u;
    }
    if (name == "SetShieldPercent") {
        // A share from 0 to 1, whatever its name says (0x437dd0).
        if (auto unit = unitOfHandle(args.integer(0)))
            _vitals[*unit].shields = _vitals[*unit].maxShields * std::clamp(args.real(1), 0.0f, 1.0f);
        return 0u;
    }
    if (name == "SetDilithium") {
        Hud::Resources *bank = bankOf(args.integer(0));
        if (!bank)
            return 0u;
        bank->dilithium = args.integer(1);
        return uint32_t(bank->dilithium);
    }
    if (name == "DisableAllAITeams" || name == "EnableAllAITeams") {
        _computersOff = name == "DisableAllAITeams";
        return 0u;
    }
    if (name == "GetLocation") {
        // A reference to its place; the script copies it at once.
        const gfx::Vec3 at = placeOfHandle(args.integer(0)).value_or(gfx::Vec3{});
        for (uint32_t k = 0; k < 3; ++k)
            args.cpu.write32(args.scratch + 4 * k, bitsOf(at[k]));
        return args.scratch;
    }
    if (name == "GetDistance") {
        // From the unit (its handle's address) to the path's point, across
        // the plane, the point kept within the path (0x436480).
        auto unit = unitOfHandle(int32_t(args.cpu.read32(args.word(0))));
        auto it = _paths.find(args.text(1));
        if (!unit || it == _paths.end() || it->second.empty())
            return bitsOf(kFar);
        const size_t point = size_t(std::clamp<int32_t>(args.integer(2), 0, int32_t(it->second.size()) - 1));
        const gfx::Vec3 &a = _ships[*unit].at;
        const gfx::Vec3 &b = it->second[point];
        return bitsOf(std::hypot(a[0] - b[0], a[2] - b[2]));
    }
    if (name == "CountUnits") {
        uint32_t n = 0;
        for (size_t i = 0; i < units.size(); ++i)
            n += counted(i, args.integer(0), args.text(1));
        return n;
    }
    if (name == "CountUnitsNearObject") {
        // Within the radius across the plane, itself too (0x435f60).
        auto from = placeOfHandle(args.integer(0));
        if (!from || !unitOfHandle(args.integer(0)))
            return 0u;
        const float radius = args.real(1);
        uint32_t n = 0;
        for (size_t i = 0; i < units.size(); ++i)
            n += counted(i, args.integer(2), args.text(3)) &&
                 std::hypot(_ships[i].at[0] - (*from)[0], _ships[i].at[2] - (*from)[2]) <= radius;
        return n;
    }
    if (name == "GetNearestEnemy") {
        // In the square of the range about it, the nearest unit of another
        // player's, across the plane (0x4368e0).
        auto unit = unitOfHandle(args.integer(0));
        if (!unit)
            return 0u;
        const float range = args.real(1);
        const gfx::Vec3 &a = _ships[*unit].at;
        int best = -1;
        float bestDistance = 0;
        for (size_t i = 0; i < units.size(); ++i) {
            const gfx::Vec3 &b = _ships[i].at;
            if (_dead[i] || units[i].slot == units[*unit].slot || units[i].slot < 1 || std::abs(b[0] - a[0]) > range ||
                std::abs(b[2] - a[2]) > range)
                continue;
            const float d = std::hypot(b[0] - a[0], b[2] - a[2]);
            if (best < 0 || d < bestDistance)
                best = int(i), bestDistance = d;
        }
        return best < 0 ? 0u : uint32_t(best + 1);
    }
    if (name == "GetNearestStarbase") {
        auto unit = unitOfHandle(args.integer(0));
        if (!unit)
            return 0u;
        int best = -1;
        float bestDistance = 0;
        for (size_t i = 0; i < units.size(); ++i) {
            if (_dead[i] || _classes[i] != "starbase" || (args.integer(1) >= 0 && units[i].slot != args.integer(1)))
                continue;
            const gfx::Vec3 &a = _ships[*unit].at;
            const gfx::Vec3 &b = _ships[i].at;
            const float d = std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
            if (best < 0 || d < bestDistance)
                best = int(i), bestDistance = d;
        }
        return best < 0 ? 0u : uint32_t(best + 1);
    }
    if (name == "InAttackMode") {
        auto unit = unitOfHandle(args.integer(0));
        return unit && _targets[*unit] >= 0 ? 1u : 0u;
    }
    if (name == "InQueue") {
        // Orders still to carry out: somewhere to go *(inferred)*.
        auto unit = unitOfHandle(args.integer(0));
        return unit && _ships[*unit].goal ? 1u : 0u;
    }
    if (name == "IsCompromised") {
        // Gone, derelict, or another player's (0x436df0).
        auto unit = unitOfHandle(args.integer(0));
        return !unit || _vitals[*unit].derelict() || units[*unit].slot != args.integer(1) ? 1u : 0u;
    }
    if (name == "SucceedMission" || name == "FailMission") {
        _missionEnd = {double(args.real(0)), name == "FailMission"};
        return 0u;
    }
    if (name == "CineractiveStart" || name == "CineractiveEnd") {
        if (name == "CineractiveStart") {
            _cine.start();
            return 0u;
        }
        // Its end puts every unit's speeds back to its definition's (0x4ed020).
        if (_cine.on())
            for (const auto &[unit, speed] : _warpSpeeds)
                if (unit < _physics.size() && _physics[unit])
                    _physics[unit]->topSpeed = speed;
        if (_cine.on())
            _warpSpeeds.clear();
        _cine.end();
        return 0u;
    }
    if (name == "CineractiveShot") {
        // By the overload, its arguments' kinds (0x4ec6b0, 0x4ebf20,
        // 0x4ebd90, 0x4ecab0, 0x4ec290); a path or point that isn't there,
        // nothing.
        const Cineractive::Locate locate = locateForCine();
        const std::string &k = args.kinds;
        auto pathOf = [this, &args](int n) -> const std::vector<gfx::Vec3> * {
            auto it = _paths.find(args.text(n));
            return it == _paths.end() || it->second.empty() ? nullptr : &it->second;
        };
        if (k == "ii") {
            _cine.shotFollow(args.integer(0), args.integer(1), locate);
        } else if (k == "sii") {
            if (auto eye = pathPoint(args.text(0), args.integer(1)))
                _cine.shotAt(*eye, args.integer(2), locate);
        } else if (k == "sisi") {
            auto eye = pathPoint(args.text(0), args.integer(1));
            auto look = pathPoint(args.text(2), args.integer(3));
            if (eye && look)
                _cine.shotAt(*eye, *look, locate);
        } else if (k == "sfsi") {
            auto look = pathPoint(args.text(2), args.integer(3));
            if (const std::vector<gfx::Vec3> *eye = pathOf(0); eye && look)
                _cine.shotAlong(*eye, args.real(1), *look, locate);
        } else if (k == "sisf") {
            auto eye = pathPoint(args.text(0), args.integer(1));
            if (const std::vector<gfx::Vec3> *look = pathOf(2); eye && look)
                _cine.shotLookingAlong(*eye, *look, args.real(3), locate);
        } else {
            return std::nullopt; // the shots along a heading, not made yet
        }
        return 0u;
    }
    if (name == "CineractiveCameraOffset" || name == "CineractiveTargetOffset") {
        const Cineractive::Locate locate = locateForCine();
        const bool camera = name == "CineractiveCameraOffset";
        if (args.kinds == "sfb") {
            auto it = _paths.find(args.text(0));
            if (it == _paths.end())
                return 0u;
            if (camera)
                _cine.cameraOffset(it->second, args.real(1), args.flag(2), locate);
            else
                _cine.targetOffset(it->second, args.real(1), args.flag(2), locate);
        } else if (args.kinds == "fffb") {
            const gfx::Vec3 by{args.real(0), args.real(1), args.real(2)};
            if (camera)
                _cine.cameraOffset(by, args.flag(3), locate);
            else
                _cine.targetOffset(by, args.flag(3), locate);
        } else {
            return std::nullopt;
        }
        return 0u;
    }
    if (name == "FadeIn" || name == "FadeOut") {
        // Over its seconds, after its delay; the third not 0, white, else black.
        _cine.fade(name == "FadeOut", args.real(0), args.real(1), args.real(2) != 0);
        return 0u;
    }
    if (name == "SetWarpSpeed") {
        // Its warp speed, as its definition's warpSpeed (0x438d80).
        auto unit = unitOfHandle(args.integer(0));
        if (unit && *unit < _physics.size() && _physics[*unit]) {
            _warpSpeeds.emplace(*unit, _physics[*unit]->topSpeed);
            _physics[*unit]->topSpeed = args.real(1);
        }
        return 0u;
    }
    if (name == "GridVisible") {
        _gridShown = args.flag(0);
        return 0u;
    }
    if (name == "FadeRemove") {
        _cine.removeFade();
        return 0u;
    }
    if (name == "AudioMessage")
        return uint32_t(++_audioMessages);
    if (name == "IsAudioMessageDone")
        return 1u; // no voices played yet: each done at once
    return std::nullopt;
}

} // namespace reluva::storm3d
