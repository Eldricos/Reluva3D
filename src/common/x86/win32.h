// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/pe.h"
#include "common/types.h"
#include "common/x86/cpu.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::x86 {

// Memory handed out from a range of the processor's: first fit, blocks
// aligned, freed blocks joined with their neighbours. New blocks are zeroed.
class Arena {
public:
    Arena(Cpu& cpu, uint32_t start, uint32_t end);
    // 0 if there's no room.
    uint32_t alloc(uint32_t size, uint32_t align = 16);
    // Allocates exactly [address, address + size) if it's all free.
    bool claim(uint32_t address, uint32_t size);
    void free(uint32_t address);
    // The size a block was allocated with; 0 if `address` isn't one's start.
    uint32_t size(uint32_t address) const;
    uint32_t freeBytes() const;

private:
    Cpu& cpu_;
    std::map<uint32_t, uint32_t> free_, used_; // start -> size
    void release(uint32_t start, uint32_t size);
};

// A Windows process for 32-bit DLLs built for Windows 95 to XP (the C
// runtime of Visual C++ 6 and its kind) to run in: their images loaded
// and relocated, their imports bound, the system's functions they call
// (KERNEL32's: memory, strings, the C runtime's start-up) stood in for, from
// the behaviour Microsoft documents for them. Files, threads and windows
// aren't there: such DLLs get those from the engine that loads them.
class Win32Process {
public:
    static constexpr uint32_t kStackSize = 4u << 20;

    // `cpu`'s memory, all of it the process's.
    explicit Win32Process(Cpu& cpu);
    Cpu& cpu() { return cpu_; }
    Arena& memory() { return *arena_; }

    struct Module {
        std::string name;
        uint32_t base = 0, size = 0;
        uint32_t entry = 0; // the address of DllMain (its C runtime's start-up); 0 if none
        PeImage image;
        uint32_t exportAddress(const std::string& name) const;
    };
    // Maps a DLL into memory (where it asks if that's free, else wherever
    // there's room, relocated) and binds its imports: functions provide()d
    // and the stand-ins first, the exports of modules already loaded next;
    // others are bound to a function that fails the run, naming them.
    Error load(const std::string& name, const std::vector<uint8_t>& file, const Module*& out);
    // Calls its DllMain for DLL_PROCESS_ATTACH: false (with why) if it
    // faulted or said no.
    Error attach(const Module& module);
    // Runs an EXE's entry point (its C runtime's start-up, which calls
    // WinMain), returning what it returns; fails if it faults, or ends the
    // process (exitCode then holds the code it gave).
    Error start(const Module& exe, uint32_t& exitCode);
    const Module* module(const std::string& name) const;

    // Supplies an import (WINMM.dll's timeGetTime, say), or replaces a
    // stand-in. `dll` is matched ignoring case.
    void provide(const std::string& dll, const std::string& function, Cpu::Native fn);
    // A stdcall function of `args` 32-bit arguments returning `fn`'s result.
    void provideStdcall(const std::string& dll, const std::string& function, int args, std::function<uint32_t(Cpu&)> fn);
    // A cdecl function returning `fn`'s result in EAX.
    void provideCdecl(const std::string& dll, const std::string& function, std::function<uint32_t(Cpu&)> fn);
    // Supplies a data import: the importer's table gets `address` (a
    // variable's, in the process's memory) instead of a function's.
    void provideData(const std::string& dll, const std::string& name, uint32_t address);
    // Whether something is supplied for dll!name already.
    bool provides(const std::string& dll, const std::string& name) const;
    // The address supplied for dll!name (a function's or a variable's); 0
    // if none.
    uint32_t provided(const std::string& dll, const std::string& name) const;
    // The imports the loaded modules have that nothing stands in for
    // ("DLL!function"); calling one fails the run.
    const std::vector<std::string>& missingImports() const { return missing_; }

    // What the program writes for a debugger, or to its standard output
    // and error (its C runtime's messages).
    std::function<void(const std::string& text)> output;
    // Milliseconds for timeGetTime and GetTickCount (default: since start).
    std::function<uint32_t()> milliseconds;

    // The thread's stack and information block (FS).
    uint32_t stackTop() const { return stackTop_; }

    // The program: its file name and folder as the program sees them
    // ("LEGOATeam.exe", "C:\\LEGO Alpha Team\\"), for its command line and
    // GetModuleFileNameA. Default: Elite Force's.
    // Also makes the folder current.
    void setProgram(const std::string& exeName, const std::string& folder);
    const std::string& programFolder() const { return programFolder_; }
    const std::string& programName() const { return programName_; }
    // Where the command line is, as GetCommandLineA returns it.
    uint32_t commandLineAddress() const { return commandLine_; }

    // The last error, for stand-ins added from outside (SetLastError).
    void setLastError(uint32_t e) { lastError_ = e; }

    // --- Files ------------------------------------------------------------------
    // Where the program's files are: a full Windows path ("C:\\GAME\\A.PAC")
    // as a path on this machine, or nothing if the program may not reach
    // it. `forWriting` asks for a file to be created or changed. Without
    // it, the program finds no files (as before files were stood in for).
    std::function<std::optional<std::filesystem::path>(const std::string& windowsPath, bool forWriting)> mapPath;
    // A path as the program gave it, made full: relative ones are taken from
    // the current folder (the program's own folder, until SetCurrentDirectory
    // or _chdir changes it).
    std::string fullPath(const std::string& path) const;
    std::string currentFolder() const { return currentFolder_; }
    void setCurrentFolder(const std::string& folder);
    // The drives the program sees: their letters, and the volume label of
    // the CD-ROM drive, if any ("LEGOATEN").
    std::string cdDrive = "D", cdLabel;

    // An open file, for the stand-ins that work with files (KERNEL32's
    // handles and the C library's FILE).
    struct OpenFile {
        std::FILE* file = nullptr;
        std::string windowsPath;
    };
    // Opens a file for a handle; 0 if it can't be (lastError set). `mode`
    // as fopen's ("rb", "wb", "r+b", "ab").
    uint32_t openFile(const std::string& windowsPath, const char* mode);
    OpenFile* file(uint32_t handle);
    bool closeFile(uint32_t handle);

private:
    Cpu& cpu_;
    std::unique_ptr<Arena> arena_;
    std::vector<std::unique_ptr<Module>> modules_;
    // "dll!function" (lower-case dll) -> native address
    std::map<std::string, uint32_t> provided_;
    std::vector<std::string> missing_;
    uint32_t stackTop_ = 0, tib_ = 0, peb_ = 0;
    std::string programName_ = "stvoy.exe", programFolder_ = "C:\\STVEF\\";
    uint32_t commandLine_ = 0, environment_ = 0, environmentW_ = 0;
    uint32_t lastError_ = 0;
    uint32_t heapHandle_ = 0x7ffe0000; // HeapCreate's handles: never dereferenced
    uint32_t exceptionFilter_ = 0;
    uint64_t startMs_ = 0;
    void addStandIns();
    void addFileStandIns();
    void addSystemStandIns();
    uint32_t nextEvent_ = 0;
    std::string currentFolder_ = "C:\\";
    std::map<uint32_t, OpenFile> files_;
    uint32_t nextFile_ = 0x2000;
    struct Mapping {
        uint32_t file = 0; // a file handle, or 0 for memory
        uint32_t size = 0;
    };
    std::map<uint32_t, Mapping> mappings_;
    struct Search {
        std::vector<std::filesystem::directory_entry> entries;
        size_t next = 0;
    };
    std::map<uint32_t, Search> searches_;
    uint32_t bind(const std::string& dll, const std::string& function, uint32_t ordinal);
    uint32_t moduleHandle(const std::string& name) const;
    void stdcall(const char* dll, const char* name, int args, std::function<uint32_t(Cpu&)> fn);
};

} // namespace reluva::x86
