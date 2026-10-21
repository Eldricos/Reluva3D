// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/vm_services.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's game logic (vm/qagame.qvm, the server side of Holomatch)
// run with the engine side of its system calls, in Elite Force's
// numbering (docs/formats/idtech3/eliteforce-syscalls.md). For now it
// loads a level: the map's entities are handed over, and what the game
// does with them is recorded. Calls that would fill in the game's own
// structures (traces, brush models) wait until their layouts are known;
// they're counted in unhandled().
class GameHost {
public:
    // vmMain's commands (Quake III's order; Init confirmed on Elite
    // Force's qagame.qvm).
    enum Command : int32_t {
        Init = 0, // (levelTime, randomSeed, restart)
        Shutdown = 1,
        ClientConnect = 2,
        ClientBegin = 3,
        ClientUserinfoChanged = 4,
        ClientDisconnect = 5,
        ClientCommand = 6,
        ClientThink = 7,
        RunFrame = 8, // (levelTime)
        ConsoleCommand = 9,
        BotAiStartFrame = 10,
    };

    GameHost(const SearchSet& files, CvarStore& cvars);

    // Reads vm/qagame.qvm.
    Error load();
    // Starts a level: sets the map's cvars, hands the game its entity
    // text (the BSP's entity lump) and calls Init, then runs a few frames
    // as the original server does to let things settle. The map is also
    // what things collide with.
    Error startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t randomSeed = 0);
    Error runFrame();
    Error call(int32_t command, const std::vector<int32_t>& args, int32_t& result);

    Qvm& vm() { return vm_; }
    int32_t levelTime() const { return levelTime_; }

    // What the game has told the engine.
    struct GameData {
        int32_t entities = 0;   // address of the entity array
        int32_t count = 0;      // entities in use so far (the highest + 1)
        int32_t entitySize = 0; // bytes per entity
        int32_t clients = 0;    // address of the client array
        int32_t clientSize = 0;
    };
    const GameData& gameData() const { return gameData_; }
    // A field of an entity, read from the game's memory (see the layout
    // in game_host.cpp).
    int32_t entityInt(int32_t number, int32_t offset);
    Vec3f entityVec(int32_t number, int32_t offset);
    // Entity numbers linked into the world (LinkEntity), not unlinked since.
    const std::set<int32_t>& linked() const { return linked_; }
    const std::map<int32_t, std::string>& configstrings() const { return configstrings_; }
    // Commands the game ran (SendConsoleCommand), in order.
    const std::vector<std::string>& consoleCommands() const { return consoleCommands_; }

    std::function<void(const std::string&)> print;
    const std::string& log() const { return log_; }
    const std::map<int, int>& unhandled() const { return unhandled_; }
    std::function<void(int number, const int32_t* args)> trace;

private:
    int32_t systemCall(Qvm& vm, int number, const int32_t* args);
    // The next token of the entity text, the way the original parses it:
    // a quoted string, or anything up to white space. Empty at the end.
    std::string nextEntityToken();
    int32_t entityNumber(int32_t address) const;
    std::string infoString(int32_t flag) const;
    int32_t entityAddress(int32_t number) const;
    Vec3f readVec(int32_t address);
    void writeVec(int32_t address, const Vec3f& v);
    void linkEntity(int32_t address);
    void clipMove(int32_t results, const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
               int32_t passEntity, int32_t mask);
    int32_t pointContents(const Vec3f& point, int32_t passEntity);

    const SearchSet& files_;
    CvarStore& cvars_;
    Qvm vm_;
    VmFiles vmFiles_;
    std::shared_ptr<const Bsp> map_;
    std::unique_ptr<CollisionWorld> world_;
    std::string entityText_;
    size_t entityPos_ = 0;
    GameData gameData_;
    std::set<int32_t> linked_;
    std::map<int32_t, std::string> configstrings_;
    std::vector<std::string> consoleCommands_;
    std::string log_;
    std::map<int, int> unhandled_;
    int32_t levelTime_ = 0;
    uint64_t startMs_ = 0;
};

} // namespace reluva::idtech3::eliteforce
