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
#include "emulation/x86/joystick.h"
#include "emulation/x86/win32.h"
#include "engines/sith/folders.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * A game's retail program (JK.EXE, JKM.EXE: GameFolders::game) run in the
 * x86 emulator (docs/engines/sith/games/jk/systems/hosting.md): the
 * install as drive C, what the game writes going to a save folder, CD 1's
 * copy as drive D, and Jedi Knight's SMACKW32.DLL as it shipped. Windows, DirectDraw, DirectInput, DirectPlay,
 * DirectSound, the joysticks and the CD's audio are the emulator's
 * stand-ins; there's no waveOut device, and the game finds none.
 */
class GameProcess {
public:
    /**
     * Sets up the process and its stand-ins; nothing is loaded until load().
     * @param folders The install and its CDs (findFolders).
     * @param saveFolder Where what the game writes goes, and is read back from
     *        first; the settings it keeps in the registry are kept there too
     *        (registry.txt).
     * @param commandLine Its arguments after the program's name, if any.
     */
    GameProcess(const GameFolders &folders, const std::filesystem::path &saveFolder,
                const std::string &commandLine = {});
    ~GameProcess();

    /**
     * Loads SMACKW32.DLL, if there is one, and the program from the
     * install, and runs the DLL's start-up. Where the program is a US
     * release's, its CD check puts the CD it asks for in drive D (insertCd).
     */
    Error load();
    /**
     * Has the game move its world at every frame at least 10 ms after the
     * last, rather than only more than 20 ms after (0x403d67): up to 100
     * times a second against the game's 47 at most; 100 is the most its
     * clock keeps the right speed at (0x4dd640). After load(); only where
     * JK.EXE is the US release's.
     * @return False where JK.EXE isn't the US release's, left as it is.
     */
    bool updateEveryFrame();
    /**
     * Turns on Setup's 3D acceleration where it's off, as Setup's *Enable
     * 3D Acceleration* keeps it: the primary display, Direct3D's HAL
     * device, 640 by 480 in 16-bit colour; where it's on, the mode chosen
     * stays. Before run().
     * @return Whether it was off.
     */
    bool useDirect3D();
    /**
     * Widescreen: offers the game modes `width` by 480 and has it play in
     * one, its view seeing as much height as at 4:3 and more at the sides,
     * where the program is a US release's
     * (docs/engines/sith/games/jk/systems/hosting.md, step 24): through
     * Direct3D in 16-bit colour (as useDirect3D, the mode that one) if
     * `direct3D` or Setup's 3D acceleration is on, else by the game's own
     * renderer in 8-bit colour. The menus
     * and cut scenes stay 640 by 480. The view's size is widescreen's own:
     * 9, the whole screen, until the player changes it; Mysteries of the
     * Sith's scope sight keeps its shape; the registry file
     * keeps the player's display and view size for 4:3. Before load().
     * @return False, changing nothing, if `width` isn't wider than 640.
     */
    bool useWidescreen(int width, bool direct3D);
    /** Whether load() widened the view for widescreen: the program is a US release's. */
    bool viewWidened() const { return _viewWidened; }
    /** Whether widescreen plays through Direct3D, rather than the game's own renderer (useWidescreen). */
    bool wideDirect3D() const { return _wideDirect3D; }
    /** Whether load() kept the scope's sight as tall as it's wide in widescreen: Mysteries of the Sith's US release. */
    bool sightKept() const { return _sightKept; }
    /**
     * Puts the game's CD `number` (1 or 2: GameInfo::cdMarks) in drive D,
     * if it was found; false if it wasn't.
     */
    bool insertCd(int number);
    /**
     * After the program has been stopped rather than ending by itself: runs
     * its own saving of its Setup settings to the registry (0x4148f0, a
     * step of its end) where JK.EXE is the US release's, so a choice made
     * in Setup isn't lost.
     * @return Whether they were saved.
     */
    bool saveSettings();
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
    x86::DirectPlay &directPlay() { return *_directPlay; }
    x86::Joysticks &joysticks() { return *_joysticks; }
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
    const GameInfo &_game;
    std::string _registryKey; // its full name
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::unique_ptr<x86::DirectX> _directX;
    std::unique_ptr<x86::DirectInput> _directInput;
    std::unique_ptr<x86::DirectPlay> _directPlay;
    std::unique_ptr<x86::Joysticks> _joysticks;
    std::atomic<int64_t> _networkAt{0}; // when the network was last answered, in steady milliseconds
    std::unique_ptr<x86::DirectSound> _directSound;
    std::unique_ptr<x86::CdAudio> _cdAudio;
    const x86::Win32Process::Module *_exe = nullptr;
    int _cdInDrive = 0;
    bool _widescreen = false;
    bool _viewWidened = false;
    bool _wideDirect3D = false;
    bool _sightKept = false;
    void hookCdCheck();
    void hookFocalLength();
    void keepSightShape();
    void setDirect3D(uint32_t mode); // Setup's 3D acceleration on, in `mode` (an index of those offered)
    // The program's addresses (a ProgramCode in game_process.cpp), or null
    // where they aren't known.
    const void *code() const;
    // Whether the program's bytes at `at` are `bytes`: its release's code.
    bool matches(uint32_t at, const uint8_t *bytes, size_t n) const;
    // The game's registry key, kept in the save folder between runs: the
    // file, and the names of the values in it.
    std::filesystem::path _registryFile;
    std::vector<std::string> _registryNames;
    // Values saved as they were, not as the game has them now: the
    // player's display, in widescreen.
    std::vector<std::pair<std::string, std::optional<std::vector<uint8_t>>>> _keptValues;
    void loadRegistry();
    void saveRegistry();
};

} // namespace reluva::sith
