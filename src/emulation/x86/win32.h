// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/pe.h"
#include "common/types.h"
#include "emulation/x86/cpu.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace reluva::x86 {

// Memory handed out from a range of the processor's: first fit, blocks
// aligned, freed blocks joined with their neighbours. New blocks are zeroed.
class Arena {
public:
    Arena(Cpu &cpu, uint32_t start, uint32_t end);
    // 0 if there's no room.
    uint32_t alloc(uint32_t size, uint32_t align = 16);
    // Allocates exactly [address, address + size) if it's all free.
    bool claim(uint32_t address, uint32_t size);
    void free(uint32_t address);
    // The size a block was allocated with; 0 if `address` isn't one's start.
    uint32_t size(uint32_t address) const;
    uint32_t freeBytes() const;

private:
    Cpu &_cpu;
    std::map<uint32_t, uint32_t> _free, _used; // start -> size
    // The free blocks' starts again, by their size's power of two (bucket
    // b: sizes from 2^b up to 2^(b+1)), to find the first fit without
    // walking every block below it: a fit is the lowest start among the
    // blocks that hold the size whatever their alignment (the first of
    // each bucket big enough) and those of the buckets that may hold it,
    // tried in order below that.
    std::set<uint32_t> _bySize[33];
    static int bucket(uint32_t size);
    void addFree(uint32_t start, uint32_t size);
    void removeFree(std::map<uint32_t, uint32_t>::iterator it);
    void release(uint32_t start, uint32_t size);
};

// A key (events.h's codes) as Windows knows it: its virtual-key code and
// its scan code (set 1, with 0x100 for the extended keys). False for keys
// Windows has none of.
bool windowsKey(uint32_t key, uint32_t &virtualKey, uint32_t &scanCode);

// Something a handle refers to (Win32Process::addHandle): a file, an
// event, a thread...
struct KernelObject {
    virtual ~KernelObject() = default;
};

// A character device the host provides (a serial port): ReadFile and
// WriteFile on its handle come here. They run on the program's thread and
// may wait (Win32Process::wait); each returns false (and sets `error`, a
// Windows error code) on failure. A read given an OVERLAPPED (`overlapped`)
// mustn't wait: it takes what there is; with nothing there, the read is left
// pending (ERROR_IO_PENDING), its event signalled once readable() and the
// bytes taken by GetOverlappedResult.
struct DeviceObject : KernelObject {
    virtual bool read(Cpu &cpu, uint32_t buffer, uint32_t size, bool overlapped, uint32_t &done, uint32_t &error) = 0;
    virtual bool readable() = 0;
    virtual bool write(Cpu &cpu, uint32_t buffer, uint32_t size, uint32_t &done, uint32_t &error) = 0;
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

    // `cpu`'s memory, all of it the process's; `exeName` is the program
    // the DLLs are told loaded them (its command line and module name).
    // `loadsExe`: an EXE will be loaded (which can't be relocated), so the
    // stack is kept out of where EXEs ask to go, 0x400000 and the few
    // megabytes after it; otherwise the memory is laid out as before that
    // was asked for, which the engine tests' records depend on (the
    // addresses things are loaded at show in their output).
    // `stackAtTop`: the stack and the thread's block at the top of memory,
    // below the native functions, so the whole bottom is free for images
    // (LEGO Alpha Team's layout, which its references were recorded with).
    explicit Win32Process(Cpu &cpu, std::string exeName = "host.exe", bool loadsExe = false, bool stackAtTop = false);
    Cpu &cpu() { return _cpu; }
    Arena &memory() { return *_arena; }

    struct Module {
        std::string name;
        uint32_t base = 0, size = 0;
        uint32_t entry = 0; // the address of DllMain (its C runtime's start-up); 0 if none
        PeImage image;
        uint32_t exportAddress(const std::string &name) const;
    };
    // Maps a DLL into memory (where it asks if that's free, else wherever
    // there's room, relocated) and binds its imports: functions provide()d
    // and the stand-ins first, the exports of modules already loaded next;
    // others are bound to a function that fails the run, naming them.
    Error load(const std::string &name, const std::vector<uint8_t> &file, const Module *&out);
    // The modules loaded, in order.
    std::vector<const Module *> modules() const {
        std::vector<const Module *> all;
        for (const auto &m : _modules)
            all.push_back(m.get());
        return all;
    }
    // Calls its DllMain for DLL_PROCESS_ATTACH: false (with why) if it
    // faulted or said no.
    Error attach(const Module &module);
    // Runs an EXE's entry point (its C runtime's start-up, which calls
    // WinMain), returning what it returns; fails if it faults, or ends the
    // process (exitCode then holds the code it gave).
    Error start(const Module &exe, uint32_t &exitCode);
    const Module *module(const std::string &name) const;
    // The module loaded at `base`, if any.
    const Module *moduleAt(uint32_t base) const;
    // The module an address is in, if any.
    const Module *moduleContaining(uint32_t address) const;

    // Supplies an import (WINMM.dll's timeGetTime, say), or replaces a
    // stand-in. `dll` is matched ignoring case. Returns its address.
    uint32_t provide(const std::string &dll, const std::string &function, Cpu::Native fn);
    // A stdcall function of `args` 32-bit arguments returning `fn`'s result.
    void provideStdcall(const std::string &dll, const std::string &function, int args,
                        std::function<uint32_t(Cpu &)> fn);
    // A cdecl function returning `fn`'s result in EAX.
    void provideCdecl(const std::string &dll, const std::string &function, std::function<uint32_t(Cpu &)> fn);
    // Supplies a data import: the importer's table gets `address` (a
    // variable's, in the process's memory) instead of a function's.
    void provideData(const std::string &dll, const std::string &name, uint32_t address);
    // Whether something is supplied for dll!name already.
    bool provides(const std::string &dll, const std::string &name) const;
    // The address supplied for dll!name (a function's or a variable's); 0
    // if none.
    uint32_t provided(const std::string &dll, const std::string &name) const;
    // The same, for a stand-in's address (for calling it from tests).
    uint32_t standIn(const std::string &dll, const std::string &name) const { return provided(dll, name); }
    // The imports the loaded modules have that nothing stands in for
    // ("DLL!function"); calling one fails the run.
    const std::vector<std::string> &missingImports() const { return _missing; }

    // What the program writes for a debugger, or to its standard output
    // and error (its C runtime's messages).
    std::function<void(const std::string &text)> output;
    // Milliseconds for timeGetTime and GetTickCount (default: since start).
    std::function<uint32_t()> milliseconds;
    // The milliseconds the main thread has asked to Sleep, in all. It
    // doesn't wait (the other threads run meanwhile, and it goes on), so a
    // clock that follows the instructions run can add them, as the time
    // that would have passed.
    uint64_t slept() const { return _slept; }

    // Events (CreateEventA). No thread can wait on one that another sets
    // (they take turns), so an event reads as set to a wait, unless it's
    // tracked: then its state is kept, set by SetEvent or setEvent and
    // cleared by ResetEvent or, for an auto-reset event, by a wait that
    // finds it set. A tracked event that isn't set times out at once.
    // Tracked are those something here sets (A3D's wave events). With host
    // threads, events are kernel objects and keep their state; setEvent
    // then sets one (false if the handle isn't one).
    void trackEvent(uint32_t handle);
    bool setEvent(uint32_t handle);

    // The thread's stack and information block (FS).
    uint32_t stackTop() const { return _stackTop; }

    // The program: its file name and folder as the program sees them
    // ("LEGOATeam.exe", "C:\\LEGO Alpha Team\\"), for its command line and
    // GetModuleFileNameA. Default: Elite Force's.
    // Also makes the folder current.
    void setProgram(const std::string &exeName, const std::string &folder);
    // RDTSC reads a 1 GHz processor's count by the process's clock
    // (milliseconds), not the instructions run (Cpu::timeStamp's default).
    void timeStampFromClock();
    const std::string &programFolder() const { return _programFolder; }
    const std::string &programName() const { return _exeName; }
    // Where the command line is, as GetCommandLineA returns it.
    uint32_t commandLineAddress() const { return _commandLine; }
    // Replaces the command line setProgram made ("nfs3.exe -d3d").
    void setCommandLine(const std::string &line);

    // The last error, for stand-ins added from outside (SetLastError).
    void setLastError(uint32_t e) { _lastError = e; }
    uint32_t lastError() const { return _lastError; }

    // --- Handles ------------------------------------------------------------------
    // Kernel objects by handle, for the stand-ins of drives and host threads
    // (below) and the engine's own (a serial port, say).
    uint32_t addHandle(std::shared_ptr<KernelObject> object);
    template<typename T>
    std::shared_ptr<T> object(uint32_t handle) const {
        auto it = _handles.find(handle);
        return it == _handles.end() ? nullptr : std::dynamic_pointer_cast<T>(it->second);
    }
    bool closeHandle(uint32_t handle);

    // --- Files ------------------------------------------------------------------
    // Where the program's files are: a full Windows path ("C:\\GAME\\A.PAC")
    // as a path on this machine, or nothing if the program may not reach
    // it. `forWriting` asks for a file to be created or changed. Without
    // it, the program finds no files (as before files were stood in for).
    std::function<std::optional<std::filesystem::path>(const std::string &windowsPath, bool forWriting)> mapPath;
    // A path as the program gave it, made full: relative ones are taken from
    // the current folder (the program's own folder, until SetCurrentDirectory
    // or _chdir changes it).
    std::string fullPath(const std::string &path) const;
    std::string currentFolder() const { return _currentFolder; }
    void setCurrentFolder(const std::string &folder);
    // The drives the program sees: their letters, and the volume label of
    // the CD-ROM drive, if any ("LEGOATEN").
    std::string cdDrive = "D", cdLabel;

    // An open file, for the stand-ins that work with files (KERNEL32's
    // handles and the C library's FILE).
    struct OpenFile {
        std::FILE *file = nullptr;
        std::string windowsPath;
    };
    // Opens a file for a handle; 0 if it can't be (lastError set). `mode`
    // as fopen's ("rb", "wb", "r+b", "ab").
    uint32_t openFile(const std::string &windowsPath, const char *mode);
    OpenFile *file(uint32_t handle);
    bool closeFile(uint32_t handle);

    // The exit code the program ended itself with (exit, ExitProcess), if it
    // did; its run then ends with an error saying so.
    std::optional<uint32_t> exited;

    // --- Drives (win32_files.cpp) -------------------------------------------------
    // Instead of mapPath, the program may see drives: each letter a folder
    // of this machine, names matched ignoring case. Files written go under
    // `writable` when it's set (the folder itself stays as it was), and are
    // read from there first. Types: 3 a fixed disk, 5 a CD-ROM. The first
    // drive added puts the stand-ins for files, folders and drives of their
    // own in place of those above, with handles (addHandle); and devices
    // (openSerialPort) and overlapped reads, which need host threads.
    struct Drive {
        char letter = 'C';
        std::filesystem::path root, writable;
        uint32_t type = 3;
        std::string label;
        uint32_t serial = 0x12345678;
    };
    void addDrive(Drive drive);
    // The host file a Windows path names on the drives: for reading, the
    // writable copy if there is one, else the drive's (empty if the drive
    // isn't there).
    std::filesystem::path hostPath(const std::string &windowsPath, bool forWriting) const;
    // Each file the program opens, and whether it was there.
    std::function<void(const std::string &windowsPath, bool found)> fileOpened;
    // A device the program opens by name: "COM1" to "COM9" (also as
    // "\\.\COM1"), its number given; null if there's none (the open fails
    // with ERROR_FILE_NOT_FOUND).
    std::function<std::shared_ptr<DeviceObject>(int port)> openSerialPort;

    // --- Host threads (win32_threads.cpp) -------------------------------------------
    // For a whole program whose threads wait on each other (events,
    // mutexes, the multimedia timer), instead of the turns above: each of
    // its threads runs on a host thread of its own, one at a time, and
    // they change over when one waits and every `slice` instructions
    // (Cpu::setSlice). useHostThreads() puts their stand-ins (threads,
    // waits, events, mutexes, critical sections, TLS, Sleep, timeSetEvent,
    // ExitProcess) in place of the others; run() then runs the program.
    void useHostThreads();
    bool hostThreads() const { return _hostThreads; }
    // Runs an executable's start-up (its entry point, which never returns)
    // on the main thread until the program ends the process, with the
    // threads it starts: its exit code if it did, else why it stopped.
    Error run(const Module &exe, uint32_t &exitCode);
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
        uint64_t instructions = 0; // run on it, up to its last turn's end
        // Notified when its turn comes (one each, so a change of thread
        // wakes only the host thread whose turn it is).
        std::condition_variable turn;
    };
    // A thread running `body` (which calls into the emulated code), with a
    // stack and information block of its own; suspended if asked.
    std::shared_ptr<Thread> createThread(std::function<uint32_t()> body, bool suspended, std::string name);
    void resumeThread(Thread &t);
    Thread *currentThread() const { return _running; }
    // From the running thread: waits until `ready` is true or `timeoutMs`
    // passes (0xffffffff: never), letting the others run. False if it timed
    // out; ends the run (with abort()) if the process is ending.
    bool wait(const std::function<bool()> &ready, uint32_t timeoutMs);
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
    void requestExit() { _exiting = true; }
    // Resets an event, then signals it as soon as `condition` holds (checked
    // whenever a thread waits on it), for overlapped I/O finishing.
    void signalEventWhen(uint32_t handle, std::function<bool()> condition);
    // Instructions between thread changes.
    uint32_t slice = 20000;
    // Called at each thread change, on the thread that ran (for checks by
    // the clock, such as memory written at a moment).
    std::function<void(Cpu &cpu)> onSlice;
    // Called when every thread waits, instead of sleeping a millisecond: for
    // a clock that isn't the real one (it should move that clock on).
    std::function<void()> idleTick;
    // Called when a thread of the program calls Sleep or SleepEx, before it
    // sleeps: for the host's own deliveries to a thread that polls so
    // (TAPI's callbacks, say).
    std::function<void(Cpu &cpu)> beforeSleep;
    // How often every thread waited at once, and for how long in all
    // (microseconds): time the processor stood idle.
    std::atomic<uint64_t> idleWaits{0}, idleMicros{0};
    // From the running thread: the instructions run on `t` so far (its
    // turn now included), and on the multimedia timer's thread (0 if
    // there's none).
    uint64_t instructionsOn(const Thread *t) const {
        return t ? t->instructions + (t == _running ? _cpu.executed() - _turnStart : 0) : 0;
    }
    uint64_t timerInstructions() const { return instructionsOn(_timerThread.get()); }

    // --- Threads ---------------------------------------------------------------
    // The program's other threads take turns with its main one: when the
    // main thread sleeps or looks for messages, each thread that's due runs
    // (Cpu::resume) until it sleeps itself or ends. A thread's Sleep is where
    // it gives way, so code shared between threads is never run by two at
    // once.
    // Starts a thread at `start` with `arg` (its first turn comes at the
    // main thread's next Sleep or look for messages); its handle.
    uint32_t startThread(uint32_t start, uint32_t arg);
    // The threads that are due, each for a turn. Nothing while one runs.
    void runThreads();
    bool inThread() const { return _current >= 0; }
    // For the running thread's natives: it sleeps for `ms` (giving way), or
    // ends.
    void threadSleep(uint32_t ms);
    void threadEnd();
    // Threads started, and those still going.
    size_t threadsStarted() const { return _turnThreads.size(); }
    size_t threadsRunning() const;

    // --- Windows and messages (USER32, GDI32) -----------------------------------
    // The desktop the program sees.
    int screenWidth = 800, screenHeight = 600, screenBits = 16;
    struct Window {
        std::string className, title;
        uint32_t proc = 0; // its window procedure
        uint32_t style = 0, exStyle = 0;
        int x = 0, y = 0, width = 0, height = 0; // the whole window, on the screen
        bool visible = false;
        uint32_t classStyle = 0; // its class's (CS_DBLCLKS: it gets double clicks)
    };
    const Window *window(uint32_t handle) const;
    // A window's client area on the screen: left, top, width, height.
    std::optional<std::array<int, 4>> clientArea(uint32_t handle) const;
    // Moves and sizes a window (as SetWindowPos does).
    void moveWindow(uint32_t handle, int x, int y, int width, int height);
    // The first window the program made that's still there; 0 if none.
    uint32_t mainWindow() const;
    // Queues a message as PostMessageA does: how the engine passes input on.
    void postMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam);
    // Calls a window's procedure as SendMessageA does; 0 if there's no such window.
    uint32_t sendMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam);
    // The mouse pointer, on the screen.
    int cursorX = 0, cursorY = 0;
    // Called when the program looks for a message and none is queued.
    std::function<void()> idle;
    // --- Input, as the engine passes it on ---------------------------------------
    // The pointer moved to (x, y) on the screen (kept within ClipCursor's
    // rectangle): WM_MOUSEMOVE to the main window, in its client
    // coordinates, with the buttons held.
    void mouseMoveTo(int x, int y);
    // A mouse button (0 left, 1 right, 2 middle) pressed or released where
    // the pointer is; a second press soon after, near the first, is a
    // double click for a window whose class asks for them.
    void mouseButton(int button, bool down);
    // The wheel turned, in WHEEL_DELTA units (120 a notch; forward positive).
    void mouseWheel(int delta);
    // A key pressed or released: its virtual-key code and its scan code.
    void key(uint32_t virtualKey, uint32_t scanCode, bool down);
    // The same input as raw devices see it (DirectInput listens).
    struct RawInput {
        enum class Kind { kMotion, kButton, kWheel, kKey } kind;
        int a = 0, b = 0; // Motion: dx, dy; Button: button, down; Wheel: delta; Key: scan code, down
    };
    std::vector<std::function<void(const RawInput &)>> rawInput;

    // What MessageBoxA shows: by default, passed to `output`.
    std::function<void(const std::string &caption, const std::string &text)> messageBox;

    // --- The registry (ADVAPI32) ------------------------------------------------
    // Keys are full paths from a root ("HKEY_LOCAL_MACHINE\\Software\\..."),
    // matched ignoring case. Values set by the program stay in memory;
    // `registryWritten` tells the engine, to keep them if it wants.
    void setRegistryValue(const std::string &key, const std::string &name, uint32_t type, std::vector<uint8_t> data);
    void setRegistryString(const std::string &key, const std::string &name, const std::string &text);
    std::optional<std::vector<uint8_t>> registryValue(const std::string &key, const std::string &name,
                                                      uint32_t *type = nullptr) const;
    std::function<void(const std::string &key, const std::string &name)> registryWritten;

    // --- COM (ole32) --------------------------------------------------------------
    // Makes the object CoCreateInstance asks for (its class and interface as
    // "{xxxxxxxx-...}"), writing its address to `out`; returns the HRESULT.
    // Without it, no class is registered.
    std::function<uint32_t(const std::string &clsid, const std::string &iid, uint32_t out)> createComObject;

private:
    Cpu &_cpu;
    std::unique_ptr<Arena> _arena;
    std::vector<std::unique_ptr<Module>> _modules;
    // "dll!function" (lower-case dll) -> native address
    std::map<std::string, uint32_t> _provided;
    std::vector<std::string> _missing;
    uint32_t _stackTop = 0, _tib = 0, _peb = 0;
    struct TurnThread {
        uint32_t handle = 0;
        Cpu::Context context;
        uint32_t wakeAt = 0; // in milliseconds()
        bool finished = false;
    };
    std::vector<TurnThread> _turnThreads;
    int _current = -1;          // the thread running (-1: the main one)
    uint32_t _threadReturn = 0; // where a thread's function returns to: a native that ends it
    std::string _exeName, _programFolder = "C:\\STVEF\\";
    uint32_t _commandLine = 0, _environment = 0, _environmentW = 0;
    uint32_t _lastError = 0;
    uint32_t _heapHandle = 0x7ffe0000; // HeapCreate's handles: never dereferenced
    uint32_t _exceptionFilter = 0;
    uint64_t _startMs = 0;
    void addStandIns();
    void addFileStandIns();
    void addSystemStandIns();
    void addUserStandIns();
    void addRegistryStandIns();
    struct WindowClass {
        uint32_t proc = 0, style = 0;
    };
    std::map<std::string, WindowClass> _classes; // by lower-case name
    uint32_t _buttons = 0;                       // MK_ flags held
    std::array<uint8_t, 256> _keysDown{};        // by virtual key
    std::optional<std::array<int, 4>> _clip;     // ClipCursor's: left, top, right, bottom
    uint32_t _lastClickTime = 0;
    int _lastClickButton = -1, _lastClickX = 0, _lastClickY = 0;
    void notify(const RawInput &r) {
        for (const auto &f : rawInput)
            f(r);
    }
    std::map<uint32_t, Window> _windows;
    uint32_t _nextWindow = 0x10010;
    struct Message {
        uint32_t window, message, wParam, lParam;
    };
    std::vector<Message> _messages;
    int _cursorShown = 0;
    struct RegistryKey {
        std::string path; // as first given
        std::vector<std::pair<std::string, std::pair<uint32_t, std::vector<uint8_t>>>> values;
    };
    std::map<std::string, RegistryKey> _registry; // by lower-case path
    std::map<uint32_t, std::string> _openKeys;    // handle -> lower-case path
    uint32_t _nextKey = 0x3000;
    uint32_t _nextEvent = 0;
    uint64_t _slept = 0;
    uint64_t _timeStampAt = 0, _timeStampExecuted = 0; // RDTSC's last reading, and the instructions then
    struct Event {
        bool manualReset = false, set = false, tracked = false;
    };
    std::map<uint32_t, Event> _events;
    std::string _currentFolder = "C:\\";
    std::map<uint32_t, OpenFile> _files;
    uint32_t _nextFile = 0x2000;
    struct Mapping {
        uint32_t file = 0; // a file handle, or 0 for memory
        uint32_t size = 0;
    };
    std::map<uint32_t, Mapping> _mappings;
    struct Search {
        std::vector<std::filesystem::directory_entry> entries;
        size_t next = 0;
    };
    std::map<uint32_t, Search> _searches;
    uint32_t bind(const std::string &dll, const std::string &function, uint32_t ordinal);
    uint32_t moduleHandle(const std::string &name) const;
    // A DLL from the program's files, loaded and started (LoadLibraryA's).
    const Module *loadFromDisk(const std::string &name);
    mutable std::vector<std::string> _standInDlls; // lower-case names, by handle
    std::map<uint32_t, uint32_t> _virtualBlocks;   // VirtualAlloc's: start -> size
    void stdcall(const char *dll, const char *name, int args, std::function<uint32_t(Cpu &)> fn);
    // The executable's base: the first module loaded that isn't a DLL.
    uint32_t exeBase() const;

    // Handles (addHandle): from 0x10000000, apart from the files' and keys'.
    std::map<uint32_t, std::shared_ptr<KernelObject>> _handles;
    uint32_t _nextHandle = 0x10000000;

    // Drives (win32_files.cpp).
    std::vector<Drive> _drives;
    void addDriveStandIns();
    // A Windows path made full ("C:\\NFS3\\GameData"): "." and ".."
    // resolved, a drive's own current folder for "D:name".
    std::string drivePath(const std::string &windowsPath) const;
    std::string driveCurrent() const; // the current folder, without its last backslash
    // Overlapped device reads left pending, by OVERLAPPED address.
    struct PendingRead {
        uint32_t handle = 0, buffer = 0, size = 0;
    };
    std::map<uint32_t, PendingRead> _pendingReads;

    // Host threads (win32_threads.cpp).
    bool _hostThreads = false;
    bool setHostEvent(uint32_t handle); // an event of theirs (setEvent)
    void addThreadStandIns();
    // Hands the processor to the next thread that can run (or keeps it).
    bool schedule(std::unique_lock<std::mutex> &lock);
    void runThread(std::shared_ptr<Thread> t);
    void exitAll(std::unique_lock<std::mutex> &lock);
    uint32_t allocTib(uint32_t stackTop, uint32_t stackBottom, uint32_t id);
    std::vector<std::shared_ptr<Thread>> _threads;
    Thread *_running = nullptr;
    std::mutex _mutex;
    std::atomic<bool> _exiting{false}; // set from other threads too (requestExit)
    uint32_t _exitCode = 0;
    std::string _failure; // a thread's fault, which ends the process
    struct Timer {
        uint32_t delay = 0, callback = 0, user = 0;
        bool periodic = false;
        uint64_t due = 0;
    };
    std::map<uint32_t, Timer> _timers; // by timeSetEvent's id
    uint32_t _nextTimer = 1;
    std::shared_ptr<Thread> _timerThread;
    uint64_t _turnStart = 0; // instructions run when the running thread's turn began
    uint32_t runTimers();
    uint32_t _nextThreadId = 0x104;
    std::vector<bool> _tlsUsed = std::vector<bool>(64);
};

} // namespace reluva::x86
