// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/scene.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/marks.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
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

// The save chunks are Raven's (Jedi Knight's single player has them too).
using raven::chunkId;
using raven::chunkName;
using raven::SaveReader;
using raven::SaveWriter;

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
    static constexpr int32_t kSettleFrameMs = 100; // the frames run before anyone joins
    static constexpr int32_t kStartTimeMs = 1000;  // a level's time as the game starts
    static constexpr uint32_t kMemory = 256u << 20;

    static constexpr SpEntityLayout kEntity{};

    SpGameHost(const SearchSet& files, CvarStore& cvars);
    ~SpGameHost();

    // Loads the DLL (efgamex86.dll's bytes), runs its start-up and
    // GetGameAPI, and checks the version.
    Error load(const std::vector<uint8_t>& dll);
    // Starts a level: the map's cvars, then the game's Init with the map's
    // entity text. `checksum`: the map file's (mapChecksum()).
    // `loadMode`: 0 a new level, 1 a saved game being loaded, 2 an
    // autosave (the game's Init and ClientConnect are told).
    Error startLevel(const std::string& mapName, std::shared_ptr<const Bsp> map, int32_t checksum,
                     int32_t randomSeed = 0, int32_t loadMode = 0);
    // A map file's checksum as the original's engine makes it (MD4, its
    // four words XOR'd together), which the game is handed.
    static int32_t mapChecksum(const std::vector<uint8_t>& file);
    // glconfig_t as the original engine fills it (5196 bytes, a 640 x 480
    // screen), for the client game and the menus.
    static void writeGlconfigTo(x86::Cpu& cpu, uint32_t address);
    // The player joining (ClientConnect, then ClientBegin); `refused` gets
    // the game's reason if it says no.
    Error connectPlayer(const std::string& userinfo, std::string& refused, int32_t loadMode = 0);
    // One server frame: the level time moves on and the game's RunFrame.
    Error runFrame();
    // --- Saved games (raven/savegame.h) ---------------------------------------
    // Where the game's own chunks go (AppendToSaveGame) and come from
    // (ReadFromSaveGame); none: it can't save or load.
    void setSaveStream(SaveWriter* writer, SaveReader* reader) {
        saveWriter_ = writer;
        saveReader_ = reader;
    }
    // The game's WriteLevel and ReadLevel (`autosave`: the level's start,
    // carrying the player over).
    Error writeLevel(bool autosave);
    Error readLevel(bool autosave);
    // The player's state into the playersave, playerammo<n> and
    // borgadapt<n> cvars, as before a level change.
    void savePlayer();
    // The player's state (0 if there's none yet) and health.
    uint32_t playerState() const;
    int32_t playerHealth() const;
    static constexpr uint32_t kPsHealth = 0xbc; // stats[STAT_HEALTH]
    void setLevelTime(int32_t ms) { levelTime_ = ms; }
    void replaceConfigstrings(const std::map<int32_t, std::string>& strings);

    // The game's GameAllowedToSaveHere(flag), which the menus ask through
    // the engine before opening (0 if the call fails).
    uint32_t allowedToSaveHere(uint32_t flag);
    // A console command the engine doesn't know, for the game
    // (ConsoleCommand); `handled` gets whether it knew it.
    Error consoleCommand(const std::string& text, bool* handled = nullptr);

    // --- The client game (the DLL's other half) ---------------------------
    // vmMain's commands.
    enum CgameCommand : int {
        CgInit = 0,       // (serverCommandSequence)
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
    static constexpr int32_t kRefEntitySize = 152; // refEntity_t, as the original renderer copies it
    // Starts the client game on the level (its CG_INIT: it registers what
    // it will draw), once the player has joined.
    Error startClientGame();
    // The player's input for a moment (its server time set by the caller),
    // which the server game acts on at once (ClientThink), as the
    // original's server does with each command; the client game reads it
    // back for its prediction.
    Error clientThink(const UserCommand& cmd);
    // One frame of the client game (CG_DRAW_ACTIVE_FRAME) at `time`, the
    // player's time in the level.
    Error drawClientFrame(int32_t time);
    // A command the player ran (a key's binding, the console): the client
    // game's if it knows it, else sent to the server game (ClientCommand),
    // as the original engine does.
    Error clientConsoleCommand(const std::string& text);
    // The client game's loading screen into scene() and pictures(), from
    // inside its start (updateScreen).
    Error drawLoadingFrame();
    // Called when the client game asks for the screen to be shown
    // (UpdateScreen, as it loads); none: nothing is shown.
    std::function<void()> updateScreen;
    // Whether the client game's camera (a scripted sequence's) is running,
    // and where it is (CG_CAMERA_POS).
    bool cameraPosition(Vec3f& at);
    // The weapon the client game has chosen (SetUserCmdValue), for commands.
    uint8_t weaponSelect() const { return clientWeapon_; }
    // As the client game sets it (SetUserCmdValue): for a replay.
    void setWeaponSelect(uint8_t weapon) { clientWeapon_ = weapon; }
    bool clientGameRunning() const { return cgameRunning_; }
    // What the client game registered, by kind.
    const HandleTable& models() const { return models_; }
    const HandleTable& shaders() const { return shaders_; }
    const HandleTable& skins() const { return skins_; }
    const HandleTable& sounds() const { return sounds_; }
    // What the last client frame drew: its scenes (each RenderScene's view
    // of the entities, polygons and lights added) and 2D pictures, as the
    // Holomatch client game's, for the same drawer.
    const Scene& scene() const { return lastScene_; }
    const std::vector<Picture>& pictures() const { return lastPictures_; }
    // A scene entity's model handle (refEntity_t's at 8).
    static int32_t entityModel(const SceneEntity& e);
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

    // Where the client game's sounds play (none: silent; their lengths are
    // still known, for the game's timing).
    SoundSystem* soundSystem = nullptr;
    SoundLibrary& soundLibrary() { return soundLibrary_; }

    // Voices starting and ending, for following a scene's timing: a line
    // of text for each ("+12 sound/... 3400 ms", "-12", or why one isn't
    // counted).
    std::function<void(const std::string&)> voiceLog;

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
    void playAmbient(const std::vector<raven::AmbientSets::Sound>& sounds);
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
    SoundLibrary soundLibrary_{files_};
    raven::AmbientSets ambient_;
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    const x86::Win32Process::Module* module_ = nullptr;
    uint32_t imports_ = 0, exports_ = 0, voiceVolume_ = 0, argvBuffer_ = 0;
    std::shared_ptr<const Bsp> map_;
    LightGrid lightGrid_;
    std::unique_ptr<MarkSurfaces> marks_; // made on the first mark
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
    std::vector<size_t> toClient_; // those for the player (0, or -1: everyone), in order
    void addServerCommand(int32_t to, std::string text);
    SaveWriter* saveWriter_ = nullptr;
    SaveReader* saveReader_ = nullptr;
    std::map<int, int> callCounts_, cgameCallCounts_, cgameUnhandled_;
    // Voices playing (sounds on CHAN_VOICE, 3, or CHAN_VOICE_ATTEN, 4), by
    // entity: what the game reads in VoiceVolume (import slot 37) to know
    // a line is still being spoken (its scripts wait for it) and to move
    // the speaker's mouth, as the original's sound code fills it.
    struct Voice {
        std::shared_ptr<const audio::Sound> sound;
        int32_t startMs = 0;
        float average = 0; // the sound's peak (its largest sample's high byte), its thresholds' scale
    };
    std::map<int32_t, Voice> voices_;
    void startVoice(int32_t entity, const std::string& name);
    void updateVoiceVolumes();
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
    bool inUpdateScreen_ = false;
    uint32_t cgameVmMain_ = 0;
    std::vector<Snapshot> snapshots_; // the last 32, by number
    int32_t snapshotNumber_ = 0;
    std::vector<UserCommand> commands_ = std::vector<UserCommand>(64);
    int32_t commandNumber_ = 0;
    uint8_t clientWeapon_ = 0;
    std::vector<std::string> clientCommands_; // AddCommand's names
    HandleTable models_, shaders_, skins_, sounds_;
    int32_t registerSkin(const std::string& name);
    std::map<int32_t, int> skinFaces_; // by skin handle: the -1 to -8 skins after it
    Scene scene_, lastScene_;
    std::vector<Picture> pictures_, lastPictures_;
    std::array<float, 4> color_{1, 1, 1, 1};
    std::map<int32_t, std::shared_ptr<Md3Model>> md3s_; // by model handle, loaded for tags and bounds
    const Md3Model* md3(int32_t handle);
    std::vector<std::pair<Vec3f, Vec3f>> tempBoxes_; // CM_TempBoxModel's box
    static constexpr uint32_t kTempBoxModel = 255;
    int32_t levelTime_ = 0;
    uint64_t startMs_ = 0;
};

// Loading a saved game (docs/formats/idtech3/eliteforce-sp-interface.md,
// "Saved games"), in the original engine's two steps.
struct SaveHeader {
    std::string comment, map;
    std::vector<uint8_t> shot; // 256 x 256 RGBA, bottom row first
    bool autosave = false;
};
// The engine's chunks before the level: the comment, picture and map, the
// cvars kept (set into `cvars`), then GAME and, for an autosave, the player
// carried over (set too).
Error readSaveHeader(SaveReader& r, CvarStore& cvars, SaveHeader& out);
// Once the level has started (load mode 1, or 2 for an autosave) with
// `r` as its save stream: but for an autosave, the time and configstrings;
// then the game's ReadLevel.
Error readSaveLevel(SaveReader& r, SpGameHost& game, bool autosave);

} // namespace reluva::idtech3::eliteforce
