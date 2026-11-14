// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/client/menu_host.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/raven/fonts.h"
#include "engines/idtech3/raven/strings.h"
#include "engines/idtech3/vm/qvm.h"

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::jk2 {

// Jedi Outcast's multiplayer menus: vm/ui.qvm run with the engine side of
// its system calls, in Jedi Outcast's numbering, as worked out in
// docs/engines/idtech3/games/jk2/systems/syscalls.md. They read their
// screens from menu scripts through the engine's script reader, write
// with Raven's fonts and take their text from the string packages.
// Everything is drawn in 640x480, as the original's renderer draws 2D,
// and scaled to the frame.
class UiHost : public MenuHost {
public:
    static constexpr int32_t kApiVersion = 6; // what Jedi Outcast 1.02a's ui.qvm reports

    UiHost(const SearchSet &files, CvarStore &cvars);

    Error load() override;
    Error call(int32_t command, const std::vector<int32_t> &args, int32_t &result) override;
    using MenuHost::call;
    Error refresh(int32_t time) override;

    void setScreenSize(int width, int height) override {
        _width = width;
        _height = height;
    }
    void setModRoot(const std::filesystem::path &install, const std::string &baseDir) override {
        _vmFiles.setModRoot(install, baseDir);
    }
    // The clock Milliseconds reports.
    void setTime(int32_t ms) { _time = ms; }

    const std::vector<Picture> &pictures() const override { return _pictures; }
    const Scene &scene() const override { return _scene; }
    void clearPictures() override {
        _pictures.clear();
        _scene = Scene{};
    }
    const HandleTable &shaders() const override { return _shaders; }
    // Jedi Outcast's pointer: the assets' cursor, cursor*.
    bool isCursor(const std::string &shader) const override;
    const std::string &log() const override { return _log; }
    const std::vector<std::string> &commands() const override { return _commands; }
    std::vector<std::string> recentCalls() const override;
    int32_t catcher() const override { return _catcher; }

    Qvm &vm() override { return _vm; }
    const HandleTable &sounds() const override { return _sounds; }
    // Calls not implemented yet: number -> how many times.
    const std::map<int, int> &unhandled() const override { return _unhandled; }
    const HandleTable &models() const override { return _models; }
    const HandleTable &skins() const override { return _none; }

private:
    int32_t systemCall(Qvm &vm, int number, const int32_t *args);
    // The fonts' calls: a font registered by name, and text measured and
    // drawn with it.
    int32_t registerFont(const std::string &name);
    const raven::Font *font(int32_t handle) const;
    // Stops after maxChars letters and spaces (-1: none), as the
    // multiplayer engine counts them.
    void drawString(int32_t x, int32_t y, const std::string &text, const std::array<float, 4> &color, int32_t handle,
                    int32_t maxChars, float scale, bool shadow = false);
    // The script calls (PC_*): sources by handle, tokens written as
    // pc_token_t.
    int32_t readToken(int32_t handle, int32_t address);
    void addPicture(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int32_t shader,
                    const std::array<float, 4> &color);
    void writeGlconfig(int32_t address);
    void writeClientState(int32_t address);

    const SearchSet &_files;
    CvarStore &_cvars;
    Qvm _vm;
    VmFiles _vmFiles;
    HandleTable _shaders;
    HandleTable _models;
    HandleTable _sounds;
    std::vector<Picture> _pictures;
    Scene _scene;      // the main menu's turning logo
    HandleTable _none; // nor name models or skins
    raven::StringTable _strings;
    bool _stringsLoaded = false;
    struct LoadedFont {
        raven::Font font;
        int32_t shader = 0;
    };
    std::vector<LoadedFont> _fonts; // handle - 1
    std::map<std::string, int32_t> _fontByName;
    std::map<int32_t, std::pair<std::array<float, 3>, std::array<float, 3>>> _modelBounds; // by handle
    std::map<int32_t, std::unique_ptr<botlib::BotScript>> _sources;
    std::map<int32_t, botlib::BotToken> _lastToken; // each source's, for SourceFileAndLine
    int32_t _nextSource = 1;
    std::vector<std::string> _globalDefines;
    std::array<float, 4> _color{1, 1, 1, 1};
    std::vector<std::string> _commands;
    std::map<int, int> _unhandled;
    std::string _log;
    int _width = 640;
    int _height = 480;
    int32_t _time = 0;
    int32_t _catcher = 0;
    bool _overstrike = false;
    struct Recent {
        int number = -1;
        int32_t args[4] = {};
        std::string text;
    };
    std::array<Recent, 64> _recent{};
    size_t _recentNext = 0;
};

} // namespace reluva::idtech3::jk2
