// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/cgame_host.h"

#include "engines/idtech3/md3.h"

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
// docs/formats/idtech3/eliteforce-syscalls.md).
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
    MemoryRemaining = 58,
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
constexpr int32_t kSnapshotSize = 52740;
constexpr int32_t kSnapServerTime = 8, kSnapPlayerState = 44; // the area mask is at 12, zeros: every area visible
constexpr int32_t kSnapNumEntities = 504, kSnapEntities = 508;
constexpr int32_t kSnapNumServerCommands = 52732, kSnapServerCommandSequence = 52736;
constexpr int32_t kMaxSnapshotEntities = 256;

constexpr int32_t kRefdefSize = 112;      // the client game clears this much of cg.refdef
constexpr int32_t kRefEntityKept = 256;   // bytes kept of each refEntity_t until its size is known
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
        if (print)
            print(text);
        else
            log_ += text;
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
    case CvarSet: cvars_.set(str(0, 256), str(1, 256)); return 0;
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
    case UpdateScreen: return 0; // the loading screen: nothing to show yet
    case CmLoadMap: {
        collisionMap_ = str(0, 256);
        std::vector<uint8_t> data;
        auto bsp = std::make_shared<Bsp>();
        if (files_.read(collisionMap_, data) || parseBsp(data, *bsp)) {
            vm.abort("CM_LoadMap: can't load " + collisionMap_);
            return 0;
        }
        map_ = std::move(bsp);
        return 0;
    }
    case CmNumInlineModels: return map_ ? int32_t(map_->models.size()) : 0;
    case SRegisterSound: return sounds_.add(str(0, 256));
    case SStartBackgroundTrack: music_ = {str(0, 256), str(1, 256)}; return 0;
    case RLoadWorldMap: worldMap_ = str(0, 256); return 0;
    case RRegisterModel: return models_.add(str(0, 256));
    case RRegisterSkin: return skins_.add(str(0, 256));
    case RRegisterShader:
    case RRegisterShaderNoMip: return shaders_.add(str(0, 256));
    case RClearScene: scene_.pending = scene_.entities.size(); return 0;
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
            if (!files_.read(name, data) && !parseMd3(data, md3) && !md3.frames.empty()) {
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
    case SClearLoopingSounds: return 0; // sounds in the world: later
    case GetCurrentSnapshotNumber: // (&number, &serverTime)
        vm.writeInt(a[0], snapshotNumber_);
        vm.writeInt(a[1], snapshotTime_);
        return 0;
    case GetSnapshot: { // (number, snapshot_t*) -> whether it's still there
        snapshotRequests_.emplace_back(a[0], a[1]);
        const auto it = snapshots_.find(a[0]);
        if (it == snapshots_.end() || !vm.checkWrite(a[1], kSnapshotSize))
            return 0;
        const GameHost::Snapshot& s = it->second;
        std::memset(&vm.memory()[size_t(a[1])], 0, size_t(kSnapNumEntities)); // flags, ping, area mask (all visible)
        vm.writeInt(a[1] + kSnapServerTime, s.serverTime);
        std::memcpy(&vm.memory()[size_t(a[1] + kSnapPlayerState)], s.playerState.data(),
                    std::min(s.playerState.size(), size_t(GameHost::kPlayerStateSize)));
        const auto count = int32_t(std::min(s.entities.size(), size_t(kMaxSnapshotEntities)));
        vm.writeInt(a[1] + kSnapNumEntities, count);
        for (int32_t i = 0; i < count; ++i)
            std::memcpy(&vm.memory()[size_t(a[1] + kSnapEntities + i * GameHost::kEntityStateSize)],
                        s.entities[size_t(i)].data(), size_t(GameHost::kEntityStateSize));
        vm.writeInt(a[1] + kSnapNumServerCommands, 0);
        vm.writeInt(a[1] + kSnapServerCommandSequence, s.serverCommandSequence);
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
    case CmTransformedPointContents: { // (point, model, origin, angles): turning not yet followed
        Vec3f p{};
        for (int i = 0; i < 3; ++i) {
            p[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
            if (number == CmTransformedPointContents && a[2])
                p[size_t(i)] -= Qvm::asFloat(vm.readInt(a[2] + 4 * i));
        }
        if (a[1] == kTempBoxModel) {
            for (int i = 0; i < 3; ++i)
                if (p[size_t(i)] < tempBoxMins_[size_t(i)] || p[size_t(i)] > tempBoxMaxs_[size_t(i)])
                    return 0;
            return contents::Body;
        }
        if (!map_)
            return 0;
        if (!world_)
            world_ = std::make_unique<CollisionWorld>(*map_);
        return world_->pointContents(p, a[1]);
    }
    case CmBoxTrace: // (results, start, end, mins, maxs, model, brushmask)
    case CmTransformedBoxTrace: { // ... origin, angles: turning not yet followed
        auto vec = [&](int32_t addr) {
            Vec3f v{};
            if (addr)
                for (int i = 0; i < 3; ++i)
                    v[size_t(i)] = Qvm::asFloat(vm.readInt(addr + 4 * i));
            return v;
        };
        Vec3f start = vec(a[1]), end = vec(a[2]);
        const Vec3f mins = vec(a[3]), maxs = vec(a[4]), origin = number == CmTransformedBoxTrace ? vec(a[7]) : Vec3f{};
        for (int i = 0; i < 3; ++i) {
            start[size_t(i)] -= origin[size_t(i)];
            end[size_t(i)] -= origin[size_t(i)];
        }
        TraceResult t;
        if (a[5] == kTempBoxModel)
            t = CollisionWorld::traceBox(start, end, mins, maxs, tempBoxMins_, tempBoxMaxs_, contents::Body);
        else if (map_) {
            if (!world_)
                world_ = std::make_unique<CollisionWorld>(*map_);
            t = world_->trace(start, end, mins, maxs, a[5], a[6]);
        }
        for (int i = 0; i < 3; ++i)
            t.endPos[size_t(i)] += origin[size_t(i)];
        writeTrace(vm, a[0], t, t.fraction < 1 ? 1022 : 1023);
        return 0;
    }
    case CmMarkFragments: return 0; // decals on walls: later
    case SStartSound:
    case SStartLocalSound:
    case SAddLoopingSound:
    case SUpdateEntityPosition:
    case SRespatialize:
        return 0; // sounds in the world: later
    case RAddRefEntityToScene: {
        SceneEntity e;
        const int32_t n = std::max<int32_t>(0, std::min<int32_t>(kRefEntityKept, int32_t(vm.memory().size()) - a[0]));
        if (a[0] > 0 && vm.valid(a[0], n))
            e.bytes.assign(&vm.memory()[size_t(a[0])], &vm.memory()[size_t(a[0])] + n);
        if (const auto it = cleared_.find(a[0]); it != cleared_.end())
            e.clearedSize = it->second;
        scene_.entities.push_back(std::move(e));
        return 0;
    }
    case RAddPolyToScene: ++scene_.polys; return 0;
    case RAddLightToScene: ++scene_.lights; return 0;
    case RRenderScene:
        if (vm.valid(a[0], kRefdefSize))
            scene_.view.assign(&vm.memory()[size_t(a[0])], &vm.memory()[size_t(a[0])] + kRefdefSize);
        ++scene_.renders;
        scene_.views.push_back({scene_.view, scene_.pending, scene_.entities.size() - scene_.pending});
        scene_.pending = scene_.entities.size();
        return 0;
    case RLerpTag: { // (orientation_t*, model, startFrame, endFrame, frac, tagName) -> found
        auto [it, added] = md3s_.try_emplace(a[1]);
        if (added) {
            std::vector<uint8_t> data;
            auto md3 = std::make_shared<Md3Model>();
            const std::string& name = models_.name(a[1]);
            if (!name.empty() && name[0] != '*' && !files_.read(name, data) && !parseMd3(data, *md3))
                it->second = std::move(md3);
        }
        if (!vm.checkWrite(a[0], 48))
            return 0;
        // The tag between two frames: the origin and axes blended (the
        // axes renormalised), as the original renderer does.
        const Md3Model* m = it->second.get();
        const std::string tagName = str(5, 64);
        const Md3Tag* t0 = m ? m->tag(std::clamp(a[2], 0, int32_t(m->frames.size()) - 1), tagName) : nullptr;
        const Md3Tag* t1 = m ? m->tag(std::clamp(a[3], 0, int32_t(m->frames.size()) - 1), tagName) : nullptr;
        if (!t0 || !t1) {
            for (int i = 0; i < 12; ++i)
                vm.writeInt(a[0] + 4 * i, Qvm::fromFloat(i >= 3 && (i - 3) % 4 == 0 ? 1.0f : 0.0f));
            return 0;
        }
        const float frac = Qvm::asFloat(a[4]);
        for (int i = 0; i < 3; ++i)
            vm.writeInt(a[0] + 4 * i,
                        Qvm::fromFloat(t0->origin[size_t(i)] + frac * (t1->origin[size_t(i)] - t0->origin[size_t(i)])));
        for (int j = 0; j < 3; ++j) {
            std::array<float, 3> axis{};
            float length = 0;
            for (int i = 0; i < 3; ++i) {
                axis[size_t(i)] = t0->axis[size_t(j)][size_t(i)] +
                                  frac * (t1->axis[size_t(j)][size_t(i)] - t0->axis[size_t(j)][size_t(i)]);
                length += axis[size_t(i)] * axis[size_t(i)];
            }
            length = length > 0 ? std::sqrt(length) : 1;
            for (int i = 0; i < 3; ++i)
                vm.writeInt(a[0] + 12 + 12 * j + 4 * i, Qvm::fromFloat(axis[size_t(i)] / length));
        }
        return 1;
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
        args_ = tokenize(serverCommands_[size_t(a[0] - 1)]);
        return 1;
    }
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
