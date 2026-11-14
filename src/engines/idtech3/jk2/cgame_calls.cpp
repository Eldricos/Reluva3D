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
    kFxAddSprite = 269, // 255 to 269: effects played and drawn
    kSpPrint = 270,
    kSpGetStringTextString = 271,
    kSpRegister = 272,
    kRoffClean = 273,
    kRoffPurgeEnt = 277,
    kG2ListSurfaces = 278,
    kG2HaveWeGhoulModels = 281,
    kG2GiveMeVectorFromMatrix = 282,
    kG2GetBolt = 283,
    kG2GetBoltNoRec = 284,
    kG2InitGhoul2Model = 285,
    kG2CleanModels = 286,
    kG2GetGlaName = 289,
    kG2HasGhoul2ModelOnIndex = 293,
    kG2AddBolt = 295,
    kG2SetNewOrigin = 299,
    kSetSharedBuffer = 300,
};

constexpr int32_t kBoltMatrixSize = 48; // mdxaBone_t: 3 rows of 4 floats

} // namespace

int32_t CgameHost::jk2SharedCall(int32_t n) {
    // Jedi Outcast's number -> Elite Force's, where the call is the same
    // (a capsule taken as a box, an additive or "real" looping sound or
    // light as the plain one).
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
    if (number >= kRegisterFont && number <= kReadCharFromString)
        return _fonts.call(vm, jk2::FontSet::Call(number - kRegisterFont), a, _time,
                           [this](float x, float y, float w, float h, float s1, float t1, float s2, float t2,
                                  int32_t shader, const std::array<float, 4> &color) {
                               _pictures.push_back({x, y, w, h, s1, t1, s2, t2, shader, color});
                           });
    if (number >= kPcAddGlobalDefine && number <= kPcRemoveAllGlobalDefines)
        return _scripts.call(vm, botlib::ScriptCalls::Call(number - kPcAddGlobalDefine), a);
    if (number > kFxRegisterEffect && number <= kFxAddSprite) {
        // Effects played: not drawn yet, counted.
        ++_unhandled[number];
        return 0;
    }
    if (number >= kRoffClean && number <= kRoffPurgeEnt) {
        ++_unhandled[number];
        return 0;
    }
    switch (number) {
    case kRemoveCommand: {
        const std::string name = str(0, 64);
        _commands.erase(std::remove(_commands.begin(), _commands.end(), name), _commands.end());
        return 0;
    }
    case kMuteSound:
    case kStopLoopingSound:
        return 0;
    case kCeil:
        return Qvm::fromFloat(std::ceil(f(0)));
    case kAddPolysToScene: // (shader, vertices each, vertices, polygons)
        for (int32_t i = 0; i < a[3]; ++i)
            _scene.addPoly(vm, a[0], a[1], a[2] + i * a[1] * 24);
        return 0;
    case kLightForPoint: // (point, ambient, directed, direction) -> whether the level has light there
        return 0;
    case kDrawRotatePic:
    case kDrawRotatePic2: { // (x, y, w, h, s1, t1, s2, t2, degrees, shader)
        Picture p{f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[9], _color};
        p.angle = f(8);
        _pictures.push_back(p);
        return 0;
    }
    case kGetLightStyle: // (style, rgba out)
        if (vm.checkWrite(a[1], 4))
            vm.writeInt(a[1], -1);
        return 0;
    case kRemapShader:
    case kSetLightStyle:
    case kGetBmodelVerts:
    case kFxAddLine:
    case kSetClientForceAngle:
    case kSetClientTurnExtent:
        ++_unhandled[number];
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
    case kFxRegisterEffect:
        return _effects.add(str(0, 256));
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
    // Ghoul2, on the client: models kept by name, bolts by number; not
    // posed or drawn yet.
    case kG2HaveWeGhoulModels:
        return a[0] != 0 && _g2Instances.count(a[0]) ? 1 : 0;
    case kG2InitGhoul2Model: { // (ghoul2 *, file, modelIndex, customSkin, customShader, flags, lodBias)
        if (!vm.checkWrite(a[0], 4))
            return -1;
        int32_t handle = vm.readInt(a[0]);
        if (!handle || !_g2Instances.count(handle)) {
            handle = _nextG2Instance++;
            vm.writeInt(a[0], handle);
        }
        std::vector<std::string> &models = _g2Instances[handle];
        models.push_back(str(1, 256));
        return int32_t(models.size()) - 1;
    }
    case kG2HasGhoul2ModelOnIndex: {
        if (!vm.valid(a[0], 4))
            return 0;
        const auto it = _g2Instances.find(vm.readInt(a[0]));
        return it != _g2Instances.end() && a[1] >= 0 && size_t(a[1]) < it->second.size() ? 1 : 0;
    }
    case kG2CleanModels:
        if (vm.checkWrite(a[0], 4)) {
            _g2Instances.erase(vm.readInt(a[0]));
            vm.writeInt(a[0], 0);
        }
        return 0;
    case kG2AddBolt:
        return _nextG2Bolt++;
    case kG2GetBolt:
    case kG2GetBoltNoRec: { // (ghoul2, modelIndex, bolt, matrix out, angles, position, time, modelList, scale)
        if (!vm.checkWrite(a[3], kBoltMatrixSize))
            return 0;
        Vec3f at{};
        if (a[5] && vm.valid(a[5], 12))
            for (int i = 0; i < 3; ++i)
                at[size_t(i)] = Qvm::asFloat(vm.readInt(a[5] + 4 * i));
        const float m[12] = {1, 0, 0, at[0], 0, 1, 0, at[1], 0, 0, 1, at[2]};
        for (int i = 0; i < 12; ++i)
            vm.writeInt(a[3] + 4 * i, Qvm::fromFloat(m[i]));
        return 1;
    }
    case kG2GiveMeVectorFromMatrix: { // (matrix, flags, vec out): an axis (0-2, 3-5 negated) or the origin
        if (!vm.valid(a[0], kBoltMatrixSize) || !vm.checkWrite(a[2], 12))
            return 0;
        auto m = [&](int r, int c) { return Qvm::asFloat(vm.readInt(a[0] + 4 * (4 * r + c))); };
        float v[3];
        const int which = a[1];
        for (int r = 0; r < 3; ++r) {
            if (which == 6) // the origin
                v[r] = m(r, 3);
            else
                v[r] = (which >= 3 ? -1.0f : 1.0f) * m(r, which % 3);
        }
        for (int i = 0; i < 3; ++i)
            vm.writeInt(a[2] + 4 * i, Qvm::fromFloat(v[i]));
        return 0;
    }
    case kG2GetGlaName:
        vm.writeString(a[2], "", 64);
        return 0;
    case kSetSharedBuffer: // (address, size): where the engine and it trade an effect's or a trace's data
        _sharedBuffer = a[0];
        _sharedBufferSize = a[1];
        return 0;
    default:
        if (number >= kG2ListSurfaces && number <= kG2SetNewOrigin)
            return 1; // animation, angles, surfaces: taken
        if (!_unhandled[number]++)
            _firstUnhandled[number] = {a[0], a[1], a[2], a[3], a[4], a[5]};
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
