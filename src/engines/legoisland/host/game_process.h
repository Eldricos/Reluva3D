// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/directx/d3drm.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/directx/dsound.h"
#include "emulation/x86/win32.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::legoisland {

/**
 * LEGO Island's own code (ISLE.EXE and LEGO1.DLL) run in the x86
 * interpreter, as docs/engines/legoisland/systems/running.md describes:
 * the DLL loaded, then the program, the DLL's start-up run, then the
 * program's. What they ask of Windows is stood in for by Win32Process and
 * the shared DirectX stand-ins; what nothing stands in for yet is listed
 * by missingImports() and fails the run when called.
 */
class GameProcess {
public:
    /** The folder the installer puts the game in, by its long name and by the short one it writes to the registry. */
    ///@{
    static constexpr const char *kInstallFolder = "C:\\Program Files\\LEGO Island\\";
    static constexpr const char *kInstallFolderShort = "C:\\PROGRA~1\\LEGOIS~1\\";
    ///@}

    /**
     * Sets up the process: its files (the install folder and the CD's
     * drive both the game folder, which an import fills with both), the
     * registry as the installer leaves it, and the DirectDraw,
     * DirectSound and DirectInput stand-ins. Nothing is loaded until
     * load().
     */
    GameProcess(std::filesystem::path gameDir, uint32_t memoryMb = 512);

    /**
     * Where what the program writes in its install folder goes, and is
     * read back from first. Without one, writing isn't allowed.
     */
    void setSaveFolder(std::filesystem::path dir) { _saveDir = std::move(dir); }
    /**
     * One of the settings the program reads from the registry, as
     * `CONFIG.EXE` writes them (systems/running.md): "YES" or "NO" for a
     * yes or no, a number as text. Before load().
     */
    void setSetting(const std::string &name, const std::string &value);

    /**
     * Whether the player turns by the time a frame took, at the speed it
     * turns at 20 frames a second, rather than a set amount each frame, as
     * the original does (systems/running.md); on unless set. Before load().
     */
    void setTurningByTime(bool on) { _turningByTime = on; }

    /** Reads and loads LEGO1.DLL, then ISLE.EXE, from the game folder. */
    Error load();
    /** Runs LEGO1.DLL's start-up. */
    Error attach();
    /**
     * Runs the program from its entry point, returning when it stops.
     * @param exitCode Set to the code it exited with.
     */
    Error run(uint32_t &exitCode);

    /** The interpreter, the Windows process, and the DirectDraw and DirectSound stand-ins. */
    ///@{
    x86::Cpu &cpu() { return *_cpu; }
    x86::Win32Process &process() { return *_process; }
    x86::DirectX &directX() { return *_directX; }
    x86::DirectSound &directSound() { return *_directSound; }
    ///@}

private:
    std::optional<std::filesystem::path> mapPath(const std::string &windowsPath, bool forWriting) const;
    void turnByTime();

    std::filesystem::path _dir;
    std::filesystem::path _saveDir;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::unique_ptr<x86::DirectX> _directX;
    std::unique_ptr<x86::DirectSound> _directSound;
    std::unique_ptr<x86::Direct3DRM> _retainedMode;
    std::unique_ptr<x86::DirectInput> _directInput;
    const x86::Win32Process::Module *_dll = nullptr;
    const x86::Win32Process::Module *_exe = nullptr;
    bool _turningByTime = true;
};

} // namespace reluva::legoisland
