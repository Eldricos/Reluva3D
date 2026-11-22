// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace reluva::sfc {

/**
 * Starfleet Command II's own StarFleet2.exe, in the x86 emulator
 * (docs/engines/sfc/games/sfc2/systems/hosting.md): the install's folder
 * as the program's, the shared stand-ins for Windows, DirectDraw and
 * DirectInput, and those of the engine's own.
 */
class GameProcess {
public:
    /** The program's own folder, as the program sees it. */
    static constexpr const char *kInstallFolder = "C:\\SFC2\\";
    static constexpr const char *kProgram = "StarFleet2.exe";

    /** `install` is the game's folder; nothing in it is written to. */
    GameProcess(std::filesystem::path install, uint32_t memoryMb);

    /** Reads and loads StarFleet2.exe, its imports bound. */
    Error load();
    /** Runs it until it ends, faults, or calls what nothing stands in for. */
    Error run(uint32_t &exitCode);

    /** The processor, the process and its DirectX, for the host's callbacks and checks. */
    ///@{
    x86::Cpu &cpu() { return *_cpu; }
    x86::Win32Process &process() { return *_process; }
    x86::DirectX &directX() { return *_directX; }
    ///@}

    /** Told of each file or folder the program looks for in its folder, and whether it was there. */
    std::function<void(const std::string &windowsPath, bool found)> fileOpened;
    /** Told of each INI setting the program reads: the file, [section] and key. */
    std::function<void(const std::string &what)> iniReads;

private:
    /** The Windows functions the shared stand-ins don't have (system.cpp). */
    void addSystem();
    /** Stands in for a function taking `args` 32-bit arguments, popping them; `fn` gives EAX. */
    void stdcall(const char *dll, const char *name, int args, std::function<uint32_t(x86::Cpu &)> fn);

    std::filesystem::path _dir;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::unique_ptr<x86::DirectX> _directX;
    std::unique_ptr<x86::DirectInput> _directInput;
    const x86::Win32Process::Module *_exe = nullptr;
    uint32_t _focus = 0; // the window with the focus
};

} // namespace reluva::sfc
