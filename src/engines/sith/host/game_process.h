// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "emulation/x86/cdaudio.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/directx/dsound.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/win32.h"
#include "engines/sith/folders.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * The retail JK.EXE run in the x86 emulator
 * (docs/engines/sith/games/jk/systems/hosting.md): the install as drive C,
 * what the game writes going to a save folder, CD 1's copy as drive D, and
 * SMACKW32.DLL as it shipped. Windows, DirectDraw, DirectInput, DirectPlay,
 * DirectSound and the CD's audio are the emulator's stand-ins; there's no
 * joystick yet, and the game finds none.
 */
class GameProcess {
public:
    /**
     * Sets up the process and its stand-ins; nothing is loaded until load().
     * @param folders The install and its CDs (findFolders).
     * @param saveFolder Where what the game writes goes, and is read back from
 *        first; the settings it keeps in the registry are kept there too
 *        (registry.txt).
     * @param commandLine Its arguments after "JK.EXE", if any.
     */
    GameProcess(const GameFolders &folders, const std::filesystem::path &saveFolder,
                const std::string &commandLine = {});
    ~GameProcess();

    /**
     * Loads SMACKW32.DLL and JK.EXE from the install, and runs the DLL's
     * start-up. Where JK.EXE is the US release's, its CD check puts the CD
     * it asks for in drive D (insertCd).
     */
    Error load();
    /** Puts CD `number` (1 or 2) in drive D, if it was found; false if it wasn't. */
    bool insertCd(int number);
    /** The CD in drive D: 1 or 2, 0 if none. */
    int cdInDrive() const { return _cdInDrive; }
    /**
     * Runs the program from its entry point, on threads of this machine,
     * until it ends or is stopped (Cpu::requestStop, Win32Process::requestExit).
     */
    Error run(uint32_t &exitCode);

    /** The interpreter, the Windows process and the stand-ins it runs with. */
    ///@{
    x86::Cpu &cpu() { return *_cpu; }
    x86::Win32Process &process() { return *_process; }
    x86::DirectX &directX() { return *_directX; }
    x86::DirectInput &directInput() { return *_directInput; }
    x86::DirectSound &directSound() { return *_directSound; }
    x86::CdAudio &cdAudio() { return *_cdAudio; }
    ///@}
    /**
     * Whether the game shows Windows' pointer, as its menus do: it hasn't
     * hidden it, and doesn't hold the mouse through DirectInput, as it does
     * in play.
     */
    bool pointerShown() const { return _process->cursorShown() && !_directInput->mouseHeld(); }

private:
    GameFolders _folders;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::unique_ptr<x86::DirectX> _directX;
    std::unique_ptr<x86::DirectInput> _directInput;
    std::unique_ptr<x86::DirectPlay> _directPlay;
    std::unique_ptr<x86::DirectSound> _directSound;
    std::unique_ptr<x86::CdAudio> _cdAudio;
    const x86::Win32Process::Module *_exe = nullptr;
    int _cdInDrive = 0;
    void hookCdCheck();
    // The game's registry key, kept in the save folder between runs: the
    // file, and the names of the values in it.
    std::filesystem::path _registryFile;
    std::vector<std::string> _registryNames;
    void loadRegistry();
    void saveRegistry();
};

} // namespace reluva::sith
