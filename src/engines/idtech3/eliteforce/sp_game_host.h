// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
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

// The parts of an entity the engine and the game share (the rest is
// the game's own), as the original's server reads them: the entity
// state (232 bytes), then these.
struct SpEntityLayout {
    int32_t number = 0, modelIndex = 0xa0, solid = 0xb4;
    int32_t stateSize = 0xe8;
    int32_t client = 0xe8, inUse = 0xec, linked = 0xf0, svFlags = 0xf4, bmodel = 0xf8;
    int32_t mins = 0xfc, maxs = 0x108, contents = 0x114, absMin = 0x118, absMax = 0x124;
    int32_t currentOrigin = 0x130, currentAngles = 0x13c;
    int32_t owner = 0x148; // a pointer to the entity, not its number
};

// Elite Force's single-player game logic (the server side of
// efgamex86.dll), run in the x86 interpreter, with the engine's side of the
// interface it was built for (docs/formats/idtech3/eliteforce-sp-interface.md):
// the 40 functions GetGameAPI is handed, and calls into the 13 it hands
// back. One player, client 0, as in the original.
class SpGameHost {
public:
    // The game's functions, by their place in its table (after the API
    // version at 0).
    enum Export : int {
        Init = 1,
        Shutdown = 2,
        WriteLevel = 3,
        ReadLevel = 4,
        GameAllowedToSaveHere = 5,
        ClientConnect = 6,
        ClientBegin = 7,
        ClientUserinfoChanged = 8,
        ClientDisconnect = 9,
        ClientCommand = 10,
        ClientThink = 11,
        RunFrame = 12,
        ConsoleCommand = 13,
    };
    static constexpr int32_t kApiVersion = 6;
    static constexpr int32_t kFrameMs = 50; // 20 frames a second, as the original's server
    static constexpr uint32_t kMemory = 256u << 20;

    static constexpr SpEntityLayout kEntity{};

    SpGameHost(const SearchSet& files, CvarStore& cvars);
    ~SpGameHost();

    // Loads the DLL (efgamex86.dll's bytes), runs its start-up and
    // GetGameAPI, and checks the version.
    Error load(const std::vector<uint8_t>& dll);
    // Starts a level: the map's cvars, then the game's Init with the map's
    // entity text. `checksum`: the map file's (mapChecksum()).
    Error startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t checksum,
                     int32_t randomSeed = 0);
    // A map file's checksum as the original's engine makes it (MD4, its
    // four words XOR'd together), which the game is handed.
    static int32_t mapChecksum(const std::vector<uint8_t>& file);
    // The player joining (ClientConnect, then ClientBegin); `refused` gets
    // the game's reason if it says no.
    Error connectPlayer(const std::string& userinfo, std::string& refused);
    // One server frame: the level time moves on and the game's RunFrame.
    Error runFrame();
    // A console command the engine doesn't know, for the game
    // (ConsoleCommand); `handled` gets whether it knew it.
    Error consoleCommand(const std::string& text, bool* handled = nullptr);

    // --- The client game (the DLL's other half) ---------------------------
    // vmMain's commands.
    enum CgameCommand : int {
        CgInit = 0,       // (serverMessageNum)
        CgShutdown = 1,
        CgConsoleCommand = 2,
        CgDrawActiveFrame = 3, // (serverTime, stereo)
        CgCrosshairPlayer = 4,
        CgCameraPos = 5, // (vec3 out) -> whether a camera is running
    };
    // A player's input, as SP's usercmd_t (28 bytes): server time 0,
    // buttons 4, weapon 8 (a byte), angles 12 (65536 a turn), forward,
    // right and up moves 24-26 (-127..127).
    struct UserCommand {
        int32_t serverTime = 0, buttons = 0;
        uint8_t weapon = 0;
        std::array<int32_t, 3> angles{};
        int8_t forward = 0, right = 0, up = 0;
    };
    static constexpr int32_t kPlayerStateSize = 544, kEntityStateSize = 232, kUserCmdSize = 28;
    // Starts the client game on the level (its CG_INIT: it registers what
    // it will draw), once the player has joined.
    Error startClientGame();
    // One frame of the client game (CG_DRAW_ACTIVE_FRAME at the level
    // time), with the player's input, which the server game gets on its
    // next frame (ClientThink).
    Error drawClientFrame(const UserCommand& input);
    bool clientGameRunning() const { return cgameRunning_; }
    // What the client game registered, by kind.
    const HandleTable& models() const { return models_; }
    const HandleTable& shaders() const { return shaders_; }
    const HandleTable& skins() const { return skins_; }
    const HandleTable& sounds() const { return sounds_; }
    // What the last client frame drew: 3D scene entities (the model handle
    // of each), polygons, lights, rendered scenes, 2D pictures.
    struct ClientFrame {
        std::vector<int32_t> entityModels;
        int polys = 0, lights = 0, scenes = 0, pictures = 0;
    };
    const ClientFrame& lastClientFrame() const { return lastFrame_; }
    // The snapshots made so far (one each server frame once the client
    // game runs).
    int32_t snapshotCount() const { return snapshotNumber_; }

    int32_t levelTime() const { return levelTime_; }
    // What the game's table says: its entities, how big each is, how many
    // are in use.
    uint32_t entities() const;
    int32_t entitySize() const;
    int32_t entityCount() const;
    int32_t entityInt(int32_t number, int32_t offset) const;
    Vec3f entityVec(int32_t number, int32_t offset) const;
    const std::set<int32_t>& linked() const { return linked_; }
    const std::map<int32_t, std::string>& configstrings() const { return configstrings_; }
    const std::vector<std::string>& consoleCommands() const { return consoleCommands_; }
    const std::vector<std::pair<int32_t, std::string>>& serverCommands() const { return serverCommands_; }
    // How many times the game called each of the engine's functions.
    const std::map<int, int>& callCounts() const { return callCounts_; }
    // The same for the client game's system calls (the server game calls
    // into its client half too: they share the DLL), and those not yet
    // answered.
    const std::map<int, int>& cgameCallCounts() const { return cgameCallCounts_; }
    const std::map<int, int>& cgameUnhandled() const { return cgameUnhandled_; }

    x86::Cpu& cpu() { return *cpu_; }
    x86::Win32Process& process() { return *process_; }
    const x86::Win32Process::Module* module() const { return module_; }

    // What the game prints (Printf).
    std::function<void(const std::string&)> print;
    // Each call the game makes to the engine: its slot and the address it
    // was called from.
    std::function<void(int slot, uint32_t from)> trace;

private:
    Error callGame(int exportIndex, const std::vector<uint32_t>& args, uint32_t* result = nullptr);
    void addImports();
    // The client game's system calls (dllEntry's function): syscall(number, args...).
    uint32_t cgameCall(x86::Cpu& c);
    void writeVmCvar(uint32_t address, const CvarStore::Cvar& cv);
    uint32_t fsOpen(x86::Cpu& c, uint32_t path, uint32_t handleAddress, uint32_t mode);
    uint32_t fsRead(x86::Cpu& c, uint32_t buffer, uint32_t length, uint32_t handle);
    uint32_t fsWrite(x86::Cpu& c, uint32_t buffer, uint32_t length, uint32_t handle);
    void import(int slot, std::function<uint32_t(x86::Cpu&)> fn);
    // Text into emulated memory (freed when the level ends).
    uint32_t text(const std::string& s);
    // The engine's cvar structures the game holds pointers to, kept up to
    // date: name 0, text 4, default 8, latched 12, flags 16, modified 20,
    // modification count 24, value 28, integer 32.
    uint32_t cvarStructure(const std::string& name);
    void syncCvars();
    int32_t entityNumber(uint32_t address) const;
    uint32_t entityAddress(int32_t number) const;
    Vec3f readVec(uint32_t address) const;
    void writeVec(uint32_t address, const Vec3f& v);
    float readFloat(uint32_t address) const;
    void linkEntity(uint32_t address);
    void unlinkEntity(uint32_t address);
    TraceResult traceMove(const Vec3f& start, const Vec3f& mins, const Vec3f& maxs, const Vec3f& end,
                          int32_t passEntity, int32_t mask, int32_t& hit);
    void writeTrace(uint32_t results, const TraceResult& t, int32_t hit);
    int32_t pointContents(const Vec3f& point, int32_t passEntity);
    bool areasConnected(int area1, int area2) const;
    std::pair<int, int> entityAreas(int32_t n) const;
    std::string infoString(int32_t flag) const;

    const SearchSet& files_;
    CvarStore& cvars_;
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    const x86::Win32Process::Module* module_ = nullptr;
    uint32_t imports_ = 0, exports_ = 0, voiceVolume_ = 0, argvBuffer_ = 0;
    std::shared_ptr<const Bsp> map_;
    std::unique_ptr<CollisionWorld> world_;
    std::unique_ptr<AreaPortals> areaPortals_;
    std::vector<uint32_t> levelText_;
    struct MirroredCvar {
        uint32_t address = 0, text = 0;
        int32_t modificationCount = -1;
    };
    std::map<std::string, MirroredCvar> cvarMirrors_; // by lower-case name
    struct OpenFile {
        std::vector<uint8_t> data;
        size_t position = 0;
        bool writing = false;
    };
    std::map<uint32_t, OpenFile> openFiles_;
    uint32_t nextFile_ = 1;
    std::vector<std::string> args_;
    std::set<int32_t> linked_;
    std::map<int32_t, std::string> configstrings_, userinfo_;
    std::vector<std::string> consoleCommands_;
    std::vector<std::pair<int32_t, std::string>> serverCommands_;
    std::map<std::string, std::vector<uint8_t>> saveChunks_;
    std::map<int, int> callCounts_, cgameCallCounts_, cgameUnhandled_;
    // The client game.
    struct Snapshot {
        int32_t number = 0, serverTime = 0, serverCommandSequence = 0, commandNumber = 0;
        std::array<uint8_t, 32> areaMask{};
        std::vector<uint8_t> playerState;
        std::vector<std::vector<uint8_t>> entities;
    };
    void makeSnapshot();
    std::vector<int32_t> visibleEntities(const Vec3f& eye);
    void writeSnapshot(uint32_t address, const Snapshot& snap);
    void writeGameState(uint32_t address);
    void writeGlconfig(uint32_t address);
    void writeUserCommand(uint32_t address, const UserCommand& cmd);
    void tokenize(const std::string& text);
    void clientTrace(x86::Cpu& c, bool transformed);
    bool cgameRunning_ = false;
    uint32_t cgameVmMain_ = 0;
    std::vector<Snapshot> snapshots_; // the last 32, by number
    int32_t snapshotNumber_ = 0;
    std::vector<UserCommand> commands_ = std::vector<UserCommand>(64);
    int32_t commandNumber_ = 0;
    uint8_t clientWeapon_ = 0;
    std::vector<std::string> clientCommands_; // AddCommand's names
    HandleTable models_, shaders_, skins_, sounds_;
    ClientFrame frame_, lastFrame_;
    std::vector<std::pair<Vec3f, Vec3f>> tempBoxes_; // CM_TempBoxModel's box
    static constexpr uint32_t kTempBoxModel = 255;
    int32_t levelTime_ = 0;
    uint64_t startMs_ = 0;
};

} // namespace reluva::idtech3::eliteforce
