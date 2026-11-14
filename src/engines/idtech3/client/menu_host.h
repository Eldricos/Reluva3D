// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/client/scene.h"
#include "engines/idtech3/client/server_browser.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/vm/vm_services.h"

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// A game's menus (its vm/ui.qvm) run with the engine side of its system
// calls, as MenuLayer drives them: each game numbers its calls its own
// way (eliteforce::UiHost, jk2::UiHost), but they're called and drawn
// alike. Drawing is recorded, not done: each frame's pictures (and 3D
// scenes) are collected for whatever shows them.
class MenuHost {
public:
    // vmMain's commands, in Quake III's order, which Elite Force's and
    // Jedi Outcast's menus both keep.
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

    virtual ~MenuHost() = default;

    // Reads vm/ui.qvm and checks its API version.
    virtual Error load() = 0;
    // vmMain(command, args...).
    virtual Error call(int32_t command, const std::vector<int32_t> &args, int32_t &result) = 0;
    Error call(int32_t command, const std::vector<int32_t> &args = {}) {
        int32_t ignored;
        return call(command, args, ignored);
    }
    // One frame of the menus at `time` (ms), the way the original engine
    // runs them while not connected to a game: if no menu covers the
    // screen, the main menu opens; then they draw (pictures()).
    virtual Error refresh(int32_t time) = 0;

    // The frame the pictures are for, in pixels.
    virtual void setScreenSize(int width, int height) = 0;
    // Where a CD key the menus take is kept, for a game that asks for one.
    virtual void setKeyFile(const std::filesystem::path & /*path*/) {}
    // The install whose mods the menus list, and its base game's folder.
    virtual void setModRoot(const std::filesystem::path &install, const std::string &baseDir) = 0;

    virtual const std::vector<Picture> &pictures() const = 0;
    // The 3D scenes drawn among them, each over the pictures before it.
    virtual const Scene &scene() const = 0;
    // Forgets the last frame's pictures and scenes, before the next.
    virtual void clearPictures() = 0;
    virtual const HandleTable &shaders() const = 0;
    // The models and skins its scenes name (none for menus without scenes).
    virtual const HandleTable &models() const = 0;
    virtual const HandleTable &sounds() const = 0;
    virtual const HandleTable &skins() const = 0;
    // Calls not implemented yet: number -> how many times.
    virtual const std::map<int, int> &unhandled() const = 0;
    // Whether a picture with this shader is the menus' mouse pointer.
    virtual bool isCursor(const std::string &shader) const = 0;
    // Text the menus print, unless print is set.
    virtual const std::string &log() const = 0;
    // Commands the menus run (Cmd_ExecuteText), in order.
    virtual const std::vector<std::string> &commands() const = 0;
    // The last system calls made, for reports when the menus stop.
    virtual std::vector<std::string> recentCalls() const = 0;
    // Their key catcher (KEYCATCH_UI, 2, while they take the keys).
    virtual int32_t catcher() const = 0;
    // The program itself, for tools that look inside it.
    virtual Qvm &vm() = 0;

    std::function<void(const std::string &)> print;
    // A registered sound to play now, not placed in the world, on a channel.
    std::function<void(const std::string &sound, int channel)> startLocalSound;
    // Music to play, `intro` once and then `loop` (both empty: stop), for
    // menus that ask for it with a system call.
    std::function<void(const std::string &intro, const std::string &loop)> backgroundTrack;
    // What keys do, which the Controls menu shows and changes.
    KeyBindings *bindings = nullptr;
    // Finding servers for the server list; none: the lists stay empty.
    ServerBrowser *browser = nullptr;
    std::function<bool(const std::string &host, uint32_t &ip)> resolveHost;
    uint64_t now = 0; // the browser's clock, in ms
    // Called by every system call before it's handled, for logging.
    std::function<void(int number, const int32_t *args)> trace;
    // What GetClientState reports (uiClientState_t): connState as Quake
    // III's (1 disconnected ... 8 active), how many connect requests were
    // sent, the player's number, the server's name, the messages to show.
    struct ClientState {
        int32_t connState = 1;
        int32_t connectPacketCount = 0;
        int32_t clientNum = 0;
        std::string serverName;
        std::string updateInfo;
        std::string message;
    };
    ClientState clientState;
    // The configstrings GetConfigString reads (a match's); none if unset.
    std::function<std::string(int32_t index)> configstring;
};

} // namespace reluva::idtech3
