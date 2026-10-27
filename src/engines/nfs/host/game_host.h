// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/host/thrash_native.h"

#include <cstdint>
#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace reluva::nfs {

// The retail nfs3.exe run in the x86 interpreter (docs/engines/nfs.md,
// Phase 4): a Windows process whose C: is the install folder and D: the
// CD, with the system's functions it calls stood in for. Its own renderer
// (THRASH) and sound (iSND) DLLs run too, on DirectDraw and DirectSound
// stood in for.
class GameHost {
public:
    struct Options {
        std::string commandLine = "nfs3.exe";
        std::string renderer = "softtria.dll"; // the THRASH DLL, as the registry names it
        std::filesystem::path saveFolder; // what the game writes; the install folder stays as it is
        uint32_t memoryMb = 256;
        uint64_t instructionLimit = 0; // 0: none
        int callLog = 0;               // how many system calls to log with their arguments
        // The game's clock against the real one: 0.1 has it see a tenth of
        // the time pass, for measuring what full speed would take.
        double timeScale = 1.0;
        // Frames whose THRASH calls are logged in full (arguments, results,
        // the memory pointer arguments point at): from, to (inclusive).
        uint64_t thrashLogFrom = 0, thrashLogTo = 0;
        // Draw THRASH's triangles natively rather than in the software
        // renderer's own code (thrash_native.h).
        bool nativeThrash = false;
    };
    GameHost(GameFolders folders, Options options);
    ~GameHost();

    // Loads nfs3.exe and binds its imports.
    Error load();
    // Runs it until it ends the process or fails.
    Error run(uint32_t& exitCode);

    x86::Cpu& cpu() { return *cpu_; }
    x86::Win32Process& process() { return *process_; }
    const x86::Win32Process::Module* exe() const { return exe_; }

    // Lines for the report: the calls logged, files opened, messages.
    std::function<void(const std::string& line)> log;
    // How many times each native function was called.
    std::map<std::string, uint64_t> callCounts;

    // The window, as the game made it.
    struct Window {
        uint32_t handle = 0, wndProc = 0, style = 0, userData = 0;
        std::string className, title;
        uint32_t threadId = 0;
        int width = 640, height = 480;
    };
    const std::vector<Window>& windows() const { return windows_; }

    // THRASH's state, for reports.
    struct ThrashStats {
        std::string dll;
        uint64_t frames = 0, textures = 0;
        uint64_t rendererInstructions = 0; // inside THRASH's functions (the software renderer's drawing)
        int width = 0, height = 0, bits = 0;
        uint64_t lastFrameInstructions = 0, frameInstructionsTotal = 0;
        // Each frame: when it was flipped (ms), the instructions it took, and
        // how many of them were the renderer's.
        struct Frame {
            uint64_t ms = 0, instructions = 0, renderer = 0;
        };
        std::vector<Frame> frameLog;
        std::map<std::string, uint64_t> calls;
        std::map<std::string, uint64_t> callInstructions; // run inside each function, in all
        std::map<int, uint64_t> textureFormats;           // talloc's format -> how many
    };
    const ThrashStats& thrash() const { return thrash_; }
    // Called after each page flip with the frame drawn so far, as RGBA.
    std::function<void(const Surface& frame)> onFrame;

    // Input, from any thread: a key ("enter", "up", "a"...) pressed or let
    // go, applied once the game's clock reaches `atMs` (0: at once). False
    // for a name not known.
    bool queueKey(const std::string& name, bool down, uint64_t atMs = 0);
    // The mouse moved by (dx, dy), or its first button pressed or let go.
    void queueMouseMove(int dx, int dy, uint64_t atMs = 0);
    void queueMouseButton(bool down, uint64_t atMs = 0);

private:
    struct Message {
        uint32_t hwnd = 0, message = 0, wParam = 0, lParam = 0, time = 0;
    };
    void say(const std::string& line) {
        if (log)
            log(line);
    }
    // A stdcall stand-in of `args` arguments.
    void provide(const char* dll, const char* name, int args, std::function<uint32_t(x86::Cpu&)> fn);
    void addSystem();   // KERNEL32 left over, GDI32, ADVAPI32, VERSION, SHELL32, ole32
    void addUser();     // USER32: windows and messages
    void addNetwork();  // WSOCK32: there's no network
    void addDirectX();  // DDRAW and DINPUT
    void addDirectSound();
    // A COM object whose methods (stdcall, `this` first) are native
    // functions: names and argument counts (this included), in vtable order.
    struct Method {
        const char* name;
        int args;
        std::function<uint32_t(x86::Cpu&)> fn; // empty: logs and returns E_NOTIMPL
    };
    uint32_t makeVtable(const std::string& interface, const std::vector<Method>& methods);
    uint32_t newObject(uint32_t vtable);
    uint32_t makeObject(const std::string& interface, const std::vector<Method>& methods) {
        return newObject(makeVtable(interface, methods));
    }
    uint32_t callWindow(const Window& w, uint32_t msg, uint32_t wParam, uint32_t lParam);
    Window* window(uint32_t handle);
    void post(uint32_t threadId, const Message& m);

    GameFolders folders_;
    Options options_;
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    const x86::Win32Process::Module* exe_ = nullptr;
    int logged_ = 0;

    std::map<std::string, uint32_t> classes_; // class name (lower case) -> window procedure
    std::vector<Window> windows_;
    std::map<uint32_t, std::deque<Message>> queues_; // by thread id
    uint32_t nextWindow_ = 0x10020;
    int cursorCount_ = 0;
    uint8_t keys_[256] = {};   // DirectInput's state, by scan code (0x80 down)
    uint8_t vkDown_[256] = {}; // by virtual-key code
    struct KeyEvent {
        uint8_t vk = 0, scan = 0;
        bool down = false;
        uint64_t atMs = 0;
    };
    std::mutex inputMutex_;
    std::deque<KeyEvent> pendingKeys_;
    std::deque<KeyEvent> bufferedKeys_; // for DirectInput's GetDeviceData
    // The mouse, as DirectInput reports it: buffered moves and buttons
    // (DIDEVICEOBJECTDATA's offset: 0 x, 4 y, 12 the first button).
    struct MouseEvent {
        uint32_t offset = 0, data = 0;
        uint64_t atMs = 0;
    };
    std::deque<MouseEvent> pendingMouse_, bufferedMouse_;
    int mouseDx_ = 0, mouseDy_ = 0;
    bool mouseButton_ = false;
    uint32_t keyboardEvent_ = 0, mouseEvent_ = 0; // SetEventNotification's handles
    uint32_t inputSequence_ = 0;
    void applyInput();

    // Native modules for LoadLibraryA: lower-case name -> handle, and each
    // handle's exports.
    std::map<std::string, uint32_t> nativeModules_;
    std::map<uint32_t, std::map<std::string, uint32_t>> nativeExports_;
    void addNativeModule(const std::string& name, uint32_t handle);
    void addExport(uint32_t module, const std::string& name, int argBytes, std::function<uint32_t(x86::Cpu&)> fn);

    ThrashStats thrash_;
    uint64_t lastFlip_ = 0, lastFlipRenderer_ = 0; // instructions run at the last page flip
    // The game's own DLLs, loaded on LoadLibraryA: lower-case name -> module.
    std::map<std::string, const x86::Win32Process::Module*> gameDlls_;
    uint32_t thrashBase_ = 0;
    uint32_t wrapThrash(const std::string& name, uint32_t address);
    std::unique_ptr<NativeThrash> native_;
    uint32_t thrashWindow_ = 2; // THRASH_window's choice: 1 the front buffer, 2 the back
    // Each window's buffer, as THRASH_lockwindow reports it: address, pitch,
    // width, height (the renderer draws there; a page flip shows it).
    std::map<uint32_t, std::array<uint32_t, 4>> windowBuffers_;
    bool nativeDraw(const std::string& name, const std::vector<uint32_t>& args);

    // DirectDraw's surfaces and palettes (directx.cpp), by object address.
    struct DdSurface {
        int width = 0, height = 0, bits = 16, pitch = 0;
        uint32_t memory = 0, back = 0, caps = 0, palette = 0, colorKey = 0;
        bool primary = false, hasColorKey = false;
    };
    std::map<uint32_t, DdSurface> surfaces_;
    std::map<uint32_t, std::array<uint32_t, 256>> palettes_; // RGBA entries
    uint32_t surfaceVtable_ = 0, paletteVtable_ = 0, clipperVtable_ = 0;
    int modeWidth_ = 640, modeHeight_ = 480, modeBits_ = 16;
    uint32_t newSurface(int width, int height, int bits, uint32_t caps);
    void present(const DdSurface& s);
    Surface frame_;

    // DirectSound's buffers: played in time, not heard yet.
    struct DsBuffer {
        uint32_t memory = 0, size = 0;
        int rate = 22050, blockAlign = 4, channels = 2, bits = 16;
        bool primary = false, playing = false, looping = false;
        uint64_t startMs = 0;
        uint32_t startPosition = 0;
        int32_t volume = 0, pan = 0;
        uint32_t position(uint64_t nowMs);
    };
    std::map<uint32_t, DsBuffer> soundBuffers_;
    uint32_t soundBufferVtable_ = 0;
};

} // namespace reluva::nfs
