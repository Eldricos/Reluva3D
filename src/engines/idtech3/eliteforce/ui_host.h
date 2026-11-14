// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/idtech3/client/menu_host.h"
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
class UiHost : public MenuHost {
public:
    static constexpr int32_t kApiVersion = 4; // what Elite Force's ui.qvm reports

    // One picture: a part (s1,t1)-(s2,t2) of a shader stretched over a
    // screen rectangle, tinted by the colour set before it.
    using Picture = idtech3::Picture;

    UiHost(const SearchSet &files, CvarStore &cvars);

    Error load() override;
    Error call(int32_t command, const std::vector<int32_t> &args, int32_t &result) override;
    using MenuHost::call;

    // One frame of the menus at `time` (ms), the way the original engine
    // runs them while not connected to a game: if no menu covers the
    // screen (one just closed itself, say), the main menu opens; then
    // they draw (pictures()).
    Error refresh(int32_t time) override;

    // What GetGlconfig reports, in pixels.
    void setScreenSize(int width, int height) override {
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
    void setKeyFile(const std::filesystem::path &path) override { _keyFile = path; }
    // The clock Milliseconds reports.
    void setTime(int32_t ms) { _time = ms; }

    Qvm &vm() override { return _vm; }
    const std::vector<Picture> &pictures() const override { return _pictures; }
    // The 3D scenes drawn among them (the player model in Player
    // settings...), each over the pictures before it.
    const Scene &scene() const override { return _scene; }
    // Forgets the last frame's pictures and scenes, before the next.
    void clearPictures() override {
        _pictures.clear();
        _scene = Scene{};
    }
    const HandleTable &shaders() const override { return _shaders; }
    // Elite Force's pointer: menu/common/cursor*.
    bool isCursor(const std::string &shader) const override;
    const HandleTable &models() const override { return _models; }
    const HandleTable &skins() const override { return _skins; }
    const HandleTable &sounds() const override { return _sounds; }
    const std::string &log() const override { return _log; }
    // Commands the menus run (Cmd_ExecuteText), in order.
    const std::vector<std::string> &commands() const override { return _commands; }
    // Calls not implemented yet: number -> how many times.
    const std::map<int, int> &unhandled() const override { return _unhandled; }
    // The last system calls made (oldest first), with their first
    // arguments and any text they point at, for reports when the menus
    // stop. The CD key is left out.
    std::vector<std::string> recentCalls() const override;
    // The install whose mods the Mods menu lists ("$modlist"), and its base
    // game's folder.
    void setModRoot(const std::filesystem::path &install, const std::string &baseDir) override {
        _vmFiles.setModRoot(install, baseDir);
    }
    // The server list's calls are 46 to 53 (the "localservers" and "ping"
    // commands run as soon as the menus run them).
    int32_t catcher() const override { return _catcher; }

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
    HandleTable _shaders;
    HandleTable _models;
    HandleTable _skins;
    HandleTable _sounds;
    std::vector<Picture> _pictures;
    Scene _scene;
    std::map<int32_t, std::shared_ptr<const Md3Model>> _md3s; // by model handle, for tags
    std::array<float, 4> _color{1, 1, 1, 1};
    std::vector<std::string> _commands;
    std::map<int, int> _unhandled;
    std::string _log;
    int _width = 640;
    int _height = 480;
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
