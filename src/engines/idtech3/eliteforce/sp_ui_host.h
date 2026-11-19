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
#include "engines/idtech3/client/sp_menu_host.h"
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
class SpUiHost : public SpMenuHost {
public:
    static constexpr int32_t kApiVersion = 2;
    static constexpr uint32_t kMemory = 64u << 20;

    SpUiHost(const SearchSet &files, CvarStore &cvars);
    ~SpUiHost() override;

    // Loads the DLL (efuix86.dll's bytes), runs GetUIAPI and Init.
    Error load(const std::vector<uint8_t> &dll);

    // The engine's calls into the menus.
    Error keyEvent(int32_t key) override; // a press (key | 1024: a typed character)
    Error mouseEvent(int32_t dx, int32_t dy) override;
    // Draws them (pictures(), scene()) at `time`, the real time in ms.
    Error refresh(int32_t time) override;
    // Whether they cover the whole screen (the game isn't drawn under).
    bool fullscreen() override;
    // The loading screen (export 8) as a level loads, into pictures(): the
    // capture if there is one (a saved game's picture), else the menus'
    // own, with `server` and `info` as the original's engine gives them.
    Error drawLoadingScreen(const std::string &server, const std::string &info) override;
    // The connect text alone (export 9), over the client game's loading
    // screen as it loads; into pictures().
    Error drawConnectText(const std::string &server, const std::string &info) override;
    // Opens a menu ("main", "ingame", "turbolift"), or closes them ("").
    Error setActiveMenu(const std::string &name) override;
    // A console command for them; `handled` gets whether they knew it.
    Error consoleCommand(const std::string &text, bool *handled) override;

    // The capture is kept in the menus' memory, where they draw it from
    // (upside down).
    void setCapture(const std::vector<uint8_t> &rgba) override;
    std::vector<uint8_t> capture() const override;

    // --- What the engine provides, besides SpMenuHost's ----------------------
    // The clock the game's code reads (milliseconds: GetTickCount,
    // timeGetTime, the engine's Milliseconds, Init's start-up time). Unset,
    // the real one; set before load() for runs that repeat exactly (the
    // engine tests).
    std::function<uint32_t()> clock;
    // The install whose mods the Mods menu lists ("$modlist": listMods),
    // and its base game's folder. None listed without it.
    std::filesystem::path modInstall;
    std::string modBaseDir;

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
    bool _overstrike = false;
    int32_t _time = 0;
    std::array<float, 4> _color{1, 1, 1, 1};
    std::map<int32_t, std::shared_ptr<Md3Model>> _md3s; // by model handle, for tags
    GameFiles _openFiles;
    std::map<int, int> _unhandled;
};

} // namespace reluva::idtech3::eliteforce
