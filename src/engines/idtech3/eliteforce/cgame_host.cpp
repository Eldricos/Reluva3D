// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/cgame_host.h"

#include "engines/idtech3/md3.h"

#include <array>
#include <cstdlib>
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
    SClearLoopingSounds = 30,
    CmLoadMap = 18,
    CmNumInlineModels = 19,
    SRegisterSound = 34,
    SStartBackgroundTrack = 35,
    RLoadWorldMap = 36,
    RRegisterModel = 37,
    RRegisterSkin = 38,
    RRegisterShader = 39,
    RClearScene = 40,
    RModelBounds = 47,
    GetGlconfig = 49,
    GetGameState = 50,
    GetCurrentSnapshotNumber = 51,
    GetSnapshot = 52,
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

} // namespace

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
    case Argc: return 0;
    case Argv: vm.writeString(a[1], "", a[2]); return 0;
    case Args: vm.writeString(a[0], "", a[1]); return 0;
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
    case RClearScene: return 0;
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
    case GetSnapshot: // (number, snapshot_t*) -> whether it's there: not yet
        snapshotRequests_.emplace_back(a[0], a[1]);
        ++unhandled_[number];
        firstUnhandled_.try_emplace(number, std::array<int32_t, 6>{a[0], a[1], a[2], a[3], a[4], a[5]});
        return 0;
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
