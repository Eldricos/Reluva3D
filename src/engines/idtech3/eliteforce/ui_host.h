// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/client/scene.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/client/server_browser.h"
#include "engines/idtech3/vm/vm_services.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's menus: vm/ui.qvm run with the engine side of its system
// calls, in Elite Force's numbering, as worked out in
// docs/engines/idtech3/games/stvef/systems/syscalls.md. Drawing is recorded, not
// done: each frame's pictures are collected for whatever shows them.
class UiHost {
public:
    // vmMain's commands (Quake III's order; GetApiVersion and Init are
    // confirmed on Elite Force's ui.qvm).
    enum Command : int32_t {
        kGetApiVersion = 0,
        kInit = 1,
        kShutdown = 2,
        kKeyEvent = 3,   // (key, down)
        kMouseEvent = 4, // (dx, dy)
        kRefresh = 5,    // (time in ms)
        kIsFullscreen = 6,
        kSetActiveMenu = 7, // (menu: 0 none, 1 main, ...)
        kConsoleCommand = 8,
        kDrawConnectScreen = 9,
    };
    static constexpr int32_t kApiVersion = 4; // what Elite Force's ui.qvm reports

    // One picture: a part (s1,t1)-(s2,t2) of a shader stretched over a
    // screen rectangle, tinted by the colour set before it.
    using Picture = idtech3::Picture;

    UiHost(const SearchSet &files, CvarStore &cvars);

    // Reads vm/ui.qvm and checks its API version.
    Error load();
    // vmMain(command, args...).
    Error call(int32_t command, const std::vector<int32_t> &args, int32_t &result);
    Error call(int32_t command, const std::vector<int32_t> &args = {}) {
        int32_t ignored;
        return call(command, args, ignored);
    }

    // One frame of the menus at `time` (ms), the way the original engine
    // runs them while not connected to a game: if no menu covers the
    // screen (one just closed itself, say), the main menu opens; then
    // they draw (pictures()).
    Error refresh(int32_t time);

    // What GetGlconfig reports, in pixels.
    void setScreenSize(int width, int height) {
        _width = width;
        _height = height;
    }
    // Where in glconfig_t Quake III's 17 words (maxTextureSize ...
    // vidWidth, vidHeight ...) start: 5124 in Elite Force, after a word of
    // its own (docs/engines/idtech3/games/stvef/systems/syscalls.md). For trying
    // other layouts.
    void setGlconfigWords(int32_t offset) { _glconfigWords = offset; }
    // Where the CD key the menus take (call 57) is kept; none: it's only
    // remembered while running.
    void setKeyFile(std::filesystem::path path) { _keyFile = std::move(path); }
    // The clock Milliseconds reports.
    void setTime(int32_t ms) { _time = ms; }

    Qvm &vm() { return _vm; }
    const std::vector<Picture> &pictures() const { return _pictures; }
    // The 3D scenes drawn among them (the player model in Player
    // settings...), each over the pictures before it.
    const Scene &scene() const { return _scene; }
    // Forgets the last frame's pictures and scenes, before the next.
    void clearPictures() {
        _pictures.clear();
        _scene = Scene{};
    }
    const HandleTable &shaders() const { return _shaders; }
    const HandleTable &models() const { return _models; }
    const HandleTable &skins() const { return _skins; }
    const HandleTable &sounds() const { return _sounds; }
    // Text the menus print; by default, kept in log().
    std::function<void(const std::string &)> print;
    const std::string &log() const { return _log; }
    // Commands the menus run (Cmd_ExecuteText), in order.
    const std::vector<std::string> &commands() const { return _commands; }
    // Calls not implemented yet: number -> how many times.
    const std::map<int, int> &unhandled() const { return _unhandled; }
    // The last system calls made (oldest first), with their first
    // arguments and any text they point at, for reports when the menus
    // stop. The CD key is left out.
    std::vector<std::string> recentCalls() const;
    // S_StartLocalSound: a registered sound to play now, not placed in the
    // world, on a channel (a new sound on the same channel, other than 0,
    // replaces the one there). Unset: sounds are only registered.
    std::function<void(const std::string &sound, int channel)> startLocalSound;
    // What keys do, which the Controls menu shows and changes; none if not set.
    KeyBindings *bindings = nullptr;
    // The install whose mods the Mods menu lists ("$modlist"), and its base
    // game's folder.
    void setModRoot(const std::filesystem::path &install, const std::string &baseDir) {
        _vmFiles.setModRoot(install, baseDir);
    }
    // Called by every system call before it's handled, for logging.
    std::function<void(int number, const int32_t *args)> trace;
    // Finding servers for the server list (calls 46 to 53, and the
    // "localservers" and "ping" commands, run as soon as the menus run
    // them); none: the lists stay empty. `resolveHost` turns a server's
    // name into its address. `now`: the browser's clock, in ms.
    ServerBrowser *browser = nullptr;
    std::function<bool(const std::string &host, uint32_t &ip)> resolveHost;
    uint64_t now = 0;
    // What GetClientState reports (uiClientState_t), for the connecting
    // screen: connState as Quake III's (1 disconnected, 3 connecting, 4
    // challenging, 5 connected, 6 loading, 7 primed, 8 active), how many
    // connect requests were sent, the player's number, the server's name,
    // and the messages to show.
    struct ClientState {
        int32_t connState = 1, connectPacketCount = 0, clientNum = 0;
        std::string serverName, updateInfo, message;
    };
    ClientState clientState;
    // The configstrings GetConfigString reads (a match's, for the in-game
    // menu); none if unset.
    std::function<std::string(int32_t index)> configstring;
    // Their key catcher (KEYCATCH_UI, 2, while they take the keys).
    int32_t catcher() const { return _catcher; }

private:
    int32_t systemCall(Qvm &vm, int number, const int32_t *args);
    const std::string &storedCdKey();
    static bool cdKeyLooksValid(const std::string &key);
    void writeGlconfig(int32_t address);
    void writeClientState(int32_t address);
    bool browserCommand(const std::string &line);

    const SearchSet &_files;
    CvarStore &_cvars;
    Qvm _vm;
    VmFiles _vmFiles;
    HandleTable _shaders, _models, _skins, _sounds;
    std::vector<Picture> _pictures;
    Scene _scene;
    std::map<int32_t, std::shared_ptr<const Md3Model>> _md3s; // by model handle, for tags
    std::array<float, 4> _color{1, 1, 1, 1};
    std::vector<std::string> _commands;
    std::map<int, int> _unhandled;
    std::string _log;
    int _width = 640, _height = 480;
    int32_t _time = 0;
    int32_t _glconfigWords = 5124;
    int32_t _catcher = 0;
    bool _overstrike = false;
    std::filesystem::path _keyFile;
    std::string _cdKey;
    bool _cdKeyRead = false;
    std::map<int32_t, std::pair<std::array<float, 3>, std::array<float, 3>>> _modelBounds; // by handle
    bool _mainMenuNotFullscreen = false; // IsFullscreen seems to mean something else
    struct Recent {
        int number = -1;
        int32_t args[4] = {};
        std::string text; // what args[0] or args[1] point at, if text
    };
    std::array<Recent, 64> _recent{};
    size_t _recentNext = 0;
};

} // namespace reluva::idtech3::eliteforce
