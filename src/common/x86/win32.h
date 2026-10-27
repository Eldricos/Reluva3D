// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/pe.h"
#include "common/types.h"
#include "common/x86/cpu.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

// Something a handle refers to: a file, an event, a thread...
struct KernelObject {
    virtual ~KernelObject() = default;
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
    // The standard handles: input, output and error.
    static constexpr uint32_t kStdHandles = 0x100;

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
    const Module* module(const std::string& name) const;
    // The module loaded at `base`, if any.
    const Module* moduleAt(uint32_t base) const;

    // Supplies an import (WINMM.dll's timeGetTime, say), or replaces a
    // stand-in. `dll` is matched ignoring case. Returns its address.
    uint32_t provide(const std::string& dll, const std::string& function, Cpu::Native fn);
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

    // --- Executables -----------------------------------------------------------
    // The program's path and command line as it sees them
    // ("C:\\NFS3\\nfs3.exe", "nfs3.exe -d3d"). Set before loading.
    void setProgram(std::string windowsPath, std::string commandLine);
    // Runs an executable's start-up (its entry point, which never returns)
    // on the main thread until the program ends the process, with the
    // threads it starts: true in `exited` (and its exit code) if it did,
    // else why it stopped.
    Error run(const Module& exe, uint32_t& exitCode);

    // --- Handles ---------------------------------------------------------------
    uint32_t addHandle(std::shared_ptr<KernelObject> object);
    template <typename T>
    std::shared_ptr<T> object(uint32_t handle) const {
        auto it = handles_.find(handle);
        return it == handles_.end() ? nullptr : std::dynamic_pointer_cast<T>(it->second);
    }
    bool closeHandle(uint32_t handle);
    // Signals an event (as SetEvent), from the program's side; false if
    // the handle isn't one.
    bool setEvent(uint32_t handle);
    uint32_t lastError() const { return lastError_; }
    void setLastError(uint32_t e) { lastError_ = e; }

    // --- Files (win32_files.cpp) -------------------------------------------------
    // A drive the program sees, its letter mapped to a folder; names are
    // matched ignoring case. Files written go under `writable` when it's
    // set (the folder itself stays as it was), and are read from there
    // first. Types: 3 a fixed disk, 5 a CD-ROM.
    struct Drive {
        char letter = 'C';
        std::filesystem::path root, writable;
        uint32_t type = 3;
        std::string label;
        uint32_t serial = 0x12345678;
    };
    void addDrive(Drive drive);
    // The current directory, a Windows path ("C:\\NFS3").
    void setCurrentDirectory(const std::string& windowsPath);
    const std::string& currentDirectory() const { return currentDirectory_; }
    // A Windows path made absolute ("C:\\NFS3\\GameData"), and the host
    // file it names: for reading, the writable copy if there is one, else
    // the drive's (empty if the drive isn't there).
    std::string fullPath(const std::string& windowsPath) const;
    std::filesystem::path hostPath(const std::string& windowsPath, bool forWriting) const;
    // Each file the program opens, and whether it was there.
    std::function<void(const std::string& windowsPath, bool found)> fileOpened;

    // --- Threads (win32_threads.cpp) -----------------------------------------------
    // The program's threads take turns on the processor, each on a host
    // thread of its own: they change over when one waits, and every
    // `slice` instructions.
    struct Thread : KernelObject {
        uint32_t id = 0, stackTop = 0, tib = 0;
        Cpu::Context context;
        std::function<uint32_t()> body; // runs on its host thread
        std::thread host;
        bool started = false, finished = false;
        bool terminate = false, exitRequested = false; // TerminateThread, ExitThread
        int suspendCount = 0;
        uint32_t exitCode = 259; // STILL_ACTIVE
        int priority = 0;
        // While waiting: what it waits for (nullptr: nothing), and until when
        // (ms; 0: no limit).
        std::function<bool()> waitFor;
        uint64_t waitUntil = 0;
        std::string name;
    };
    // A thread running `body` (which calls into the emulated code), with a
    // stack and information block of its own; suspended if asked.
    std::shared_ptr<Thread> createThread(std::function<uint32_t()> body, bool suspended, std::string name);
    void resumeThread(Thread& t);
    Thread* currentThread() const { return running_; }
    // From the running thread: waits until `ready` is true or `timeoutMs`
    // passes (0xffffffff: never), letting the others run. False if it timed
    // out; ends the run (with abort()) if the process is ending.
    bool wait(std::function<bool()> ready, uint32_t timeoutMs);
    // From the running thread: lets the others run, if any can.
    void yield();
    // From the running thread, after it may have woken another: switches
    // at once if one of higher priority can run, as Windows does.
    void preempt();
    // Milliseconds, as wait() counts them.
    uint64_t now() const;
    // Whether the process is ending (or the running thread is being
    // terminated): native functions that waited should then abort().
    bool ending() const;
    // From any thread: ends the process, as ExitProcess would; run() then
    // returns once every thread has unwound.
    void requestExit() { exiting_ = true; }
    // Instructions between thread changes.
    uint32_t slice = 20000;

private:
    void addFileStandIns();
    // The executable's base: the first module loaded that isn't a DLL.
    uint32_t exeBase() const;
    void addThreadStandIns();
    // Hands the processor to the next thread that can run (or keeps it).
    bool schedule(std::unique_lock<std::mutex>& lock);
    void runThread(std::shared_ptr<Thread> t);
    void exitAll(std::unique_lock<std::mutex>& lock);
    uint32_t allocTib(uint32_t stackTop, uint32_t stackBottom, uint32_t id);

    std::map<uint32_t, std::shared_ptr<KernelObject>> handles_;
    uint32_t nextHandle_ = 0x1000;
    std::vector<Drive> drives_;
    std::string currentDirectory_ = "C:\\";
    std::string programPath_ = "C:\\STVEF\\stvoy.exe";
    std::vector<std::shared_ptr<Thread>> threads_;
    Thread* running_ = nullptr;
    std::mutex mutex_;
    std::condition_variable turn_;
    std::atomic<bool> exiting_{false}; // set from other threads too (requestExit)
    uint32_t exitCode_ = 0;
    std::string failure_; // a thread's fault, which ends the process
    struct Timer {
        uint32_t delay = 0, callback = 0, user = 0;
        bool periodic = false;
        uint64_t due = 0;
    };
    std::map<uint32_t, Timer> timers_; // by timeSetEvent's id
    uint32_t nextTimer_ = 1;
    std::shared_ptr<Thread> timerThread_;
    uint32_t runTimers();
    uint32_t nextThreadId_ = 0x104;
    std::vector<bool> tlsUsed_ = std::vector<bool>(64);

    Cpu& cpu_;
    std::unique_ptr<Arena> arena_;
    std::vector<std::unique_ptr<Module>> modules_;
    // "dll!function" (lower-case dll) -> native address
    std::map<std::string, uint32_t> provided_;
    std::vector<std::string> missing_;
    uint32_t stackTop_ = 0, tib_ = 0, peb_ = 0;
    uint32_t commandLine_ = 0, environment_ = 0, environmentW_ = 0;
    uint32_t lastError_ = 0;
    uint32_t heapHandle_ = 0x7ffe0000; // HeapCreate's handles: never dereferenced
    uint32_t exceptionFilter_ = 0;
    uint64_t startMs_ = 0;
    void addStandIns();
    uint32_t bind(const std::string& dll, const std::string& function, uint32_t ordinal);
    uint32_t moduleHandle(const std::string& name) const;
    void stdcall(const char* dll, const char* name, int args, std::function<uint32_t(Cpu&)> fn);
};

} // namespace reluva::x86
