// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/cgame_host.h"

#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/formats/shader.h"

#include <array>
#include <cstdlib>
#include "engines/idtech3/collision.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force cgame.qvm system calls (Quake III's client game order; the
// seen ones checked against the game's own calls, see
// docs/engines/idtech3/games/stvef/systems/syscalls.md).
enum CgameCall : int {
    kPrint = 0,
    kError = 1,
    kMilliseconds = 2,
    kCvarRegister = 3,
    kCvarUpdate = 4,
    kCvarSet = 5,
    kCvarVariableStringBuffer = 6,
    kArgc = 7,
    kArgv = 8,
    kArgs = 9,
    kFsFOpenFile = 10,
    kFsRead = 11,
    kFsWrite = 12,
    kFsFCloseFile = 13,
    kSendConsoleCommand = 14,
    kAddCommand = 15,
    kSendClientCommand = 16,
    kUpdateScreen = 17,
    kCmLoadMap = 18,
    kCmNumInlineModels = 19,
    kCmInlineModel = 20,
    kCmTempBoxModel = 22,
    kCmPointContents = 23,
    kCmTransformedPointContents = 24,
    kCmBoxTrace = 25,
    kCmTransformedBoxTrace = 26,
    kCmMarkFragments = 27,
    kSStartSound = 28,
    kSStartLocalSound = 29,
    kSClearLoopingSounds = 30,
    kSAddLoopingSound = 31,
    kSUpdateEntityPosition = 32,
    kSRespatialize = 33,
    kSRegisterSound = 34,
    kSStartBackgroundTrack = 35,
    kRLoadWorldMap = 36,
    kRRegisterModel = 37,
    kRRegisterSkin = 38,
    kRRegisterShader = 39,
    kRClearScene = 40,
    kRAddRefEntityToScene = 41,
    kRAddPolyToScene = 42,
    kRAddLightToScene = 43,
    kRRenderScene = 44,
    kRSetColor = 45,
    kRDrawStretchPic = 46,
    kRModelBounds = 47,
    kRLerpTag = 48,
    kGetGlconfig = 49,
    kGetGameState = 50,
    kGetCurrentSnapshotNumber = 51,
    kGetSnapshot = 52,
    kGetServerCommand = 53,
    kGetCurrentCmdNumber = 54,
    kGetUserCmd = 55,
    kSetUserCmdValue = 56,
    kRRegisterShaderNoMip = 57,
    kRRegisterShader3D = 59, // (name) -> handle, 0 if there's no such picture
    kMemoryRemaining = 58,
    // ("model", text): made once, for the local player only, with a name
    // the client game put together in a buffer of its own; taken as
    // setting that cvar, as it would the model the player now has.
    kSetLocalCvar = 60,
    kFloor = 107, // (x) -> x rounded down, a float; Quake III's client game number too
};

// gamestate_t, as Elite Force's client game lays it out: the same size as
// Quake III's (20100 bytes: the gap between the game state and the next
// field the client game fills in), so Quake III's layout: an offset into
// the text for each of the 1024 configstrings, the text, how much is used.
constexpr int32_t kMaxConfigstrings = 1024;
constexpr int32_t kGameStateChars = 16000;
constexpr int32_t kGameStateSize = 4 * kMaxConfigstrings + kGameStateChars + 4;

// glconfig_t, the same as the menus' (see ui_host.cpp).
constexpr int32_t kGlconfigWords = 5124;
constexpr int32_t kGlconfigSize = 5192;

// snapshot_t, as Elite Force's client game lays it out (the second
// snapshot buffer is 52740 bytes after the first; the fields it reads
// through its snapshot pointers match): Quake III's, with Elite Force's
// player and entity states.
// After the player state (1.0's 460 bytes, patch 1.2's 468) the offsets
// follow from its size: 504, 508, 52732, 52736 and 52740 bytes with 1.0's.
constexpr int32_t kSnapServerTime = 8;
constexpr int32_t kSnapPlayerState = 44;
constexpr int32_t kSnapAreaMask = 12; // 32 bytes: a bit set for each area hidden behind a closed door
constexpr int32_t kMaxSnapshotEntities = 256;
struct SnapshotLayout {
    int32_t numEntities;
    int32_t entities;
    int32_t numServerCommands;
    int32_t serverCommandSequence;
    int32_t size;
};
SnapshotLayout snapshotLayout(int32_t playerStateSize) {
    const int32_t numEntities = kSnapPlayerState + playerStateSize;
    const int32_t entities = numEntities + 4;
    const int32_t numServerCommands = entities + kMaxSnapshotEntities * GameHost::kEntityStateSize;
    return {numEntities, entities, numServerCommands, numServerCommands + 4, numServerCommands + 8};
}

constexpr int32_t kTempBoxModel = 0x10000; // CM_TempBoxModel's handle (the original keeps one at a time too)

// Quake III's trace_t (56 bytes), as the game logic's traces.
void writeTrace(Qvm &vm, int32_t address, const TraceResult &t, int32_t entityNum) {
    if (!vm.checkWrite(address, 56))
        return;
    vm.writeInt(address + 0, t.allSolid);
    vm.writeInt(address + 4, t.startSolid);
    vm.writeInt(address + 8, Qvm::fromFloat(t.fraction));
    for (int i = 0; i < 3; ++i) {
        vm.writeInt(address + 12 + 4 * i, Qvm::fromFloat(t.endPos[size_t(i)]));
        vm.writeInt(address + 24 + 4 * i, Qvm::fromFloat(t.normal[size_t(i)]));
    }
    vm.writeInt(address + 36, Qvm::fromFloat(t.dist));
    int type = 3;
    int signs = 0;
    for (int i = 0; i < 3; ++i) {
        if (t.normal[size_t(i)] == 1 || t.normal[size_t(i)] == -1)
            type = i;
        if (t.normal[size_t(i)] < 0)
            signs |= 1 << i;
    }
    vm.memory()[size_t(address + 40)] = uint8_t(type);
    vm.memory()[size_t(address + 41)] = uint8_t(signs);
    vm.memory()[size_t(address + 42)] = 0;
    vm.memory()[size_t(address + 43)] = 0;
    vm.writeInt(address + 44, t.surfaceFlags);
    vm.writeInt(address + 48, t.contents);
    vm.writeInt(address + 52, entityNum);
}

// A command line split into words as Quake III's Cmd_TokenizeString does:
// at white space, a quoted string as one word, // comments ending it.
Vec3f readVec3(const Qvm &vm, int32_t address) {
    return {Qvm::asFloat(vm.readInt(address)), Qvm::asFloat(vm.readInt(address + 4)),
            Qvm::asFloat(vm.readInt(address + 8))};
}

std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (true) {
        while (i < line.size() && static_cast<unsigned char>(line[i]) <= ' ')
            ++i;
        if (i >= line.size() || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
            return words;
        std::string word;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"')
                word += line[i++];
            if (i < line.size())
                ++i;
        } else {
            while (i < line.size() && static_cast<unsigned char>(line[i]) > ' ')
                word += line[i++];
        }
        words.push_back(std::move(word));
    }
}

} // namespace

void CgameHost::newSnapshot(GameHost::Snapshot snapshot) {
    // Numbered as the original's client numbers them, by the message they
    // came in: Init's serverMessageNum is the game state's, and the client
    // game takes only snapshots numbered after it.
    if (snapshot.messageNum > _snapshotNumber)
        _snapshotNumber = snapshot.messageNum;
    else
        ++_snapshotNumber;
    _snapshotTime = snapshot.serverTime;
    _snapshots[_snapshotNumber] = std::move(snapshot);
    while (_snapshots.size() > 32)
        _snapshots.erase(_snapshots.begin());
}

CgameHost::CgameHost(const SearchSet &files, CvarStore &cvars) : _files(files), _cvars(cvars), _vmFiles(files) {
    _vm.setSystemCall([this](Qvm &vm, int number, const int32_t *args) { return systemCall(vm, number, args); });
}

Error CgameHost::load() {
    std::vector<uint8_t> data;
    if (Error err = _files.read("vm/cgame.qvm", data))
        return Error::fail("vm/cgame.qvm: " + err.message);
    if (Error err = _vm.load(data))
        return Error::fail("vm/cgame.qvm: " + err.message);
    _layout = detectLayout(_vm);
    return Error::success();
}

Error CgameHost::call(int32_t command, const std::vector<int32_t> &args, int32_t &result) {
    return _vm.call(command, args, result);
}

Error CgameHost::consoleCommand(const std::string &text, bool &handled) {
    _args = tokenize(text);
    int32_t result = 0;
    Error err = call(kConsoleCommand, {}, result);
    handled = !err && result;
    return err;
}

Error CgameHost::init(int32_t clientNum, int32_t serverMessageNum, int32_t serverCommandSequence) {
    int32_t ignored = 0;
    return call(kInit, {serverMessageNum, serverCommandSequence, clientNum}, ignored);
}

void CgameHost::writeGameState(int32_t address) {
    if (!_vm.checkWrite(address, kGameStateSize))
        return;
    std::memset(&_vm.memory()[size_t(address)], 0, size_t(kGameStateSize));
    // Offset 0 is an empty string, for configstrings not set.
    const int32_t data = address + 4 * kMaxConfigstrings;
    int32_t used = 1;
    for (const auto &[index, text] : _configstrings) {
        if (index < 0 || index >= kMaxConfigstrings || text.empty())
            continue;
        if (used + int32_t(text.size()) + 1 > kGameStateChars) {
            _vm.abort("GetGameState: the configstrings don't fit in the game state");
            return;
        }
        _vm.writeInt(address + 4 * index, used);
        _vm.writeString(data + used, text, int32_t(text.size()) + 1);
        used += int32_t(text.size()) + 1;
    }
    _vm.writeInt(data + kGameStateChars, used);
}

void CgameHost::writeGlconfig(int32_t address) {
    if (!_vm.checkWrite(address, kGlconfigSize))
        return;
    std::memset(&_vm.memory()[size_t(address)], 0, size_t(kGlconfigSize));
    _vm.writeString(address, "Reluva", 1024);
    _vm.writeString(address + 1024, "Reluva", 1024);
    _vm.writeString(address + 2048, "1.0", 1024);
    // Quake III's words from 5124: maxTextureSize, textureUnits, colour,
    // depth, stencil bits ... vidWidth (+40), vidHeight (+44), aspect (+48).
    const int32_t w = address + kGlconfigWords;
    _vm.writeInt(w + 0, 2048);
    _vm.writeInt(w + 4, 2);
    _vm.writeInt(w + 8, 32);
    _vm.writeInt(w + 12, 24);
    _vm.writeInt(w + 16, 8);
    _vm.writeInt(w + 36, 1);
    _vm.writeInt(w + 40, 640);
    _vm.writeInt(w + 44, 480);
    _vm.writeInt(w + 48, Qvm::fromFloat(640.0f / 480.0f));
    _vm.writeInt(w + 52, 60);
}

int32_t CgameHost::systemCall(Qvm &vm, int number, const int32_t *a) {
    if (trace)
        trace(number, a);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    int32_t result = 0;
    if (number == 100 && a[1] == 0) // memset(address, 0, size): kept, to learn the sizes of what it clears
        _cleared[a[0]] = a[2];
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case kPrint: {
        const std::string text = str(0, 4096);
        // Kept (the last 4 KB) as well as handed on.
        _log += text;
        if (_log.size() > 8192)
            _log.erase(0, _log.size() - 4096);
        if (print)
            print(text);
        return 0;
    }
    case kError:
        vm.abort("cgame.qvm: " + str(0));
        return 0;
    case kMilliseconds:
        return _time;
    case kCvarRegister: {
        const CvarStore::Cvar &cv = _cvars.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            _cvars.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case kCvarUpdate:
        _cvars.updateVmCvar(vm, a[0]);
        return 0;
    case kCvarSet:
        _cvars.set(str(0, 256), str(1, 256), true);
        return 0; // forced, as the original's Cvar_Set
    case kSetLocalCvar:
        // Holomatch's engine sets the cvar forced (read-only ones too),
        // without counting it as a change of the player's settings to send.
        // Reluva sends them only on connecting, so a forced set is enough.
        _cvars.set(str(0, 256), str(1, 256), true);
        return 0;
    case kCvarVariableStringBuffer:
        vm.writeString(a[1], _cvars.string(str(0, 256)), a[2]);
        return 0;
    case kArgc:
        return int32_t(_args.size());
    case kArgv:
        vm.writeString(a[1], a[0] >= 0 && size_t(a[0]) < _args.size() ? _args[size_t(a[0])] : "", a[2]);
        return 0;
    case kArgs: {
        std::string rest;
        for (size_t i = 1; i < _args.size(); ++i)
            rest += (i > 1 ? " " : "") + _args[i];
        vm.writeString(a[0], rest, a[1]);
        return 0;
    }
    case kFsFOpenFile:
        return _vmFiles.open(vm, a[0], a[1], a[2]);
    case kFsRead:
        return _vmFiles.read(vm, a[0], a[1], a[2]);
    case kFsFCloseFile:
        _vmFiles.close(a[0]);
        return 0;
    case kAddCommand:
        _commands.push_back(str(0, 64));
        return 0;
    case kSendClientCommand:
        _clientCommands.push_back(str(0, 1024));
        return 0;
    case kSendConsoleCommand:
        _consoleCommands.push_back(str(0, 1024));
        return 0;
    case kUpdateScreen: // the loading screen
        if (updateScreen)
            updateScreen();
        return 0;
    case kCmLoadMap: {
        _collisionMap = str(0, 256);
        std::vector<uint8_t> data;
        auto bsp = std::make_shared<Bsp>();
        if (_files.read(_collisionMap, data) || parseBsp(data, *bsp)) {
            vm.abort("CM_LoadMap: can't load " + _collisionMap);
            return 0;
        }
        _map = std::move(bsp);
        _world.reset();
        _marks.reset();
        return 0;
    }
    case kCmNumInlineModels:
        return _map ? int32_t(_map->models.size()) : 0;
    case kSRegisterSound:
        return _sounds.add(str(0, 256));
    case kSStartBackgroundTrack: // (intro, loop)
        _music = {str(0, 256), str(1, 256)};
        if (soundSystem)
            soundSystem->startBackgroundTrack(_music.first, _music.second);
        return 0;
    case kRLoadWorldMap:
        _worldMap = str(0, 256);
        return 0;
    case kRRegisterModel:
        return _models.add(str(0, 256));
    case kRRegisterSkin:
        return _skins.add(str(0, 256));
    case kRRegisterShader:
    case kRRegisterShaderNoMip:
        return _shaders.add(str(0, 256));
    case kRRegisterShader3D: {
        // The CTF flags' team pictures (models/flags/<team>_red): 0 for a
        // team without one, and the client game takes the default.
        const std::string name = str(0, 256);
        return findImage(_files, name).empty() ? 0 : _shaders.add(name);
    }
    case kRClearScene:
        _scene.clear();
        return 0;
    case kRSetColor: // (rgba or 0 for white)
        if (!a[0])
            _color = {1, 1, 1, 1};
        else
            for (int i = 0; i < 4; ++i)
                _color[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
        return 0;
    case kRDrawStretchPic: { // (x, y, w, h, s1, t1, s2, t2, shader)
        auto f = [&](int i) { return Qvm::asFloat(a[i]); };
        _pictures.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[8], _color});
        return 0;
    }
    case kRModelBounds: { // (model, mins[3], maxs[3])
        std::array<float, 3> mins{};
        std::array<float, 3> maxs{};
        const std::string &name = _models.name(a[0]);
        if (!name.empty() && name[0] == '*') { // one of the map's brush models
            const int n = std::atoi(name.c_str() + 1);
            if (_map && n > 0 && size_t(n) < _map->models.size()) {
                mins = _map->models[size_t(n)].mins;
                maxs = _map->models[size_t(n)].maxs;
            }
        } else if (!name.empty()) { // an MD3's first frame, as the original renderer gives
            std::vector<uint8_t> data;
            Md3Model md3;
            if (!_files.read(name, data) && !parseModel(data, md3) && !md3.frames.empty()) {
                mins = md3.frames[0].mins;
                maxs = md3.frames[0].maxs;
            }
        }
        for (int i = 0; i < 3; ++i) {
            vm.writeInt(a[1] + 4 * i, Qvm::fromFloat(mins[size_t(i)]));
            vm.writeInt(a[2] + 4 * i, Qvm::fromFloat(maxs[size_t(i)]));
        }
        return 0;
    }
    case kSClearLoopingSounds:
        if (soundSystem)
            soundSystem->clearLoopingSounds();
        return 0;
    case kGetCurrentSnapshotNumber: // (&number, &serverTime)
        vm.writeInt(a[0], _snapshotNumber);
        vm.writeInt(a[1], _snapshotTime);
        return 0;
    case kGetSnapshot: { // (number, snapshot_t*) -> whether it's still there
        _snapshotRequests.emplace_back(a[0], a[1]);
        const auto it = _snapshots.find(a[0]);
        const SnapshotLayout l = snapshotLayout(_layout.playerStateSize);
        if (it == _snapshots.end() || !vm.checkWrite(a[1], l.size))
            return 0;
        const GameHost::Snapshot &s = it->second;
        std::memset(&vm.memory()[size_t(a[1])], 0, size_t(l.numEntities)); // flags, ping, area mask (all visible)
        vm.writeInt(a[1], s.snapFlags);
        vm.writeInt(a[1] + kSnapServerTime, s.serverTime);
        std::memcpy(&vm.memory()[size_t(a[1] + kSnapAreaMask)], s.areaMask.data(), s.areaMask.size());
        std::memcpy(&vm.memory()[size_t(a[1] + kSnapPlayerState)], s.playerState.data(),
                    std::min(s.playerState.size(), size_t(_layout.playerStateSize)));
        const auto count = int32_t(std::min(s.entities.size(), size_t(kMaxSnapshotEntities)));
        vm.writeInt(a[1] + l.numEntities, count);
        for (int32_t i = 0; i < count; ++i)
            std::memcpy(&vm.memory()[size_t(a[1] + l.entities + i * GameHost::kEntityStateSize)],
                        s.entities[size_t(i)].data(),
                        std::min(s.entities[size_t(i)].size(), size_t(GameHost::kEntityStateSize)));
        vm.writeInt(a[1] + l.numServerCommands, 0);
        vm.writeInt(a[1] + l.serverCommandSequence, s.serverCommandSequence);
        return 1;
    }
    case kCmInlineModel:
        return a[0];      // the map's model n is clip model n
    case kCmTempBoxModel: // (mins, maxs): a box to trace against, until the next one
        for (int i = 0; i < 3; ++i) {
            _tempBoxMins[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
            _tempBoxMaxs[size_t(i)] = Qvm::asFloat(vm.readInt(a[1] + 4 * i));
        }
        return kTempBoxModel;
    case kCmPointContents:              // (point, model)
    case kCmTransformedPointContents: { // (point, model, origin, angles)
        auto vec = [&](int32_t addr) {
            Vec3f v{};
            if (addr)
                for (int i = 0; i < 3; ++i)
                    v[size_t(i)] = Qvm::asFloat(vm.readInt(addr + 4 * i));
            return v;
        };
        const bool transformed = number == kCmTransformedPointContents;
        const Vec3f point = vec(a[0]);
        const Vec3f origin = transformed ? vec(a[2]) : Vec3f{};
        const Vec3f angles = transformed ? vec(a[3]) : Vec3f{};
        if (a[1] == kTempBoxModel) {
            for (int i = 0; i < 3; ++i) {
                const float p = point[size_t(i)] - origin[size_t(i)];
                if (p < _tempBoxMins[size_t(i)] || p > _tempBoxMaxs[size_t(i)])
                    return 0;
            }
            return contents::kBody;
        }
        if (!_map)
            return 0;
        if (!_world)
            _world = std::make_unique<CollisionWorld>(*_map);
        return _world->transformedPointContents(point, a[1], origin, angles);
    }
    case kCmBoxTrace:              // (results, start, end, mins, maxs, model, brushmask)
    case kCmTransformedBoxTrace: { // ... origin, angles
        auto vec = [&](int32_t addr) {
            Vec3f v{};
            if (addr)
                for (int i = 0; i < 3; ++i)
                    v[size_t(i)] = Qvm::asFloat(vm.readInt(addr + 4 * i));
            return v;
        };
        const bool transformed = number == kCmTransformedBoxTrace;
        const Vec3f start = vec(a[1]);
        const Vec3f end = vec(a[2]);
        const Vec3f mins = vec(a[3]);
        const Vec3f maxs = vec(a[4]);
        const Vec3f origin = transformed ? vec(a[7]) : Vec3f{};
        const Vec3f angles = transformed ? vec(a[8]) : Vec3f{};
        TraceResult t;
        if (a[5] == kTempBoxModel) {
            // A box isn't turned, as the original's.
            Vec3f s = start;
            Vec3f e = end;
            for (int i = 0; i < 3; ++i) {
                s[size_t(i)] -= origin[size_t(i)];
                e[size_t(i)] -= origin[size_t(i)];
            }
            t = CollisionWorld::traceBox(s, e, mins, maxs, _tempBoxMins, _tempBoxMaxs, contents::kBody);
            for (int i = 0; i < 3; ++i)
                t.endPos[size_t(i)] += origin[size_t(i)];
        } else if (_map) {
            if (!_world)
                _world = std::make_unique<CollisionWorld>(*_map);
            t = _world->transformedTrace(start, end, mins, maxs, a[5], a[6], origin, angles);
        }
        writeTrace(vm, a[0], t, t.fraction < 1 ? 1022 : 1023);
        return 0;
    }
    case kCmMarkFragments: {
        // (numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer) -> fragments:
        // the pieces of the level under a mark, each (firstPoint, numPoints) into the points written.
        if (!_map || a[0] < 3 || a[0] > 64 || !vm.valid(a[1], a[0] * 12) || !vm.valid(a[2], 12))
            return 0;
        if (!_marks)
            _marks = std::make_unique<MarkSurfaces>(*_map, subdivisions);
        auto vec = [&](int32_t at) {
            return Vec3f{Qvm::asFloat(vm.readInt(at)), Qvm::asFloat(vm.readInt(at + 4)),
                         Qvm::asFloat(vm.readInt(at + 8))};
        };
        std::vector<Vec3f> polygon;
        for (int32_t i = 0; i < a[0]; ++i)
            polygon.push_back(vec(a[1] + 12 * i));
        std::vector<Vec3f> points;
        std::vector<MarkSurfaces::Fragment> fragments;
        _marks->mark(polygon, vec(a[2]), size_t(std::max(a[3], 0)), size_t(std::max(a[5], 0)), points, fragments);
        if (!vm.checkWrite(a[4], int32_t(points.size()) * 12) || !vm.checkWrite(a[6], int32_t(fragments.size()) * 8))
            return 0;
        for (size_t i = 0; i < points.size(); ++i)
            for (int c = 0; c < 3; ++c)
                vm.writeInt(a[4] + int32_t(i) * 12 + 4 * c, Qvm::fromFloat(points[i][size_t(c)]));
        for (size_t i = 0; i < fragments.size(); ++i) {
            vm.writeInt(a[6] + int32_t(i) * 8, fragments[i].firstPoint);
            vm.writeInt(a[6] + int32_t(i) * 8 + 4, fragments[i].numPoints);
        }
        return int32_t(fragments.size());
    }
    // Sounds, by their registered handles (SRegisterSound), as Quake III's.
    case kSStartSound: // (origin or 0 to follow the entity, entityNum, channel, sfx)
        if (soundSystem) {
            std::optional<Vec3f> origin;
            if (a[0] && vm.valid(a[0], 12))
                origin = readVec3(vm, a[0]);
            soundSystem->startSound(origin, a[1], a[2], _sounds.name(a[3]));
        }
        return 0;
    case kSStartLocalSound: // (sfx, channel)
        if (soundSystem)
            soundSystem->startLocalSound(_sounds.name(a[0]), a[1]);
        return 0;
    case kSAddLoopingSound: // (entityNum, origin, velocity, sfx): seen as (0, ..., ..., 109) while firing
        if (soundSystem && vm.valid(a[1], 12))
            soundSystem->addLoopingSound(a[0], readVec3(vm, a[1]), _sounds.name(a[3]));
        return 0;
    case kSUpdateEntityPosition: // (entityNum, origin)
        if (soundSystem && vm.valid(a[1], 12))
            soundSystem->updateEntityPosition(a[0], readVec3(vm, a[1]));
        return 0;
    case kSRespatialize: // (entityNum, origin, axes, inWater)
        if (soundSystem && vm.valid(a[1], 12) && vm.valid(a[2], 36))
            soundSystem->respatialize(a[0], readVec3(vm, a[1]),
                                      {readVec3(vm, a[2]), readVec3(vm, a[2] + 12), readVec3(vm, a[2] + 24)});
        return 0;
    case kRAddRefEntityToScene: {
        const auto it = _cleared.find(a[0]);
        _scene.addEntity(vm, a[0], it != _cleared.end() ? it->second : 0);
        return 0;
    }
    case kRAddPolyToScene:
        _scene.addPoly(vm, a[0], a[1], a[2]);
        return 0;           // (shader, numVerts, verts)
    case kRAddLightToScene: // (origin, intensity, r, g, b)
        _scene.addLight(vm, a[0], Qvm::asFloat(a[1]), Qvm::asFloat(a[2]), Qvm::asFloat(a[3]), Qvm::asFloat(a[4]));
        return 0;
    case kRRenderScene:
        _scene.render(vm, a[0], _pictures.size());
        return 0;
    case kRLerpTag: { // (orientation_t*, model, startFrame, endFrame, frac, tagName) -> found
        auto [it, added] = _md3s.try_emplace(a[1]);
        if (added) {
            std::vector<uint8_t> data;
            auto md3 = std::make_shared<Md3Model>();
            const std::string &name = _models.name(a[1]);
            if (!name.empty() && name[0] != '*' && !_files.read(name, data) && !parseModel(data, *md3))
                it->second = std::move(md3);
        }
        return writeLerpTag(vm, a[0], it->second.get(), a[2], a[3], Qvm::asFloat(a[4]), str(5, 64));
    }
    case kGetCurrentCmdNumber:
        return _userCommandNumber;
    case kGetUserCmd: { // (number, usercmd_t*) -> whether it's still kept
        const auto it = _userCommands.find(a[0]);
        if (it == _userCommands.end() || !vm.checkWrite(a[1], 24))
            return 0;
        GameHost::writeUserCommand(vm, a[1], it->second);
        return 1;
    }
    case kSetUserCmdValue:
        _weaponSelect = a[0];
        return 0;             // (weapon, sensitivity scale): what the next commands carry
    case kGetServerCommand: { // (sequence) -> whether it's there; it becomes Argc/Argv
        if (a[0] < 1 || size_t(a[0]) > _serverCommands.size())
            return 0;
        // As the original's client, the engine acts on some first: a
        // configstring that changed (cs index "text", or in pieces, bcs0,
        // bcs1... bcs2, which the client game doesn't see until the last,
        // as one cs) is set before the client game hears of it.
        std::string text = _serverCommands[size_t(a[0] - 1)];
        _args = tokenize(text);
        const std::string verb = _args.empty() ? std::string() : _args[0];
        if ((verb == "bcs0" || verb == "bcs1" || verb == "bcs2") && _args.size() >= 3) {
            if (verb == "bcs0")
                _bigConfigstring.clear();
            _bigConfigstring += _args[2];
            if (verb != "bcs2")
                return 0;
            text = "cs " + _args[1] + " \"" + _bigConfigstring + "\"";
            _args = {"cs", _args[1], _bigConfigstring};
        }
        if (!_args.empty() && _args[0] == "cs" && _args.size() >= 2) {
            const int32_t index = std::atoi(_args[1].c_str());
            if (index >= 0 && index < kMaxConfigstrings)
                _configstrings[index] = _args.size() >= 3 ? _args[2] : std::string();
        }
        return 1;
    }
    case kFloor:
        return Qvm::fromFloat(std::floor(Qvm::asFloat(a[0])));
    case kMemoryRemaining:
        return 64 << 20; // plenty: the client game loads every player model up front
    case kGetGlconfig:
        writeGlconfig(a[0]);
        return 0;
    case kGetGameState:
        writeGameState(a[0]);
        return 0;
    default:
        if (!_unhandled[number]++)
            _firstUnhandled[number] = {a[0], a[1], a[2], a[3], a[4], a[5]};
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
