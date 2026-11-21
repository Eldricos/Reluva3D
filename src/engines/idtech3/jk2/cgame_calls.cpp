// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Jedi Outcast's client game calls (docs/engines/idtech3/games/jk2/systems/cgame-calls.md):
// the order of the GPL multiplayer cg_public.h, each checked against what
// 1.02a's cgame.qvm passes. Those it shares with Elite Force's are
// answered as Elite Force's; these are the rest.

#include "engines/idtech3/eliteforce/cgame_host.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace reluva::idtech3::eliteforce {

namespace {

enum Jk2CgameCall : int {
    kRemoveCommand = 16,
    kMuteSound = 31,
    kStopLoopingSound = 38,
    kCeil = 111,
    kRegisterFont = 47, // to 52: the text calls, in FontSet's order
    kReadCharFromString = 52,
    kAddPolysToScene = 203,
    kLightForPoint = 204,
    kDrawRotatePic = 212,
    kDrawRotatePic2 = 213,
    kRemapShader = 214,
    kGetLightStyle = 215,
    kSetLightStyle = 216,
    kGetBmodelVerts = 217,
    kFxAddLine = 218,
    kSetClientForceAngle = 227,
    kSetClientTurnExtent = 228,
    kOpenUiMenu = 229,
    kTestPrintInt = 230,
    kTestPrintFloat = 231,
    kKeyIsDown = 233,
    kKeyGetCatcher = 234,
    kKeySetCatcher = 235,
    kKeyGetKey = 236,
    kPcAddGlobalDefine = 237, // to 243: the script calls, in ScriptCalls' order
    kPcRemoveAllGlobalDefines = 243,
    kStopBackgroundTrack = 244,
    kRealTime = 245,
    kSnapVector = 246,
    kCinPlayCinematic = 247,
    kCinSetExtents = 251,
    kGetEntityToken = 252,
    kInPvs = 253,
    kFxRegisterEffect = 254,
    kFxAddSprite = 269, // 254 to 269 and 218: the effects calls (cgame_effects.cpp)
    kSpPrint = 270,
    kSpGetStringTextString = 271,
    kSpRegister = 272,
    kRoffClean = 273,
    kRoffPurgeEnt = 277,
    kG2ListSurfaces = 278, // to 299: Ghoul2's
    kG2SetNewOrigin = 299,
    kSetSharedBuffer = 300,
};

} // namespace

int32_t CgameHost::jk2SharedCall(int32_t n) {
    // Jedi Outcast's number -> Elite Force's, where the call is the same
    // (a capsule's as the box's, told apart by the caller; an additive or
    // "real" looping sound or light as the plain one).
    if (n >= 0 && n <= 15)
        return n;
    switch (n) {
    case 17:
        return 16; // SendClientCommand
    case 18:
        return 17; // UpdateScreen
    case 19:
        return 18; // CM_LoadMap
    case 20:
        return 19; // CM_NumInlineModels
    case 21:
        return 20; // CM_InlineModel
    case 22:
        return 22; // CM_TempBoxModel
    case 23:
        return 22; // CM_TempCapsuleModel
    case 24:
        return 23; // CM_PointContents
    case 25:
        return 24; // CM_TransformedPointContents
    case 26:
        return 25; // CM_BoxTrace
    case 27:
        return 25; // CM_CapsuleTrace
    case 28:
        return 26; // CM_TransformedBoxTrace
    case 29:
        return 26; // CM_TransformedCapsuleTrace
    case 30:
        return 27; // CM_MarkFragments
    case 32:
        return 28; // S_StartSound
    case 33:
        return 29; // S_StartLocalSound
    case 34:
        return 30; // S_ClearLoopingSounds
    case 35:
        return 31; // S_AddLoopingSound
    case 36:
        return 32; // S_UpdateEntityPosition
    case 37:
        return 31; // S_AddRealLoopingSound
    case 39:
        return 33; // S_Respatialize
    case 40:
        return 34; // S_RegisterSound
    case 41:
        return 35; // S_StartBackgroundTrack
    case 42:
        return 36; // R_LoadWorldMap
    case 43:
        return 37; // R_RegisterModel
    case 44:
        return 38; // R_RegisterSkin
    case 45:
        return 39; // R_RegisterShader
    case 46:
        return 57; // R_RegisterShaderNoMip
    case 110:
        return 107; // floor
    case 200:
        return 40; // R_ClearScene
    case 201:
        return 41; // R_AddRefEntityToScene
    case 202:
        return 42; // R_AddPolyToScene
    case 205:
        return 43; // R_AddLightToScene
    case 206:
        return 43; // R_AddAdditiveLightToScene
    case 207:
        return 44; // R_RenderScene
    case 208:
        return 45; // R_SetColor
    case 209:
        return 46; // R_DrawStretchPic
    case 210:
        return 47; // R_ModelBounds
    case 211:
        return 48; // R_LerpTag
    case 219:
        return 49; // GetGlconfig
    case 220:
        return 50; // GetGameState
    case 221:
        return 51; // GetCurrentSnapshotNumber
    case 222:
        return 52; // GetSnapshot
    case 223:
        return 53; // GetServerCommand
    case 224:
        return 54; // GetCurrentCmdNumber
    case 225:
        return 55; // GetUserCmd
    case 226:
        return 56; // SetUserCmdValue
    case 232:
        return 58; // MemoryRemaining
    default:
        return -1;
    }
}

int32_t CgameHost::jk2Call(Qvm &vm, int number, const int32_t *a) {
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    // Word k of the vector argument i points at.
    auto f3 = [&](int i, int k) { return Qvm::asFloat(vm.readInt(a[i] + 4 * k)); };
    auto draw = [this](float x, float y, float w, float h, float s1, float t1, float s2, float t2, int32_t shader,
                       const std::array<float, 4> &color) {
        _pictures.push_back({x, y, w, h, s1, t1, s2, t2, shader, color});
    };
    if (_release != JediRelease::k102a) {
        // 1.03's numbering (jk2mp.exe 1.03's jump table): the language's
        // calls at 52 to 54 (ReadCharFromString with its new arguments);
        // Ghoul2's GetBolt_NoRec_NoRot new at 285, CollisionDetect at 287,
        // and 1.02a's 285 to 300 at 286 and 288 to 302.
        const int32_t language = _cvars.integer("sp_language");
        if (number == kReadCharFromString)
            return language == 4 || language == 5 || language == 8 ? 1 : 0; // Language_IsAsian
        if (number == kReadCharFromString + 1)
            return language == 5 || language == 8 ? 0 : 1; // Language_UsesSpaces
        if (number == kReadCharFromString + 2) {
            _fonts.language = language;
            return _fonts.call(vm, jk2::FontSet::kReadCharAdvance, a, _time, draw);
        }
        if (number == kG2ListSurfaces + 7)
            return _ghoul2.call(vm, jk2::Ghoul2Instances::kGetBoltUnturned, a);
        if (number == kG2ListSurfaces + 8)
            number = kG2ListSurfaces + 7;
        else if (number == kG2ListSurfaces + 9)
            return _ghoul2.call(vm, jk2::Ghoul2Instances::kCollisionDetect, a);
        else if (number >= kG2ListSurfaces + 10 && number <= kSetSharedBuffer + 2)
            number -= 2;
    }
    if (number >= kRegisterFont && number <= kReadCharFromString)
        return _fonts.call(vm, jk2::FontSet::Call(number - kRegisterFont), a, _time, draw);
    if (number >= kPcAddGlobalDefine && number <= kPcRemoveAllGlobalDefines)
        return _scripts.call(vm, botlib::ScriptCalls::Call(number - kPcAddGlobalDefine), a);
    if (number == kFxAddLine || (number >= kFxRegisterEffect && number <= kFxAddSprite))
        return jk2EffectCall(vm, number, a);
    if (number >= kG2ListSurfaces && number <= kG2SetNewOrigin) {
        // Ghoul2's, in the client game's own order.
        using G = jk2::Ghoul2Instances;
        static constexpr G::Call kOrder[] = {
            G::kListSurfaces,
            G::kListBones,
            G::kSetModels,
            G::kHaveWeGhoulModels,
            G::kGiveMeVectorFromMatrix,
            G::kGetBolt,
            G::kGetBoltNoRec,
            G::kInitGhoul2Model,
            G::kCleanModels,
            G::kAngleOverride,
            G::kPlayAnim,
            G::kGetGlaName,
            G::kCopyGhoul2Instance,
            G::kCopySpecificModel,
            G::kDuplicateInstance,
            G::kHasGhoul2ModelOnIndex,
            G::kRemoveGhoul2Model,
            G::kAddBolt,
            G::kSetBoltInfo,
            G::kSetRootSurface,
            G::kSetSurfaceOnOff,
            G::kSetNewOrigin,
        };
        return _ghoul2.call(vm, kOrder[number - kG2ListSurfaces], a);
    }
    if (number >= kRoffClean && number <= kRoffPurgeEnt)
        return jk2RoffCall(vm, number, a);
    switch (number) {
    case kRemoveCommand: {
        const std::string name = str(0, 64);
        _commands.erase(std::remove(_commands.begin(), _commands.end(), name), _commands.end());
        return 0;
    }
    case kMuteSound: // (entityNum, channel)
        if (soundSystem)
            soundSystem->muteSound(a[0], a[1]);
        return 0;
    case kStopLoopingSound: // (entityNum)
        if (soundSystem)
            soundSystem->stopLoopingSound(a[0]);
        return 0;
    case kCeil:
        return Qvm::fromFloat(std::ceil(f(0)));
    case kAddPolysToScene: // (shader, vertices each, vertices, polygons)
        for (int32_t i = 0; i < a[3]; ++i)
            _scene.addPoly(vm, a[0], a[1], a[2] + i * a[1] * 24);
        return 0;
    case kLightForPoint: { // (point, ambient, directed, direction) -> whether the level has a light grid
        // The light as the grid has it, without the models' least light,
        // as the original's renderer gives it the games.
        if (!_lightGrid || _lightGrid->empty() || !vm.valid(a[0], 12) || !vm.checkWrite(a[1], 12) ||
            !vm.checkWrite(a[2], 12) || !vm.checkWrite(a[3], 12))
            return 0;
        const PointLight light = _lightGrid->sample({f3(0, 0), f3(0, 1), f3(0, 2)}, false);
        for (int i = 0; i < 3; ++i) {
            vm.writeInt(a[1] + 4 * i, Qvm::fromFloat(light.ambient[size_t(i)]));
            vm.writeInt(a[2] + 4 * i, Qvm::fromFloat(light.directed[size_t(i)]));
            vm.writeInt(a[3] + 4 * i, Qvm::fromFloat(light.direction[size_t(i)]));
        }
        return 1;
    }
    case kDrawRotatePic:
    case kDrawRotatePic2: { // (x, y, w, h, s1, t1, s2, t2, degrees, shader)
        Picture p{f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[9], _color};
        p.angle = f(8);
        _pictures.push_back(p);
        return 0;
    }
    case kGetLightStyle: // (style, rgba out)
        if (a[0] >= 0 && a[0] < kStyleCount && vm.checkWrite(a[1], 4))
            for (int i = 0; i < 4; ++i)
                vm.memory()[size_t(a[1] + i)] = _lightStyles[size_t(a[0])][size_t(i)];
        return 0;
    case kSetLightStyle: // (style, rgba packed in a word: red its lowest byte)
        if (a[0] >= 0 && a[0] < kStyleCount)
            for (int i = 0; i < 4; ++i)
                _lightStyles[size_t(a[0])][size_t(i)] = uint8_t(uint32_t(a[1]) >> (8 * i));
        return 0;
    case kRemapShader: // (old shader, new shader, time offset as text)
        _shaderRemaps.push_back({str(0, 64), str(1, 64), float(std::atof(str(2, 32).c_str()))});
        return 0;
    case kGetBmodelVerts: { // (inline model "*n", verts[4] out, normal): a breaking pane's corners
        // As the GPL renderer's RE_GetBModelVerts: of the model's two
        // largest faces (each quad's two triangles' areas, squared and
        // quartered, summed and cut to whole numbers), the one facing the
        // last view, else the largest; its first four corners. The normal
        // isn't written.
        const std::string &name = _models.name(a[0]);
        if (!_map || name.size() < 2 || name[0] != '*' || !vm.checkWrite(a[1], 48))
            return 0;
        const auto index = size_t(std::atoi(name.c_str() + 1));
        if (index >= _map->models.size())
            return 0;
        const BspModel &model = _map->models[index];
        auto corner = [&](const BspSurface &s, int k) {
            const size_t at = size_t(s.firstVertex + std::min(k, std::max(0, s.numVertices - 1)));
            return at < _map->vertices.size() ? _map->vertices[at].pos : std::array<float, 3>{};
        };
        auto area = [&](const BspSurface &s) {
            const auto v1 = corner(s, 0);
            const auto v2 = corner(s, 1);
            const auto v3 = corner(s, 2);
            const auto v4 = corner(s, 3);
            auto quarterCrossSquared = [](const std::array<float, 3> &o, const std::array<float, 3> &p,
                                          const std::array<float, 3> &q) {
                const float a1[3] = {o[0] - p[0], o[1] - p[1], o[2] - p[2]};
                const float a2[3] = {o[0] - q[0], o[1] - q[1], o[2] - q[2]};
                const float c[3] = {(a1[1] * a2[2] - a1[2] * a2[1]) * 0.25f, (a1[2] * a2[0] - a1[0] * a2[2]) * 0.25f,
                                    (a1[0] * a2[1] - a1[1] * a2[0]) * 0.25f};
                return c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
            };
            return int32_t(quarterCrossSquared(v1, v2, v4) + quarterCrossSquared(v3, v2, v4));
        };
        std::array<int32_t, 2> best{};
        std::array<int32_t, 2> bestIndex{};
        for (int32_t i = 0; i < model.numSurfaces; ++i) {
            const size_t s = size_t(model.firstSurface + i);
            if (s >= _map->surfaces.size())
                break;
            const int32_t d = area(_map->surfaces[s]);
            if (d > best[0]) {
                best[1] = best[0];
                bestIndex[1] = bestIndex[0];
                best[0] = d;
                bestIndex[0] = i;
            } else if (d >= best[1]) {
                best[1] = d;
                bestIndex[1] = i;
            }
        }
        auto facing = [&](int32_t i) {
            const size_t s = size_t(model.firstSurface + i);
            if (s >= _map->surfaces.size())
                return 0.0f;
            const auto &n = _map->surfaces[s].lightmapVecs[2];
            return n[0] * _lastViewForward[0] + n[1] * _lastViewForward[1] + n[2] * _lastViewForward[2];
        };
        const float dot1 = facing(bestIndex[0]);
        const float dot2 = facing(bestIndex[1]);
        const int32_t chosen = dot2 < dot1 && dot2 < 0 ? bestIndex[1] : bestIndex[0];
        const size_t s = size_t(model.firstSurface + chosen);
        if (s >= _map->surfaces.size())
            return 0;
        for (int k = 0; k < 4; ++k) {
            const auto p = corner(_map->surfaces[s], k);
            for (int c = 0; c < 3; ++c)
                vm.writeInt(a[1] + 12 * k + 4 * c, Qvm::fromFloat(p[size_t(c)]));
        }
        return 0;
    }
    case kSetClientForceAngle: // (time, angles): the yaw taken, as jk2mp.exe keeps it (0x401200)
        if (vm.valid(a[1], 12)) {
            _viewLimits.forceTime = a[0];
            _viewLimits.forceYaw = f3(1, 1);
        }
        return 0;
    case kSetClientTurnExtent: // (add, sub, time) (0x401230)
        _viewLimits.turnAdd = f(0);
        _viewLimits.turnSub = f(1);
        _viewLimits.turnTime = a[2];
        return 0;
    case kOpenUiMenu: // (menu): the menus' to open (the force powers...)
        _uiMenus.push_back(a[0]);
        return 0;
    case kTestPrintInt:
    case kTestPrintFloat:
        return 0;
    case kKeyIsDown:
    case kKeyGetKey:
        return 0;
    case kKeyGetCatcher:
        return _catcher;
    case kKeySetCatcher:
        _catcher = a[0];
        return 0;
    case kStopBackgroundTrack:
        _music = {};
        if (soundSystem)
            soundSystem->stopBackgroundTrack();
        return 0;
    case kRealTime: {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        const int32_t fields[9] = {local.tm_sec,  local.tm_min,  local.tm_hour, local.tm_mday, local.tm_mon,
                                   local.tm_year, local.tm_wday, local.tm_yday, local.tm_isdst};
        if (a[0] && vm.checkWrite(a[0], 36))
            for (int i = 0; i < 9; ++i)
                vm.writeInt(a[0] + 4 * i, fields[i]);
        return int32_t(now);
    }
    case kSnapVector:
        if (vm.checkWrite(a[0], 12))
            for (int i = 0; i < 3; ++i)
                vm.writeInt(a[0] + 4 * i, Qvm::fromFloat(std::nearbyint(Qvm::asFloat(vm.readInt(a[0] + 4 * i)))));
        return 0;
    case kCinPlayCinematic:
        return -1;
    case kCinSetExtents:
        return 0;
    case kGetEntityToken: { // (buffer, size) -> whether there was one: the level's entities, word by word
        const std::string token = _map ? nextEntityToken(_map->entities, _entityTokenPos) : std::string();
        vm.writeString(a[0], token, a[1]);
        return token.empty() ? 0 : 1;
    }
    case kInPvs: { // (p1, p2)
        if (!_map)
            return 1;
        if (!_world)
            _world = std::make_unique<CollisionWorld>(*_map);
        auto vec = [&](int32_t at) {
            return Vec3f{Qvm::asFloat(vm.readInt(at)), Qvm::asFloat(vm.readInt(at + 4)),
                         Qvm::asFloat(vm.readInt(at + 8))};
        };
        const int l1 = _world->leafAt(vec(a[0]));
        const int l2 = _world->leafAt(vec(a[1]));
        if (l1 < 0 || l2 < 0)
            return 0;
        return _map->clusterVisible(_map->leafs[size_t(l1)].cluster, _map->leafs[size_t(l2)].cluster) ? 1 : 0;
    }
    case kSpPrint:
        return 0;
    case kSpRegister:
    case kSpGetStringTextString: {
        if (!_stringsLoaded) {
            _strings.load(_files);
            _stringsLoaded = true;
        }
        if (number == kSpRegister) {
            const std::string name = fs::toLower(str(0, 256));
            for (const raven::StringPackage &p : _strings.packages())
                if (fs::toLower(p.reference) == name)
                    return 1;
            return 0;
        }
        const int language = std::clamp(_cvars.integer("sp_language"), 0, 2);
        const std::string *text = _strings.find(str(0, 256), raven::StringTable::Language(language));
        vm.writeString(a[1], text ? *text : std::string(), a[2]);
        return text ? 1 : 0;
    }
    case kSetSharedBuffer: // (address, size): where the engine and it trade an effect's or a trace's data
        _sharedBuffer = a[0];
        _sharedBufferSize = a[1];
        return 0;
    default:
        if (!_unhandled[number]++)
            _firstUnhandled[number] = {a[0], a[1], a[2], a[3], a[4], a[5]};
        return 0;
    }
}

int32_t CgameHost::jk2RoffCall(Qvm &vm, int number, const int32_t *a) {
    // The client game's own movers' ROFFs (docs/engines/idtech3/formats/roff.md),
    // in its order: clean, update, cache, play, purge. On the server's
    // clock, as the original's (svs.time, 0xb0d674, at 0x435b50): here the
    // latest snapshot's, which is it on the Play page's own server and
    // keeps moving on another's, where the original's stood still.
    switch (number - kRoffClean) {
    case 0:
        _roffs->clean(true);
        return 1;
    case 1:
        _roffs->update(true, _snapshotTime, roffSide());
        return 0;
    case 2:
        return _roffs->cache(vm.string(a[0], 64), true);
    case 3: { // (entity, roff, translate)
        raven::RoffSystem::Vec3 origin{};
        raven::RoffSystem::Vec3 angles{};
        roffSide().current(a[0], origin, angles);
        return _roffs->play(a[0], a[1], a[2] != 0, true, _snapshotTime, angles) ? 1 : 0;
    }
    default:
        return _roffs->purge(a[0], true, _snapshotTime, roffSide()) ? 1 : 0;
    }
}

raven::RoffSystem::Side CgameHost::roffSide() {
    // Asked of the client game, as the original does: where an entity is and
    // faces (written to the shared buffer here: Reluva's client game is a
    // QVM, where the original's engine passed a pointer of its own), and
    // its next state's trajectories' addresses.
    constexpr int32_t kGetOrigin = 17;
    constexpr int32_t kGetAngles = 18;
    constexpr int32_t kGetOriginTrajectory = 20;
    constexpr int32_t kGetAngleTrajectory = 21;
    raven::RoffSystem::Side side;
    side.current = [this](int32_t entity, raven::RoffSystem::Vec3 &origin, raven::RoffSystem::Vec3 &angles) {
        if (!_sharedBuffer || !_vm.valid(_sharedBuffer, 24))
            return false;
        int32_t result = 0;
        if (_vm.call(kGetOrigin, {entity, _sharedBuffer}, result) ||
            _vm.call(kGetAngles, {entity, _sharedBuffer + 12}, result))
            return false;
        for (int i = 0; i < 3; ++i) {
            origin[size_t(i)] = Qvm::asFloat(_vm.readInt(_sharedBuffer + 4 * i));
            angles[size_t(i)] = Qvm::asFloat(_vm.readInt(_sharedBuffer + 12 + 4 * i));
        }
        return true;
    };
    side.setTrajectories = [this](int32_t entity, int32_t type, int32_t time, const raven::RoffSystem::Vec3 &origin,
                                  const raven::RoffSystem::Vec3 &originDelta, const raven::RoffSystem::Vec3 &angles,
                                  const raven::RoffSystem::Vec3 &anglesDelta) {
        // trajectory_t: type, time, duration, base, delta.
        auto set = [&](int32_t trajectoryExport, const raven::RoffSystem::Vec3 &base,
                       const raven::RoffSystem::Vec3 &delta) {
            int32_t at = 0;
            if (_vm.call(trajectoryExport, {entity}, at) || !_vm.valid(at, 36))
                return;
            _vm.writeInt(at, type);
            _vm.writeInt(at + 4, time);
            for (int i = 0; i < 3; ++i) {
                _vm.writeInt(at + 12 + 4 * i, Qvm::fromFloat(base[size_t(i)]));
                _vm.writeInt(at + 24 + 4 * i, Qvm::fromFloat(delta[size_t(i)]));
            }
        };
        set(kGetOriginTrajectory, origin, originDelta);
        set(kGetAngleTrajectory, angles, anglesDelta);
    };
    // A frame's note: CG_ROFF_NOTETRACK_CALLBACK (22), (entity, text).
    side.note = [this](int32_t entity, const std::string &note) {
        constexpr int32_t kRoffNotetrack = 22;
        int32_t ignored = 0;
        if (Error err = _vm.callWithText(kRoffNotetrack, {entity, 0}, 1, note, ignored); err && print)
            print("ROFF note \"" + note + "\": " + err.message + "\n");
    };
    return side;
}

} // namespace reluva::idtech3::eliteforce
