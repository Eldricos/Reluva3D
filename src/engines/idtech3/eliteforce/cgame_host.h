// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/vm_services.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's client game (vm/cgame.qvm: the player's view, HUD and
// effects) run with the engine side of its system calls, in Elite Force's
// numbering (docs/formats/idtech3/eliteforce-syscalls.md). For now it
// starts: it's given the server's configstrings as its game state, and
// what it loads and registers is recorded. Snapshots and drawing come
// next; calls not implemented are counted in unhandled().
class CgameHost {
public:
    // vmMain's commands (Quake III's order; Init seen on Elite Force's
    // cgame.qvm).
    enum Command : int32_t {
        Init = 0, // (serverMessageNum, serverCommandSequence, clientNum)
        Shutdown = 1,
        ConsoleCommand = 2,
        DrawActiveFrame = 3, // (serverTime, stereoView, demoPlayback)
    };

    CgameHost(const SearchSet& files, CvarStore& cvars);

    // Reads vm/cgame.qvm.
    Error load();
    // The configstrings the server has set, as the client receives them
    // (its game state).
    void setConfigstrings(std::map<int32_t, std::string> configstrings) { configstrings_ = std::move(configstrings); }
    // CG_INIT for the player `clientNum`.
    Error init(int32_t clientNum, int32_t serverMessageNum = 0, int32_t serverCommandSequence = 0);
    Error call(int32_t command, const std::vector<int32_t>& args, int32_t& result);
    // A new snapshot from the server: what GetCurrentSnapshotNumber
    // reports (its number counts up from 1, with the server's time).
    void newSnapshot(int32_t serverTime) {
        ++snapshotNumber_;
        snapshotTime_ = serverTime;
    }
    // Where the client game asked for each snapshot (GetSnapshot's
    // number and buffer), in order: not filled in yet.
    const std::vector<std::pair<int32_t, int32_t>>& snapshotRequests() const { return snapshotRequests_; }

    Qvm& vm() { return vm_; }
    // What it registered, by handle.
    const HandleTable& models() const { return models_; }
    const HandleTable& skins() const { return skins_; }
    const HandleTable& shaders() const { return shaders_; }
    const HandleTable& sounds() const { return sounds_; }
    // The console commands it added (AddCommand).
    const std::vector<std::string>& commands() const { return commands_; }
    // The map it asked for, for collision (CM_LoadMap) and drawing
    // (R_LoadWorldMap), and the music (S_StartBackgroundTrack: intro, loop).
    const std::string& collisionMap() const { return collisionMap_; }
    const std::string& worldMap() const { return worldMap_; }
    const std::pair<std::string, std::string>& music() const { return music_; }
    // The loaded collision map, if CM_LoadMap found it.
    std::shared_ptr<const Bsp> map() const { return map_; }

    std::function<void(const std::string&)> print;
    const std::string& log() const { return log_; }
    const std::map<int, int>& unhandled() const { return unhandled_; }
    // The arguments of the first call to each call not implemented.
    const std::map<int, std::array<int32_t, 6>>& firstUnhandled() const { return firstUnhandled_; }
    std::function<void(int number, const int32_t* args)> trace;

private:
    int32_t systemCall(Qvm& vm, int number, const int32_t* args);
    void writeGameState(int32_t address);
    void writeGlconfig(int32_t address);

    const SearchSet& files_;
    CvarStore& cvars_;
    Qvm vm_;
    VmFiles vmFiles_;
    std::map<int32_t, std::string> configstrings_;
    HandleTable models_, skins_, shaders_, sounds_;
    std::vector<std::string> commands_;
    std::string collisionMap_, worldMap_;
    std::pair<std::string, std::string> music_;
    std::shared_ptr<const Bsp> map_;
    std::string log_;
    std::map<int, int> unhandled_;
    std::map<int, std::array<int32_t, 6>> firstUnhandled_;
    int32_t time_ = 0;
    int32_t snapshotNumber_ = 0, snapshotTime_ = 0;
    std::vector<std::pair<int32_t, int32_t>> snapshotRequests_;
};

} // namespace reluva::idtech3::eliteforce
