// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/cgame_host.h"

#include "engines/idtech3/md3.h"
#include "engines/idtech3/shader.h"

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
// docs/engines/idtech3/formats/eliteforce-syscalls.md).
enum CgameCall : int {
    Print = 0,
    Error_ = 1,
    Milliseconds = 2,
    CvarRegister = 3,
    CvarUpdate = 4,
    CvarSet = 5,
    CvarVariableStringBuffer = 6,
    Argc = 7,
    Argv = 8,
    Args = 9,
    FsFOpenFile = 10,
    FsRead = 11,
    FsWrite = 12,
    FsFCloseFile = 13,
    SendConsoleCommand = 14,
    AddCommand = 15,
    SendClientCommand = 16,
    UpdateScreen = 17,
    CmLoadMap = 18,
    CmNumInlineModels = 19,
    CmInlineModel = 20,
    CmTempBoxModel = 22,
    CmPointContents = 23,
    CmTransformedPointContents = 24,
    CmBoxTrace = 25,
    CmTransformedBoxTrace = 26,
    CmMarkFragments = 27,
    SStartSound = 28,
    SStartLocalSound = 29,
    SClearLoopingSounds = 30,
    SAddLoopingSound = 31,
    SUpdateEntityPosition = 32,
    SRespatialize = 33,
    SRegisterSound = 34,
    SStartBackgroundTrack = 35,
    RLoadWorldMap = 36,
    RRegisterModel = 37,
    RRegisterSkin = 38,
    RRegisterShader = 39,
    RClearScene = 40,
    RAddRefEntityToScene = 41,
    RAddPolyToScene = 42,
    RAddLightToScene = 43,
    RRenderScene = 44,
    RSetColor = 45,
    RDrawStretchPic = 46,
    RModelBounds = 47,
    RLerpTag = 48,
    GetGlconfig = 49,
    GetGameState = 50,
    GetCurrentSnapshotNumber = 51,
    GetSnapshot = 52,
    GetServerCommand = 53,
    GetCurrentCmdNumber = 54,
    GetUserCmd = 55,
    SetUserCmdValue = 56,
    RRegisterShaderNoMip = 57,
    RRegisterShader3D = 59, // (name) -> handle, 0 if there's no such picture
    MemoryRemaining = 58,
    // ("model", text): made once, for the local player only, with a name
    // the client game put together in a buffer of its own; taken as
    // setting that cvar, as it would the model the player now has.
    SetLocalCvar = 60,
    Floor = 107, // (x) -> x rounded down, a float; Quake III's client game number too
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
constexpr int32_t kSnapServerTime = 8, kSnapPlayerState = 44;
constexpr int32_t kSnapAreaMask = 12; // 32 bytes: a bit set for each area hidden behind a closed door
constexpr int32_t kMaxSnapshotEntities = 256;
struct SnapshotLayout {
    int32_t numEntities, entities, numServerCommands, serverCommandSequence, size;
};
SnapshotLayout snapshotLayout(int32_t playerStateSize) {
    const int32_t numEntities = kSnapPlayerState + playerStateSize, entities = numEntities + 4;
    const int32_t numServerCommands = entities + kMaxSnapshotEntities * GameHost::kEntityStateSize;
    return {numEntities, entities, numServerCommands, numServerCommands + 4, numServerCommands + 8};
}

constexpr int32_t kTempBoxModel = 0x10000; // CM_TempBoxModel's handle (the original keeps one at a time too)

// Quake III's trace_t (56 bytes), as the game logic's traces.
void writeTrace(Qvm& vm, int32_t address, const TraceResult& t, int32_t entityNum) {
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
    int type = 3, signs = 0;
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
Vec3f readVec3(const Qvm& vm, int32_t address) {
    return {Qvm::asFloat(vm.readInt(address)), Qvm::asFloat(vm.readInt(address + 4)),
            Qvm::asFloat(vm.readInt(address + 8))};
}

std::vector<std::string> tokenize(const std::string& line) {
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
    if (snapshot.messageNum > snapshotNumber_)
        snapshotNumber_ = snapshot.messageNum;
    else
        ++snapshotNumber_;
    snapshotTime_ = snapshot.serverTime;
    snapshots_[snapshotNumber_] = std::move(snapshot);
    while (snapshots_.size() > 32)
        snapshots_.erase(snapshots_.begin());
}

CgameHost::CgameHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars), vmFiles_(files) {
    vm_.setSystemCall([this](Qvm& vm, int number, const int32_t* args) { return systemCall(vm, number, args); });
}

Error CgameHost::load() {
    std::vector<uint8_t> data;
    if (Error err = files_.read("vm/cgame.qvm", data))
        return Error::fail("vm/cgame.qvm: " + err.message);
    if (Error err = vm_.load(data))
        return Error::fail("vm/cgame.qvm: " + err.message);
    layout_ = detectLayout(vm_);
    return Error::success();
}

Error CgameHost::call(int32_t command, const std::vector<int32_t>& args, int32_t& result) {
    return vm_.call(command, args, result);
}

Error CgameHost::consoleCommand(const std::string& text, bool& handled) {
    args_ = tokenize(text);
    int32_t result = 0;
    Error err = call(ConsoleCommand, {}, result);
    handled = !err && result;
    return err;
}

Error CgameHost::init(int32_t clientNum, int32_t serverMessageNum, int32_t serverCommandSequence) {
    int32_t ignored = 0;
    return call(Init, {serverMessageNum, serverCommandSequence, clientNum}, ignored);
}

void CgameHost::writeGameState(int32_t address) {
    if (!vm_.checkWrite(address, kGameStateSize))
        return;
    std::memset(&vm_.memory()[size_t(address)], 0, size_t(kGameStateSize));
    // Offset 0 is an empty string, for configstrings not set.
    const int32_t data = address + 4 * kMaxConfigstrings;
    int32_t used = 1;
    for (const auto& [index, text] : configstrings_) {
        if (index < 0 || index >= kMaxConfigstrings || text.empty())
            continue;
        if (used + int32_t(text.size()) + 1 > kGameStateChars) {
            vm_.abort("GetGameState: the configstrings don't fit in the game state");
            return;
        }
        vm_.writeInt(address + 4 * index, used);
        vm_.writeString(data + used, text, int32_t(text.size()) + 1);
        used += int32_t(text.size()) + 1;
    }
    vm_.writeInt(data + kGameStateChars, used);
}

void CgameHost::writeGlconfig(int32_t address) {
    if (!vm_.checkWrite(address, kGlconfigSize))
        return;
    std::memset(&vm_.memory()[size_t(address)], 0, size_t(kGlconfigSize));
    vm_.writeString(address, "Reluva", 1024);
    vm_.writeString(address + 1024, "Reluva", 1024);
    vm_.writeString(address + 2048, "1.0", 1024);
    // Quake III's words from 5124: maxTextureSize, textureUnits, colour,
    // depth, stencil bits ... vidWidth (+40), vidHeight (+44), aspect (+48).
    const int32_t w = address + kGlconfigWords;
    vm_.writeInt(w + 0, 2048);
    vm_.writeInt(w + 4, 2);
    vm_.writeInt(w + 8, 32);
    vm_.writeInt(w + 12, 24);
    vm_.writeInt(w + 16, 8);
    vm_.writeInt(w + 36, 1);
    vm_.writeInt(w + 40, 640);
    vm_.writeInt(w + 44, 480);
    vm_.writeInt(w + 48, Qvm::fromFloat(640.0f / 480.0f));
    vm_.writeInt(w + 52, 60);
}

int32_t CgameHost::systemCall(Qvm& vm, int number, const int32_t* a) {
    if (trace)
        trace(number, a);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    int32_t result = 0;
    if (number == 100 && a[1] == 0) // memset(address, 0, size): kept, to learn the sizes of what it clears
        cleared_[a[0]] = a[2];
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case Print: {
        const std::string text = str(0, 4096);
        // Kept (the last 4 KB) as well as handed on.
        log_ += text;
        if (log_.size() > 8192)
            log_.erase(0, log_.size() - 4096);
        if (print)
            print(text);
        return 0;
    }
    case Error_: vm.abort("cgame.qvm: " + str(0)); return 0;
    case Milliseconds: return time_;
    case CvarRegister: {
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            cvars_.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case CvarUpdate: cvars_.updateVmCvar(vm, a[0]); return 0;
    case CvarSet: cvars_.set(str(0, 256), str(1, 256), true); return 0; // forced, as the original's Cvar_Set
    case SetLocalCvar:
        // Holomatch's engine sets the cvar forced (read-only ones too),
        // without counting it as a change of the player's settings to send.
        // Reluva sends them only on connecting, so a forced set is enough.
        cvars_.set(str(0, 256), str(1, 256), true);
        return 0;
    case CvarVariableStringBuffer: vm.writeString(a[1], cvars_.string(str(0, 256)), a[2]); return 0;
    case Argc: return int32_t(args_.size());
    case Argv: vm.writeString(a[1], a[0] >= 0 && size_t(a[0]) < args_.size() ? args_[size_t(a[0])] : "", a[2]); return 0;
    case Args: {
        std::string rest;
        for (size_t i = 1; i < args_.size(); ++i)
            rest += (i > 1 ? " " : "") + args_[i];
        vm.writeString(a[0], rest, a[1]);
        return 0;
    }
    case FsFOpenFile: return vmFiles_.open(vm, a[0], a[1], a[2]);
    case FsRead: return vmFiles_.read(vm, a[0], a[1], a[2]);
    case FsFCloseFile: vmFiles_.close(a[0]); return 0;
    case AddCommand: commands_.push_back(str(0, 64)); return 0;
    case SendClientCommand: clientCommands_.push_back(str(0, 1024)); return 0;
    case SendConsoleCommand: consoleCommands_.push_back(str(0, 1024)); return 0;
    case UpdateScreen: // the loading screen
        if (updateScreen)
            updateScreen();
        return 0;
    case CmLoadMap: {
        collisionMap_ = str(0, 256);
        std::vector<uint8_t> data;
        auto bsp = std::make_shared<Bsp>();
        if (files_.read(collisionMap_, data) || parseBsp(data, *bsp)) {
            vm.abort("CM_LoadMap: can't load " + collisionMap_);
            return 0;
        }
        map_ = std::move(bsp);
        world_.reset();
        marks_.reset();
        return 0;
    }
    case CmNumInlineModels: return map_ ? int32_t(map_->models.size()) : 0;
    case SRegisterSound: return sounds_.add(str(0, 256));
    case SStartBackgroundTrack: // (intro, loop)
        music_ = {str(0, 256), str(1, 256)};
        if (soundSystem)
            soundSystem->startBackgroundTrack(music_.first, music_.second);
        return 0;
    case RLoadWorldMap: worldMap_ = str(0, 256); return 0;
    case RRegisterModel: return models_.add(str(0, 256));
    case RRegisterSkin: return skins_.add(str(0, 256));
    case RRegisterShader:
    case RRegisterShaderNoMip: return shaders_.add(str(0, 256));
    case RRegisterShader3D: {
        // The CTF flags' team pictures (models/flags/<team>_red): 0 for a
        // team without one, and the client game takes the default.
        const std::string name = str(0, 256);
        return findImage(files_, name).empty() ? 0 : shaders_.add(name);
    }
    case RClearScene: scene_.clear(); return 0;
    case RSetColor: // (rgba or 0 for white)
        if (!a[0])
            color_ = {1, 1, 1, 1};
        else
            for (int i = 0; i < 4; ++i)
                color_[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
        return 0;
    case RDrawStretchPic: { // (x, y, w, h, s1, t1, s2, t2, shader)
        auto f = [&](int i) { return Qvm::asFloat(a[i]); };
        pictures_.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[8], color_});
        return 0;
    }
    case RModelBounds: { // (model, mins[3], maxs[3])
        std::array<float, 3> mins{}, maxs{};
        const std::string& name = models_.name(a[0]);
        if (!name.empty() && name[0] == '*') { // one of the map's brush models
            const int n = std::atoi(name.c_str() + 1);
            if (map_ && n > 0 && size_t(n) < map_->models.size()) {
                mins = map_->models[size_t(n)].mins;
                maxs = map_->models[size_t(n)].maxs;
            }
        } else if (!name.empty()) { // an MD3's first frame, as the original renderer gives
            std::vector<uint8_t> data;
            Md3Model md3;
            if (!files_.read(name, data) && !parseModel(data, md3) && !md3.frames.empty()) {
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
    case SClearLoopingSounds:
        if (soundSystem)
            soundSystem->clearLoopingSounds();
        return 0;
    case GetCurrentSnapshotNumber: // (&number, &serverTime)
        vm.writeInt(a[0], snapshotNumber_);
        vm.writeInt(a[1], snapshotTime_);
        return 0;
    case GetSnapshot: { // (number, snapshot_t*) -> whether it's still there
        snapshotRequests_.emplace_back(a[0], a[1]);
        const auto it = snapshots_.find(a[0]);
        const SnapshotLayout l = snapshotLayout(layout_.playerStateSize);
        if (it == snapshots_.end() || !vm.checkWrite(a[1], l.size))
            return 0;
        const GameHost::Snapshot& s = it->second;
        std::memset(&vm.memory()[size_t(a[1])], 0, size_t(l.numEntities)); // flags, ping, area mask (all visible)
        vm.writeInt(a[1], s.snapFlags);
        vm.writeInt(a[1] + kSnapServerTime, s.serverTime);
        std::memcpy(&vm.memory()[size_t(a[1] + kSnapAreaMask)], s.areaMask.data(), s.areaMask.size());
        std::memcpy(&vm.memory()[size_t(a[1] + kSnapPlayerState)], s.playerState.data(),
                    std::min(s.playerState.size(), size_t(layout_.playerStateSize)));
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
    case CmInlineModel: return a[0]; // the map's model n is clip model n
    case CmTempBoxModel: // (mins, maxs): a box to trace against, until the next one
        for (int i = 0; i < 3; ++i) {
            tempBoxMins_[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
            tempBoxMaxs_[size_t(i)] = Qvm::asFloat(vm.readInt(a[1] + 4 * i));
        }
        return kTempBoxModel;
    case CmPointContents: // (point, model)
    case CmTransformedPointContents: { // (point, model, origin, angles)
        auto vec = [&](int32_t addr) {
            Vec3f v{};
            if (addr)
                for (int i = 0; i < 3; ++i)
                    v[size_t(i)] = Qvm::asFloat(vm.readInt(addr + 4 * i));
            return v;
        };
        const bool transformed = number == CmTransformedPointContents;
        const Vec3f point = vec(a[0]), origin = transformed ? vec(a[2]) : Vec3f{},
                    angles = transformed ? vec(a[3]) : Vec3f{};
        if (a[1] == kTempBoxModel) {
            for (int i = 0; i < 3; ++i) {
                const float p = point[size_t(i)] - origin[size_t(i)];
                if (p < tempBoxMins_[size_t(i)] || p > tempBoxMaxs_[size_t(i)])
                    return 0;
            }
            return contents::Body;
        }
        if (!map_)
            return 0;
        if (!world_)
            world_ = std::make_unique<CollisionWorld>(*map_);
        return world_->transformedPointContents(point, a[1], origin, angles);
    }
    case CmBoxTrace: // (results, start, end, mins, maxs, model, brushmask)
    case CmTransformedBoxTrace: { // ... origin, angles
        auto vec = [&](int32_t addr) {
            Vec3f v{};
            if (addr)
                for (int i = 0; i < 3; ++i)
                    v[size_t(i)] = Qvm::asFloat(vm.readInt(addr + 4 * i));
            return v;
        };
        const bool transformed = number == CmTransformedBoxTrace;
        const Vec3f start = vec(a[1]), end = vec(a[2]), mins = vec(a[3]), maxs = vec(a[4]);
        const Vec3f origin = transformed ? vec(a[7]) : Vec3f{}, angles = transformed ? vec(a[8]) : Vec3f{};
        TraceResult t;
        if (a[5] == kTempBoxModel) {
            // A box isn't turned, as the original's.
            Vec3f s = start, e = end;
            for (int i = 0; i < 3; ++i) {
                s[size_t(i)] -= origin[size_t(i)];
                e[size_t(i)] -= origin[size_t(i)];
            }
            t = CollisionWorld::traceBox(s, e, mins, maxs, tempBoxMins_, tempBoxMaxs_, contents::Body);
            for (int i = 0; i < 3; ++i)
                t.endPos[size_t(i)] += origin[size_t(i)];
        } else if (map_) {
            if (!world_)
                world_ = std::make_unique<CollisionWorld>(*map_);
            t = world_->transformedTrace(start, end, mins, maxs, a[5], a[6], origin, angles);
        }
        writeTrace(vm, a[0], t, t.fraction < 1 ? 1022 : 1023);
        return 0;
    }
    case CmMarkFragments: {
        // (numPoints, points, projection, maxPoints, pointBuffer, maxFragments, fragmentBuffer) -> fragments:
        // the pieces of the level under a mark, each (firstPoint, numPoints) into the points written.
        if (!map_ || a[0] < 3 || a[0] > 64 || !vm.valid(a[1], a[0] * 12) || !vm.valid(a[2], 12))
            return 0;
        if (!marks_)
            marks_ = std::make_unique<MarkSurfaces>(*map_, subdivisions);
        auto vec = [&](int32_t at) {
            return Vec3f{Qvm::asFloat(vm.readInt(at)), Qvm::asFloat(vm.readInt(at + 4)),
                         Qvm::asFloat(vm.readInt(at + 8))};
        };
        std::vector<Vec3f> polygon;
        for (int32_t i = 0; i < a[0]; ++i)
            polygon.push_back(vec(a[1] + 12 * i));
        std::vector<Vec3f> points;
        std::vector<MarkSurfaces::Fragment> fragments;
        marks_->mark(polygon, vec(a[2]), size_t(std::max(a[3], 0)), size_t(std::max(a[5], 0)), points, fragments);
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
    case SStartSound: // (origin or 0 to follow the entity, entityNum, channel, sfx)
        if (soundSystem) {
            std::optional<Vec3f> origin;
            if (a[0] && vm.valid(a[0], 12))
                origin = readVec3(vm, a[0]);
            soundSystem->startSound(origin, a[1], a[2], sounds_.name(a[3]));
        }
        return 0;
    case SStartLocalSound: // (sfx, channel)
        if (soundSystem)
            soundSystem->startLocalSound(sounds_.name(a[0]), a[1]);
        return 0;
    case SAddLoopingSound: // (entityNum, origin, velocity, sfx): seen as (0, ..., ..., 109) while firing
        if (soundSystem && vm.valid(a[1], 12))
            soundSystem->addLoopingSound(a[0], readVec3(vm, a[1]), sounds_.name(a[3]));
        return 0;
    case SUpdateEntityPosition: // (entityNum, origin)
        if (soundSystem && vm.valid(a[1], 12))
            soundSystem->updateEntityPosition(a[0], readVec3(vm, a[1]));
        return 0;
    case SRespatialize: // (entityNum, origin, axes, inWater)
        if (soundSystem && vm.valid(a[1], 12) && vm.valid(a[2], 36))
            soundSystem->respatialize(a[0], readVec3(vm, a[1]),
                                      {readVec3(vm, a[2]), readVec3(vm, a[2] + 12), readVec3(vm, a[2] + 24)});
        return 0;
    case RAddRefEntityToScene: {
        const auto it = cleared_.find(a[0]);
        scene_.addEntity(vm, a[0], it != cleared_.end() ? it->second : 0);
        return 0;
    }
    case RAddPolyToScene: scene_.addPoly(vm, a[0], a[1], a[2]); return 0; // (shader, numVerts, verts)
    case RAddLightToScene: // (origin, intensity, r, g, b)
        scene_.addLight(vm, a[0], Qvm::asFloat(a[1]), Qvm::asFloat(a[2]), Qvm::asFloat(a[3]), Qvm::asFloat(a[4]));
        return 0;
    case RRenderScene: scene_.render(vm, a[0], pictures_.size()); return 0;
    case RLerpTag: { // (orientation_t*, model, startFrame, endFrame, frac, tagName) -> found
        auto [it, added] = md3s_.try_emplace(a[1]);
        if (added) {
            std::vector<uint8_t> data;
            auto md3 = std::make_shared<Md3Model>();
            const std::string& name = models_.name(a[1]);
            if (!name.empty() && name[0] != '*' && !files_.read(name, data) && !parseModel(data, *md3))
                it->second = std::move(md3);
        }
        return writeLerpTag(vm, a[0], it->second.get(), a[2], a[3], Qvm::asFloat(a[4]), str(5, 64));
    }
    case GetCurrentCmdNumber: return userCommandNumber_;
    case GetUserCmd: { // (number, usercmd_t*) -> whether it's still kept
        const auto it = userCommands_.find(a[0]);
        if (it == userCommands_.end() || !vm.checkWrite(a[1], 24))
            return 0;
        GameHost::writeUserCommand(vm, a[1], it->second);
        return 1;
    }
    case SetUserCmdValue: weaponSelect_ = a[0]; return 0; // (weapon, sensitivity scale): what the next commands carry
    case GetServerCommand: { // (sequence) -> whether it's there; it becomes Argc/Argv
        if (a[0] < 1 || size_t(a[0]) > serverCommands_.size())
            return 0;
        // As the original's client, the engine acts on some first: a
        // configstring that changed (cs index "text", or in pieces, bcs0,
        // bcs1... bcs2, which the client game doesn't see until the last,
        // as one cs) is set before the client game hears of it.
        std::string text = serverCommands_[size_t(a[0] - 1)];
        args_ = tokenize(text);
        const std::string verb = args_.empty() ? std::string() : args_[0];
        if ((verb == "bcs0" || verb == "bcs1" || verb == "bcs2") && args_.size() >= 3) {
            if (verb == "bcs0")
                bigConfigstring_.clear();
            bigConfigstring_ += args_[2];
            if (verb != "bcs2")
                return 0;
            text = "cs " + args_[1] + " \"" + bigConfigstring_ + "\"";
            args_ = {"cs", args_[1], bigConfigstring_};
        }
        if (!args_.empty() && args_[0] == "cs" && args_.size() >= 2) {
            const int32_t index = std::atoi(args_[1].c_str());
            if (index >= 0 && index < kMaxConfigstrings)
                configstrings_[index] = args_.size() >= 3 ? args_[2] : std::string();
        }
        return 1;
    }
    case Floor: return Qvm::fromFloat(std::floor(Qvm::asFloat(a[0])));
    case MemoryRemaining: return 64 << 20; // plenty: the client game loads every player model up front
    case GetGlconfig: writeGlconfig(a[0]); return 0;
    case GetGameState: writeGameState(a[0]); return 0;
    default:
        if (!unhandled_[number]++)
            firstUnhandled_[number] = {a[0], a[1], a[2], a[3], a[4], a[5]};
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
