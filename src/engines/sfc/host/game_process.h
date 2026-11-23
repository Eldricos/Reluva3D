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
#include <vector>

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

    /**
     * Where what the program writes in its folder goes, and is read back
     * from first; without one, it can't write. The registry values it
     * wrote before (its CD key among them) are read back from the
     * folder's registry.txt.
     */
    void setSaveFolder(std::filesystem::path folder);

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
    /**
     * An exception the program raised, from its EXCEPTION_RECORD: its code
     * and where, and for a C++ throw the types thrown and the text in the
     * object (the game's tErrInfo holds its message).
     */
    static std::string describeException(x86::Cpu &c, uint32_t record);

    /** Told of each INI setting the program reads: the file, [section] and key. */
    std::function<void(const std::string &what)> iniReads;
    /** Told of each registry value the program writes, once it's kept. */
    std::function<void(const std::string &key, const std::string &name)> registryWritten;

    /** The file in the save folder the program's registry values are kept in. */
    static constexpr const char *kRegistryFile = "registry.txt";

private:
    /** The Windows functions the shared stand-ins don't have (system.cpp). */
    void addSystem();
    /** Reads back the registry values kept in the save folder. */
    void loadRegistry();
    /** Keeps a value the program wrote, rewriting the save folder's file. */
    void keepRegistryValue(const std::string &key, const std::string &name);
    /** Stands in for a function taking `args` 32-bit arguments, popping them; `fn` gives EAX. */
    void stdcall(const char *dll, const char *name, int args, std::function<uint32_t(x86::Cpu &)> fn);

    std::filesystem::path _dir;
    std::filesystem::path _saveDir;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::unique_ptr<x86::DirectX> _directX;
    std::unique_ptr<x86::DirectInput> _directInput;
    const x86::Win32Process::Module *_exe = nullptr;
    uint32_t _focus = 0; // the window with the focus
    /** A registry value kept, as the program wrote it. */
    struct RegistryValue {
        std::string key;
        std::string name;
        uint32_t type = 0;
        std::vector<uint8_t> data;
    };
    std::map<std::string, RegistryValue> _registry; // by key and name, lower-case, as Windows matches them
};

} // namespace reluva::sfc
