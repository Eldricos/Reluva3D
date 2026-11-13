// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/client/scene.h"
#include "engines/idtech3/formats/marks.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/audio/sound_system.h"
#include "engines/idtech3/vm/vm_services.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's client game (vm/cgame.qvm: the player's view, HUD and
// effects) run with the engine side of its system calls, in Elite Force's
// numbering (docs/engines/idtech3/games/stvef/systems/syscalls.md). For now it
// starts: it's given the server's configstrings as its game state, and
// what it loads and registers is recorded. Snapshots and drawing come
// next; calls not implemented are counted in unhandled().
class CgameHost {
public:
    // vmMain's commands (Quake III's order; Init seen on Elite Force's
    // cgame.qvm).
    enum Command : int32_t {
        kInit = 0, // (serverMessageNum, serverCommandSequence, clientNum)
        kShutdown = 1,
        kConsoleCommand = 2,
        kDrawActiveFrame = 3, // (serverTime, stereoView, demoPlayback)
    };

    CgameHost(const SearchSet &files, CvarStore &cvars);

    // Reads vm/cgame.qvm.
    Error load();
    // The configstrings the server has set, as the client receives them
    // (its game state).
    void setConfigstrings(std::map<int32_t, std::string> configstrings) { _configstrings = std::move(configstrings); }
    // CG_INIT for the player `clientNum`.
    Error init(int32_t clientNum, int32_t serverMessageNum = 0, int32_t serverCommandSequence = 0);
    Error call(int32_t command, const std::vector<int32_t> &args, int32_t &result);
    // A new snapshot from the server, numbered from 1: what
    // GetCurrentSnapshotNumber reports and GetSnapshot hands over (the
    // last 32 are kept).
    void newSnapshot(GameHost::Snapshot snapshot);
    // The commands the server has sent this player, in order (what
    // GetServerCommand hands over, by sequence number from 1).
    void setServerCommands(std::vector<std::string> commands) { _serverCommands = std::move(commands); }
    // Its 2D drawing since clearPictures() (the loading screen, the HUD).
    using Picture = idtech3::Picture;
    const std::vector<Picture> &pictures() const { return _pictures; }
    void clearPictures() { _pictures.clear(); }
    // The weapon the client game last selected (SetUserCmdValue), which
    // the player's next commands carry.
    int32_t weaponSelect() const { return _weaponSelect; }
    // A command the player gave (numbered from 1), for the client game's
    // prediction: GetCurrentCmdNumber, GetUserCmd (the last 64 kept).
    void addUserCommand(const GameHost::UserCommand &cmd) {
        _userCommands[++_userCommandNumber] = cmd;
        while (_userCommands.size() > 64)
            _userCommands.erase(_userCommands.begin());
    }
    // A frame's 3D scene, as the client game hands it over (scene.h).
    using SceneEntity = idtech3::SceneEntity;
    using View = idtech3::View;
    using Scene = idtech3::Scene;
    const Scene &scene() const { return _scene; }
    void clearScene() { _scene = Scene{}; }
    // Where the client game asked for each snapshot (GetSnapshot's
    // number and buffer), in order.
    const std::vector<std::pair<int32_t, int32_t>> &snapshotRequests() const { return _snapshotRequests; }
    // A command typed or bound to a key that the client game may handle
    // itself (CG_CONSOLE_COMMAND: "weapnext", "weapon 3"...): whether it did.
    Error consoleCommand(const std::string &text, bool &handled);

    Qvm &vm() { return _vm; }
    // The layout its code was built with (1.0 or patch 1.2's), once loaded.
    const GameLayout &layout() const { return _layout; }
    // What it registered, by handle.
    const HandleTable &models() const { return _models; }
    const HandleTable &skins() const { return _skins; }
    const HandleTable &shaders() const { return _shaders; }
    const HandleTable &sounds() const { return _sounds; }
    // The console commands it added (AddCommand).
    const std::vector<std::string> &commands() const { return _commands; }
    // What it asked to send the server as the player's (SendClientCommand:
    // "score" while the scores are shown...) and to run at the console
    // (SendConsoleCommand), since last taken, in order.
    std::vector<std::string> takeClientCommands() { return std::exchange(_clientCommands, {}); }
    std::vector<std::string> takeConsoleCommands() { return std::exchange(_consoleCommands, {}); }
    // The map it asked for, for collision (CM_LoadMap) and drawing
    // (R_LoadWorldMap), and the music (S_StartBackgroundTrack: intro, loop).
    const std::string &collisionMap() const { return _collisionMap; }
    const std::string &worldMap() const { return _worldMap; }
    const std::pair<std::string, std::string> &music() const { return _music; }
    // The loaded collision map, if CM_LoadMap found it.
    std::shared_ptr<const Bsp> map() const { return _map; }

    // Where its sounds go (S_StartSound...); none if not set.
    SoundSystem *soundSystem = nullptr;
    float subdivisions = 4; // r_subdivisions, for marks on curves to lie on them as drawn

    std::function<void(const std::string &)> print;
    // UpdateScreen: while it loads, the client game asks for the screen to
    // be shown as it stands (its loading information); unset, nothing is.
    std::function<void()> updateScreen;
    const std::string &log() const { return _log; }
    const std::map<int, int> &unhandled() const { return _unhandled; }
    // The arguments of the first call to each call not implemented.
    const std::map<int, std::array<int32_t, 6>> &firstUnhandled() const { return _firstUnhandled; }
    std::function<void(int number, const int32_t *args)> trace;

private:
    int32_t systemCall(Qvm &vm, int number, const int32_t *args);
    void writeGameState(int32_t address);
    void writeGlconfig(int32_t address);

    const SearchSet &_files;
    CvarStore &_cvars;
    Qvm _vm;
    GameLayout _layout = kLayout10;
    VmFiles _vmFiles;
    std::map<int32_t, std::string> _configstrings;
    HandleTable _models;
    HandleTable _skins;
    HandleTable _shaders;
    HandleTable _sounds;
    std::vector<std::string> _commands;
    std::string _collisionMap;
    std::string _worldMap;
    std::pair<std::string, std::string> _music;
    std::shared_ptr<const Bsp> _map;
    std::string _log;
    std::map<int, int> _unhandled;
    std::vector<std::string> _clientCommands;
    std::vector<std::string> _consoleCommands;
    std::map<int, std::array<int32_t, 6>> _firstUnhandled;
    int32_t _time = 0;
    int32_t _snapshotNumber = 0;
    int32_t _snapshotTime = 0;
    std::map<int32_t, GameHost::Snapshot> _snapshots; // by number
    std::vector<std::string> _serverCommands;
    std::string _bigConfigstring;   // a long configstring coming in pieces (bcs0, bcs1, bcs2)
    std::vector<std::string> _args; // the command being handled, split as Quake III's Cmd_TokenizeString does
    std::vector<Picture> _pictures;
    std::array<float, 4> _color{1, 1, 1, 1};
    std::vector<std::pair<int32_t, int32_t>> _snapshotRequests;
    std::map<int32_t, GameHost::UserCommand> _userCommands;
    int32_t _userCommandNumber = 0;
    Scene _scene;
    std::map<int32_t, int32_t> _cleared;                      // memset(address, 0, size): the last size by address
    std::map<int32_t, std::shared_ptr<const Md3Model>> _md3s; // by model handle, for tags
    std::array<float, 3> _tempBoxMins{};
    std::array<float, 3> _tempBoxMaxs{};
    std::unique_ptr<CollisionWorld> _world;
    std::unique_ptr<MarkSurfaces> _marks; // made from map_ on the first mark
    int32_t _weaponSelect = 0;
};

} // namespace reluva::idtech3::eliteforce
