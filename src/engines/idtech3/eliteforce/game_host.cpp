// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/game_host.h"

#include <cmath>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force qagame.qvm system calls (Quake III's game order, the seen
// ones checked against the game's own calls; see
// docs/formats/idtech3/eliteforce-syscalls.md).
enum GameCall : int {
    Print = 0,
    Error_ = 1,
    Milliseconds = 2,
    CvarRegister = 3,
    CvarUpdate = 4,
    CvarSet = 5,
    CvarVariableIntegerValue = 6,
    CvarVariableStringBuffer = 7,
    Argc = 8,
    Argv = 9,
    FsFOpenFile = 10,
    FsRead = 11,
    FsWrite = 12,
    FsFCloseFile = 13,
    SendConsoleCommand = 14,
    LocateGameData = 15,
    DropClient = 16,
    SendServerCommand = 17,
    SetConfigstring = 18,
    GetConfigstring = 19,
    GetUserinfo = 20,
    SetUserinfo = 21,
    GetServerinfo = 22,
    SetBrushModel = 23,
    Trace = 24,
    PointContents = 25,
    InPvs = 26,
    InPvsIgnorePortals = 27,
    AdjustAreaPortalState = 28,
    AreasConnected = 29,
    LinkEntity = 30,
    UnlinkEntity = 31,
    EntitiesInBox = 32,
    EntityContact = 33,
    BotAllocateClient = 34,
    BotFreeClient = 35,
    GetUsercmd = 36,
    GetEntityToken = 37,
    FsGetFileList = 38,
    Ceil = 111, // Quake III's game numbers floor 110 and ceil 111; Elite Force's code uses 111
};

constexpr int32_t kFrameMs = 100; // Quake III servers default to 10 frames a second (sv_fps)

} // namespace

GameHost::GameHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars), vmFiles_(files) {
    vm_.setSystemCall([this](Qvm& vm, int number, const int32_t* args) { return systemCall(vm, number, args); });
}

Error GameHost::load() {
    std::vector<uint8_t> data;
    if (Error err = files_.read("vm/qagame.qvm", data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    if (Error err = vm_.load(data))
        return Error::fail("vm/qagame.qvm: " + err.message);
    return Error::success();
}

Error GameHost::call(int32_t command, const std::vector<int32_t>& args, int32_t& result) {
    return vm_.call(command, args, result);
}

Error GameHost::startLevel(const std::string& mapName, std::string entities, int32_t randomSeed) {
    // What the original server sets before starting the game code.
    cvars_.create("mapname", "", CvarStore::ServerInfo | CvarStore::ReadOnly);
    cvars_.set("mapname", mapName, true);
    cvars_.create("sv_maxclients", "8", CvarStore::ServerInfo | CvarStore::Latch | CvarStore::Archive);
    cvars_.create("g_gametype", "0", CvarStore::ServerInfo | CvarStore::Latch);
    cvars_.create("dedicated", "0", CvarStore::Latch);
    cvars_.create("sv_cheats", "0", CvarStore::ServerInfo | CvarStore::ReadOnly);
    entityText_ = std::move(entities);
    entityPos_ = 0;
    levelTime_ = 0;
    int32_t ignored = 0;
    if (Error err = call(Init, {levelTime_, randomSeed, 0}, ignored))
        return err;
    // "Run a few frames to allow everything to settle", as the original does.
    for (int i = 0; i < 3; ++i)
        if (Error err = runFrame())
            return err;
    return Error::success();
}

Error GameHost::runFrame() {
    levelTime_ += kFrameMs;
    int32_t ignored = 0;
    return call(RunFrame, {levelTime_}, ignored);
}

std::string GameHost::nextEntityToken() {
    const std::string& s = entityText_;
    size_t& i = entityPos_;
    while (true) {
        while (i < s.size() && static_cast<unsigned char>(s[i]) <= ' ' && s[i] != '\0')
            ++i;
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
            continue;
        }
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*') {
            const size_t end = s.find("*/", i + 2);
            i = end == std::string::npos ? s.size() : end + 2;
            continue;
        }
        break;
    }
    if (i >= s.size() || s[i] == '\0')
        return {};
    std::string token;
    if (s[i] == '"') {
        ++i;
        while (i < s.size() && s[i] != '"' && s[i] != '\0')
            token += s[i++];
        if (i < s.size() && s[i] == '"')
            ++i;
        return token;
    }
    while (i < s.size() && static_cast<unsigned char>(s[i]) > ' ')
        token += s[i++];
    return token;
}

int32_t GameHost::entityNumber(int32_t address) const {
    if (!gameData_.entitySize || address < gameData_.entities)
        return -1;
    const int32_t offset = address - gameData_.entities;
    return offset % gameData_.entitySize == 0 ? offset / gameData_.entitySize : -1;
}

std::string GameHost::infoString(int32_t flag) const {
    std::string info;
    for (const CvarStore::Cvar& cv : cvars_.all())
        if (cv.flags & flag)
            info += "\\" + cv.name + "\\" + cv.value;
    return info;
}

int32_t GameHost::systemCall(Qvm& vm, int number, const int32_t* a) {
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
    case Error_: vm.abort("qagame.qvm: " + str(0)); return 0;
    case Milliseconds: return levelTime_;
    case CvarRegister: {
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            cvars_.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case CvarUpdate: cvars_.updateVmCvar(vm, a[0]); return 0;
    case CvarSet: cvars_.set(str(0, 256), str(1, 256)); return 0;
    case CvarVariableIntegerValue: return cvars_.integer(str(0, 256));
    case CvarVariableStringBuffer: vm.writeString(a[1], cvars_.string(str(0, 256)), a[2]); return 0;
    case Argc: return 0;
    case Argv: vm.writeString(a[1], "", a[2]); return 0;
    case FsFOpenFile: return vmFiles_.open(vm, a[0], a[1], a[2]);
    case FsRead: return vmFiles_.read(vm, a[0], a[1], a[2]);
    case FsFCloseFile: vmFiles_.close(a[0]); return 0;
    case FsGetFileList: return vmFiles_.list(vm, a[0], a[1], a[2], a[3]);
    case SendConsoleCommand: consoleCommands_.push_back(str(1, 4096)); return 0;
    case LocateGameData:
        gameData_ = {a[0], a[1], a[2], a[3], a[4]};
        return 0;
    case SendServerCommand: return 0; // no clients yet
    case SetConfigstring: configstrings_[a[0]] = str(1, 8192); return 0;
    case GetConfigstring: {
        const auto it = configstrings_.find(a[0]);
        vm.writeString(a[1], it == configstrings_.end() ? std::string() : it->second, a[2]);
        return 0;
    }
    case GetServerinfo: vm.writeString(a[0], infoString(CvarStore::ServerInfo), a[1]); return 0;
    case LinkEntity:
        if (const int32_t n = entityNumber(a[0]); n >= 0)
            linked_.insert(n);
        return 0;
    case UnlinkEntity:
        if (const int32_t n = entityNumber(a[0]); n >= 0)
            linked_.erase(n);
        return 0;
    case GetEntityToken: {
        const std::string token = nextEntityToken();
        vm.writeString(a[0], token, a[1]);
        return token.empty() ? 0 : 1;
    }
    case Ceil: return Qvm::fromFloat(std::ceil(Qvm::asFloat(a[0])));
    default: ++unhandled_[number]; return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
