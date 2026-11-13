// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/client/scene.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/vm/vm_services.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {

// Elite Force's single-player menus (efuix86.dll), run in the x86
// interpreter in a process of their own, with the engine's side of their
// interface (docs/engines/idtech3/games/stvef/systems/sp-interface.md, "The menus"):
// the 59 functions Init is handed, and calls into the 12 GetUIAPI hands
// back. Drawing is recorded, as the client game's, for the page to show.
class SpUiHost {
public:
    static constexpr int32_t kApiVersion = 2;
    static constexpr uint32_t kMemory = 64u << 20;
    // The key catcher's bit for the menus (the engine's KEYCATCH_UI): while
    // it's set, keys go to them rather than to the game.
    static constexpr int32_t kCatchUi = 2;

    SpUiHost(const SearchSet &files, CvarStore &cvars);
    ~SpUiHost();

    // Loads the DLL (efuix86.dll's bytes), runs GetUIAPI and Init.
    Error load(const std::vector<uint8_t> &dll);

    // The engine's calls into the menus.
    Error keyEvent(int32_t key); // a press (key | 1024: a typed character)
    Error mouseEvent(int32_t dx, int32_t dy);
    // Draws them (pictures(), scene()) at `time`, the real time in ms.
    Error refresh(int32_t time);
    // Whether they cover the whole screen (the game isn't drawn under).
    bool fullscreen();
    // The loading screen (export 8) as a level loads, into pictures(): the
    // capture if there is one (a saved game's picture), else the menus'
    // own, with `server` and `info` as the original's engine gives them.
    Error drawLoadingScreen(const std::string &server, const std::string &info);
    // The connect text alone (export 9), over the client game's loading
    // screen as it loads; into pictures().
    Error drawConnectText(const std::string &server, const std::string &info);
    // Opens a menu ("main", "ingame", "turbolift"), or closes them ("").
    Error setActiveMenu(const std::string &name);
    // A console command for them; `handled` gets whether they knew it.
    Error consoleCommand(const std::string &text, bool *handled = nullptr);

    // --- Saved games ----------------------------------------------------------
    static constexpr uint32_t kShotSize = 256 * 256 * 4;
    // A saved game's file by name (no folder or extension); false if
    // there's none.
    std::function<bool(const std::string &name, std::vector<uint8_t> &file)> readSave;
    // Where saved games are written, listed with the game's own.
    std::filesystem::path saveDir;
    // Whether a saved game (by name) is listed: those the game running
    // can't read (another version's) are left out. All when unset.
    std::function<bool(const std::string &name)> listSave;
    // The comment the save menu set for the next save (its description).
    std::string saveComment;
    // The picture of the game for the next save, or of the saved game
    // loading (256 x 256 RGBA, rows bottom first as the original's screen
    // captures; the menus draw them upside down); empty: none.
    void setCapture(const std::vector<uint8_t> &rgba);
    std::vector<uint8_t> capture() const;

    // Who has the keys (kCatchUi while a menu is open), as they set it.
    int32_t catcher() const { return _catcher; }
    void setCatcher(int32_t c) { _catcher = c; }

    // The last refresh's drawing, and what it was drawn with.
    const std::vector<Picture> &pictures() const { return _pictures; }
    const Scene &scene() const { return _scene; }
    // Raw images drawn (DrawStretchRaw: a saved game's picture): where in
    // the 640 x 480 screen, their size and RGBA pixels.
    struct RawImage {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
        int cols = 0;
        int rows = 0;
        std::vector<uint8_t> rgba;
    };
    const std::vector<RawImage> &rawImages() const { return _raw; }
    const HandleTable &shaders() const { return _shaders; }
    const HandleTable &models() const { return _models; }
    const HandleTable &skins() const { return _skins; }
    const HandleTable &sounds() const { return _sounds; }

    // Commands the menus ran (Cmd_ExecuteText): "map borg1", "disconnect",
    // "quit"... in order; taken (and cleared) by the caller.
    std::vector<std::string> takeCommands() { return std::move(_commands); }

    // --- What the engine provides ------------------------------------------
    // What the menus print (Printf).
    std::function<void(const std::string &)> print;
    // The clock the game's code reads (milliseconds: GetTickCount,
    // timeGetTime, the engine's Milliseconds, Init's start-up time). Unset,
    // the real one; set before load() for runs that repeat exactly (the
    // engine tests).
    std::function<uint32_t()> clock;
    // A sound to play now, not placed in the world (S_StartLocalSound).
    std::function<void(const std::string &sound, int channel)> startLocalSound;
    // Configstrings of the game running (GetConfigString): false if none.
    std::function<bool(int32_t index, std::string &out)> configString;
    // Engine function 42 (flag): the running game's GameAllowedToSaveHere
    // with that flag; unset while there's no game (the flag comes back).
    std::function<uint32_t(uint32_t flag)> askGame;
    // What keys do (the Controls menu); none: no bindings.
    KeyBindings *bindings = nullptr;
    // The install whose mods the Mods menu lists ("$modlist": listMods),
    // and its base game's folder. None listed without it.
    std::filesystem::path modInstall;
    std::string modBaseDir;
    // Whether a shader or picture can be found (RegisterShader gives 0 for
    // one that can't); unset: all can.
    std::function<bool(const std::string &name)> shaderExists;
    // Keys held now (IsDown).
    std::set<int32_t> keysDown;
    // The connection state they're told (1: no game, 7: in one).
    int32_t connectionState = 1;

    // Calls they made that aren't answered yet, by import slot.
    const std::map<int, int> &unhandled() const { return _unhandled; }
    x86::Cpu &cpu() { return *_cpu; }

private:
    Error callUi(int index, const std::vector<uint32_t> &args, uint32_t *result = nullptr);
    void import(int slot, const std::function<uint32_t(x86::Cpu &)> &fn);
    void addImports();
    uint32_t text(const std::string &s);
    std::string str(x86::Cpu &c, int n, size_t max = 1024) const { return c.string(c.arg(n), max); }
    float argFloat(x86::Cpu &c, int n) const;
    Vec3f readVec(uint32_t address) const;
    const Md3Model *md3(int32_t handle);
    uint32_t fileList(const std::string &folder, const std::string &extension, uint32_t buffer, uint32_t size);

    const SearchSet &_files;
    CvarStore &_cvars;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    const x86::Win32Process::Module *_module = nullptr;
    uint32_t _imports = 0;
    uint32_t _exports = 0;
    uint32_t _scratch = 0;
    uint32_t _scratch2 = 0;
    uint32_t _comment = 0;
    uint32_t _capture = 0;
    bool _captured = false;
    std::vector<std::string> _args;
    std::vector<std::string> _commands;
    int32_t _catcher = 0;
    bool _overstrike = false;
    int32_t _time = 0;
    HandleTable _shaders;
    HandleTable _models;
    HandleTable _skins;
    HandleTable _sounds;
    std::vector<Picture> _pictures;
    std::vector<RawImage> _raw;
    Scene _scene;
    std::array<float, 4> _color{1, 1, 1, 1};
    std::map<int32_t, std::shared_ptr<Md3Model>> _md3s; // by model handle, for tags
    GameFiles _openFiles;
    std::map<int, int> _unhandled;
};

} // namespace reluva::idtech3::eliteforce
