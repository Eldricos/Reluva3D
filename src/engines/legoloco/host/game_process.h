// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace reluva::legoloco {

/**
 * LEGO Loco's own program, loco.exe, run in the x86 interpreter with
 * Windows and DirectX stood in for
 * (docs/engines/legoloco/systems/running.md). Its threads (the multimedia
 * timer's among them) run on host threads of their own.
 */
class GameProcess {
public:
    /**
     * Sets up the process for a copy at `gameDir` (the folder holding Exe
     * and art-res), which the program sees as drive C:, and what it writes
     * kept in `saveDir` (GameTarget::savePath), read back from there first.
     * Nothing is loaded until load().
     */
    GameProcess(std::filesystem::path gameDir, std::filesystem::path saveDir, uint32_t memoryMb = 256);

    /**
     * Writes the save folder's Exe/LEGO.INI, the game's own with its
     * folders made C:'s, and loads Exe/Loco.exe.
     */
    Error load();
    /**
     * Runs the program until it ends the process.
     * @param exitCode Set to the code it exited with.
     */
    Error run(uint32_t &exitCode);

    /** The interpreter, the Windows process and DirectX's stand-in. */
    ///@{
    x86::Cpu &cpu() { return *_cpu; }
    x86::Win32Process &process() { return *_process; }
    x86::DirectX &directX() { return *_directX; }
    ///@}

private:
    Error writeIni();

    std::filesystem::path _dir;
    std::filesystem::path _saveDir;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::unique_ptr<x86::DirectX> _directX;
    std::unique_ptr<x86::DirectPlay> _directPlay;
    const x86::Win32Process::Module *_exe = nullptr;
};

} // namespace reluva::legoloco
