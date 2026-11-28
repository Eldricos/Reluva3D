// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What a campaign mission's scripts ask of the game: the functions they
// import from Armada.exe, as its code does them
// (docs/engines/storm3d/games/armada/systems/mission-scripts.md).

#include "engines/storm3d/play/mission_view.h"

#include "audio/wav.h"
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
    // (0x43db6f); its titles fading in or out over a second, gone faded out
    // (0x494330).
    _cine.update(float(seconds), locateForCine());
    if (_cine.on())
        for (auto it = _overlayTexts.begin(); it != _overlayTexts.end();) {
            OverlayText &t = it->second;
            t.fade = std::clamp(t.fade + float(t.shown ? seconds : -seconds), 0.0f, 1.0f);
            it = !t.shown && t.fade <= 0 ? _overlayTexts.erase(it) : std::next(it);
        }
    // The voices finished leave their ids, which reads them done (0x59e880).
    std::erase_if(_voices, [this](const auto &v) { return double(_last) / 1000 >= v.second.until; });
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
            // To a wormhole, or a point within 5 of one, is through it (0x425b7e).
            auto hole = wormholeOfHandle(args.integer(1));
            if (!hole)
                hole = wormholeNear(*at);
            _targets[*unit] = -1;
            if (hole) {
                sendThrough(*unit, *hole);
                return 0u;
            }
            ship.route.clear();
            ship.goal = *at;
        }
        ship.waiting = Movement::kSetOff;
        _targets[*unit] = -1;
        return 0u;
    }
    if (name == "Stop") {
        if (auto unit = ordered(0)) {
            _boundFor.erase(*unit);
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
        // In a wormhole's queue: on its way in or out (0x434e90).
        auto unit = unitOfHandle(args.integer(0));
        return unit && inWormholeQueue(*unit) ? 1u : 0u;
    }
    if (name == "MinimapMessage") {
        // A white ring at the unit, or a path's point, shrinking from `n`
        // pixels a pixel each `f` seconds, once (0x437150, 0x437110).
        std::optional<gfx::Vec3> at;
        if (args.kinds.size() == 4)
            at = pathPoint(args.text(0), args.integer(3));
        else if (unitOfHandle(args.integer(0)))
            at = placeOfHandle(args.integer(0));
        if (at) {
            Marker m;
            m.x = (*at)[0];
            m.z = (*at)[2];
            m.radius = m.now = args.integer(1);
            m.shrink = args.real(2);
            _markers.push_back(m);
        }
        return 0u;
    }
    if (name == "ObstacleAvoidence") {
        _avoiding = args.flag(0);
        return 0u;
    }
    if (name == "LinkWormHoles") {
        // Each the other's partner; their old partners keep their one-way
        // links (0x4516a0).
        auto a = wormholeOfHandle(args.integer(0));
        auto b = wormholeOfHandle(args.integer(1));
        if (a && b) {
            _wormholes[*a].partner = int(*b);
            _wormholes[*b].partner = int(*a);
        }
        return 0u;
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
        // The music stops for a cut scene, another track after (0x4eba80, 0x4ed0b9).
        if (name == "CineractiveStart") {
            if (!_cine.on())
                stopMusic();
            _cine.start();
            return 0u;
        }
        if (_cine.on())
            playMusic();
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
    if (name == "PreLoadBridgeScene")
        return 0u; // its pictures read ahead, which these needn't be
    if (name == "SetBridgeSceneRace") {
        _bridgeType = args.integer(0);
        return 0u;
    }
    if (name == "ShowBridgeScene") {
        // Its lights from the start again (0x48faf0).
        _bridgeShown = args.flag(0);
        _bridgeLightsFrom = double(_last) / 1000;
        return 0u;
    }
    if (name == "ShowBridgeOnScreen") {
        _bridgeOnScreen = args.flag(0);
        return 0u;
    }
    if (name == "PlayBridgeMovie") {
        // From animations/, into the view screen, played its number of
        // times (0 or under, until stopped), with its sound at the voice
        // volume; one already open, nothing (0x47a5f0).
        if (_bridgeMoviePlaying)
            return 0u;
        auto movie = std::make_unique<PanelMovie>();
        if (movie->open(_files, "animations/" + args.text(0)))
            return 0u;
        const std::shared_ptr<audio::Sound> sound = movie->sound();
        movie->restart(_last);
        _bridgeMovie = std::move(movie);
        _bridgeMoviePlaying = true;
        _bridgeMoviePasses = args.integer(1);
        _bridgeMovieRect = bridgeScreen();
        if (_mixer && sound)
            _bridgeMovieSound =
                _mixer->play(sound, audio::SoundType::kSpeech, audio::kMaxVolume, 0, _bridgeMoviePasses <= 0);
        return 0u;
    }
    if (name == "StopMovie") {
        stopBridgeMovie();
        return 0u;
    }
    if (name == "IsMoviePlaying")
        return _bridgeMoviePlaying ? 1u : 0u;
    if (name == "GridVisible") {
        _gridShown = args.flag(0);
        return 0u;
    }
    if (name == "FadeRemove") {
        _cine.removeFade();
        return 0u;
    }
    if (name == "ShowTitle" || name == "ShowSubtitle") {
        // Titles blue (0, 0.5, 1) in the title font, subtitles white in the
        // default one; faded in over a second, or shown whole (0x437190).
        OverlayText t;
        t.area = {args.integer(0), args.integer(1), args.integer(2), args.integer(3)};
        t.text = args.text(4);
        t.title = name == "ShowTitle";
        t.colour = t.title ? std::array<float, 3>{0, 0.5f, 1} : std::array<float, 3>{1, 1, 1};
        t.fade = args.flag(5) ? 0 : 1;
        const int id = _nextOverlayText++;
        _overlayTexts[id] = t;
        return uint32_t(id);
    }
    if (name == "RemoveTitle" || name == "RemoveSubtitle") {
        // Faded out, or gone at once.
        auto it = _overlayTexts.find(args.integer(0));
        if (it != _overlayTexts.end()) {
            if (args.flag(1))
                it->second.shown = false;
            else
                _overlayTexts.erase(it);
        }
        return 0u;
    }
    if (name == "ObjectivesDisplay_Set_Text_From_File") {
        // From the install's files (objtxt.zfs); "null" or none, nothing.
        const std::string file = args.text(0);
        std::vector<uint8_t> data;
        if (!file.empty() && !text::toLower(file).starts_with("null") && !_files.read(file, data))
            _objectives.assign(data.begin(), data.end());
        return 0u;
    }
    if (name == "ObjectivesDisplay_Set_Visibility") {
        _objectivesShown = args.flag(0);
        return 0u;
    }
    if (name == "AudioMessage") {
        // Found by its name wherever it is, as the game's file index finds
        // it; played once at the voice volume, done when it ends.
        const int id = _nextVoice++;
        VoiceMessage voice;
        voice.until = double(_last) / 1000;
        const std::string path = pathByName(args.text(0));
        auto sound = std::make_shared<audio::Sound>();
        std::vector<uint8_t> data;
        if (!path.empty() && !_files.read(path, data) && !audio::decodeWav(data, *sound) && sound->rate > 0) {
            voice.until += double(sound->frames()) / sound->rate;
            if (_mixer)
                voice.handle = _mixer->play(sound, audio::SoundType::kSpeech);
        }
        _voices[id] = voice;
        return uint32_t(id);
    }
    if (name == "IsAudioMessageDone")
        return _voices.count(args.integer(0)) ? 0u : 1u;
    if (name == "StopAudioMessage") {
        auto it = _voices.find(args.integer(0));
        if (it != _voices.end()) {
            if (_mixer && it->second.handle != audio::kNoSound)
                _mixer->stop(it->second.handle);
            _voices.erase(it);
        }
        return 0u;
    }
    return std::nullopt;
}

void MissionView::runMarkers(double seconds) {
    // Each shrinks a pixel per `shrink` seconds; at nothing it's gone if
    // its time is spent, else starts again (0x481c90).
    for (size_t i = 0; i < _markers.size();) {
        Marker &m = _markers[i];
        m.timer += float(seconds);
        m.life -= float(seconds);
        while (m.shrink > 0 && m.timer > m.shrink) {
            m.timer -= m.shrink;
            --m.now;
        }
        if (m.now < 1) {
            if (m.life <= 0) {
                _markers.erase(_markers.begin() + std::ptrdiff_t(i));
                continue;
            }
            m.now = m.radius;
        }
        ++i;
    }
}

std::string MissionView::pathByName(const std::string &name) {
    if (_filesByName.empty())
        for (const std::string &path : _files.list()) {
            const size_t slash = path.find_last_of('/');
            _filesByName.emplace(text::toLower(slash == std::string::npos ? path : path.substr(slash + 1)), path);
        }
    auto found = _filesByName.find(text::toLower(name));
    return found == _filesByName.end() ? std::string() : found->second;
}

void MissionView::playMusic() {
    // A track at random, not the one just played if there are two or more
    // (0x5a9340), at the music volume; another when it ends.
    stopMusic();
    _jukebox = true;
    if (_tracks.empty() || !_mixer)
        return;
    int pick = 0;
    do {
        _trackRoll = _trackRoll * 1664525u + 1013904223u;
        pick = int((_trackRoll >> 8) % uint32_t(_tracks.size()));
    } while (_tracks.size() > 1 && pick == _lastTrack);
    _lastTrack = pick;
    auto sound = std::make_shared<audio::Sound>();
    std::vector<uint8_t> data;
    const std::string path = pathByName(_tracks[size_t(pick)]);
    if (!path.empty() && !_files.read(path, data) && !audio::decodeWav(data, *sound))
        _track = _mixer->play(sound, audio::SoundType::kMusic);
}

void MissionView::stopMusic() {
    if (_mixer && _track != audio::kNoSound)
        _mixer->stop(_track);
    _track = audio::kNoSound;
    _jukebox = false;
}

void MissionView::runMusic() {
    // On by itself at its third update, unless a cut scene is on
    // (0x5a9310); then a track after each.
    if (_jukeboxUpdates < 3 && ++_jukeboxUpdates == 3 && !_cine.on())
        playMusic();
    if (_jukebox && _mixer && _track != audio::kNoSound && !_mixer->isPlaying(_track))
        playMusic();
}

std::array<int, 4> MissionView::bridgeScreen() const {
    // Each race's view screen, 400 x 200, where its picture is clear
    // (0x48f5b0); another type, the default's.
    switch (_bridgeType) {
    case 1:
        return {220, 10, 400, 200};
    case 2:
        return {154, 128, 400, 200};
    case 3:
        return {84, 148, 400, 200};
    case 4:
        return {184, 126, 400, 200};
    default:
        return {62, 110, 400, 200};
    }
}

std::string MissionView::bridgePrefix() const {
    static const char *const kPrefixes[] = {"d_", "f_", "k_", "r_", "b_"};
    return kPrefixes[_bridgeType >= 1 && _bridgeType <= 4 ? _bridgeType : 0];
}

void MissionView::stopBridgeMovie() {
    // Closed; its last frame stays while the bridge shows it (0x47a980).
    _bridgeMoviePlaying = false;
    if (_mixer && _bridgeMovieSound != audio::kNoSound)
        _mixer->stop(_bridgeMovieSound);
    _bridgeMovieSound = audio::kNoSound;
}

void MissionView::drawCutScene(bool clear) {
    // Only the titles and the fade over the picture (input mode 2); the
    // fade over the titles, as the original's lift with the picture
    // (0x4942e7, 0x495720).
    if (_overlay.w != 640)
        _overlay.create(640, 480, PixelFormat::kRGBA32);
    if (clear)
        std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    for (const auto &[id, t] : _overlayTexts)
        _hud.drawOverlayText(_overlay, t.area, t.text, t.colour, t.fade, t.title);
    if (auto fade = _cine.fadeOver()) {
        const float a = std::clamp(fade->second, 0.0f, 1.0f);
        for (size_t p = 0; p + 3 < _overlay.pixels.size(); p += 4) {
            uint8_t *px = &_overlay.pixels[p];
            for (size_t k = 0; k < 3; ++k)
                px[k] = uint8_t(std::lround(float(px[k]) * (1 - a) + fade->first[k] * 255 * a));
            px[3] = uint8_t(std::lround(float(px[3]) + (255 - float(px[3])) * a));
        }
    }
}

} // namespace reluva::storm3d
