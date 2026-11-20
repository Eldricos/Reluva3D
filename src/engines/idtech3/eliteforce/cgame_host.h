// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/formats/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/botlib/script_calls.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/jk2/font_set.h"
#include "engines/idtech3/raven/effects.h"
#include "engines/idtech3/raven/roff.h"
#include "engines/idtech3/raven/strings.h"
#include "engines/idtech3/render/light_grid.h"
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
        // Jedi Outcast's: the server is about to change levels (its client
        // game shows "Server changing maps").
        kJk2MapChange = 24,
    };

    // `game`: whose client game it runs, Elite Force's or Jedi Outcast's
    // multiplayer one.
    // `release`: Jedi Outcast's, whose engines number the calls and lay out
    // the player state differently.
    CgameHost(const SearchSet &files, CvarStore &cvars, GameCode game = GameCode::kEliteForce,
              JediRelease release = JediRelease::k102a);

    // Reads vm/cgame.qvm.
    Error load();
    // The configstrings the server has set, as the client receives them
    // (its game state).
    void setConfigstrings(std::map<int32_t, std::string> configstrings) { _configstrings = std::move(configstrings); }
    // The level's light grid, which R_LightForPoint samples (Jedi
    // Outcast's); null: no light anywhere.
    void setLightGrid(const LightGrid *grid) { _lightGrid = grid; }
    // R_RemapShader's since the last call, in order, for the renderer.
    struct ShaderRemap {
        std::string from;
        std::string to;
        float timeOffset = 0; // seconds
    };
    std::vector<ShaderRemap> takeShaderRemaps() { return std::exchange(_shaderRemaps, {}); }
    // Jedi Outcast's limits on the player's view yaw for the next command
    // (SetClientForceAngle, SetClientTurnExtent): each applies once, if its
    // time is still ahead of the command's (see applyViewLimits).
    struct ViewLimits {
        int32_t forceTime = 0;
        float forceYaw = 0;
        float turnAdd = 0;
        float turnSub = 0;
        int32_t turnTime = 0;
    };
    ViewLimits &viewLimits() { return _viewLimits; }
    // The newest snapshot's player state (empty before the first).
    const std::vector<uint8_t> &latestPlayerState() const;
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
    // Jedi Outcast's: the force power and inventory item selected, which
    // its commands carry too.
    int32_t forceSelect() const { return _forceSelect; }
    int32_t inventorySelect() const { return _inventorySelect; }
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
    // The menus it asked to open (Jedi Outcast's OpenUIMenu: its force
    // powers, its team choice...), since last taken.
    std::vector<int32_t> takeUiMenus() { return std::exchange(_uiMenus, {}); }
    // Its key catcher (Jedi Outcast's sets it while a menu of its own
    // takes the keys).
    int32_t catcher() const { return _catcher; }
    // Jedi Outcast's Ghoul2 instances (its players, weapons, sabers), by the
    // numbers its entities hold, to draw.
    jk2::Ghoul2Instances &ghoul2() { return _ghoul2; }
    // Raven's light styles' colours, as Jedi Outcast's client game sets
    // them each frame (full white until it does).
    const LightStyleColors &lightStyles() const { return _lightStyles; }
    // The ROFF system shared with the game module when both run here, as
    // the original's one system: the numbers the server's events carry are
    // the ones it cached.
    void shareRoffs(std::shared_ptr<raven::RoffSystem> roffs) { _roffs = std::move(roffs); }
    // Jedi Outcast's effects, as its effects calls run them.
    const raven::EffectSystem &effects() const { return _fx; }
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
    // Jedi Outcast's: the Elite Force number of one of its calls the two
    // share (-1 for one of its own), and its own calls.
    static int32_t jk2SharedCall(int32_t number);
    int32_t jk2Call(Qvm &vm, int number, const int32_t *args);
    // Its effects calls (jk2/cgame_effects.cpp): what the effects system asks
    // of the client game and the world, and what it draws put in the scene.
    int32_t jk2EffectCall(Qvm &vm, int number, const int32_t *args);
    int32_t jk2RoffCall(Qvm &vm, int number, const int32_t *args);
    raven::RoffSystem::Side roffSide();
    raven::EffectSystem::Host effectHost();
    void addEffects(const raven::EffectSystem::Frame &frame);
    void writeGameState(int32_t address);
    void writeGlconfig(int32_t address);

    const SearchSet &_files;
    CvarStore &_cvars;
    GameCode _game;
    JediRelease _release;
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
    const LightGrid *_lightGrid = nullptr;
    std::vector<ShaderRemap> _shaderRemaps;
    ViewLimits _viewLimits;
    // The last RenderScene's forward axis (its refdef's at 36), as the
    // original's renderer keeps its last view.
    std::array<float, 3> _lastViewForward{1, 0, 0};
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
    std::unique_ptr<MarkSurfaces> _marks; // made from _map on the first mark
    int32_t _weaponSelect = 0;
    int32_t _forceSelect = 0;
    int32_t _inventorySelect = 0;
    // Jedi Outcast's own calls' state: its text and menu scripts, its
    // string packages, its Ghoul2 instances (by the number it holds), its
    // effects.
    jk2::FontSet _fonts{_files, _shaders};
    botlib::ScriptCalls _scripts{_files};
    raven::StringTable _strings;
    bool _stringsLoaded = false;
    std::vector<int32_t> _uiMenus;
    int32_t _catcher = 0;
    size_t _entityTokenPos = 0;
    int32_t _sharedBuffer = 0;
    int32_t _sharedBufferSize = 0;
    jk2::Ghoul2Instances _ghoul2{_files};
    raven::EffectSystem _fx{_files, effectHost()};
    LightStyleColors _lightStyles = fullLightStyles();
    std::shared_ptr<raven::RoffSystem> _roffs = std::make_shared<raven::RoffSystem>(_files);
};

} // namespace reluva::idtech3::eliteforce
