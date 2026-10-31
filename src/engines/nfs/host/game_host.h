// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/mixer.h"
#include "common/net.h"
#include "common/types.h"
#include "common/x86/cpu.h"
#include "common/x86/win32.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/host/force_effects.h"
#include "engines/nfs/host/sound3d.h"
#include "engines/nfs/host/thrash_native.h"

#include <cstdint>
#include <cstdio>
#include <array>
#include <atomic>
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
        // The THRASH DLL the game is told it has, if not renderer: say a 3D
        // card's ("voodoo2a.dll") and it draws as for that card (the sky, fog,
        // 16-bit textures), while renderer is loaded in its place and the
        // drawing is Reluva's (thrash.md).
        std::string presentedRenderer;
        std::filesystem::path saveFolder; // what the game writes; the install folder stays as it is
        uint32_t memoryMb = 256;
        uint64_t instructionLimit = 0; // 0: none
        int callLog = 0;               // how many system calls to log with their arguments
        // The game's clock against the real one: 0.1 has it see a tenth of
        // the time pass, for measuring what full speed would take.
        double timeScale = 1.0;
        // A clock of instructions rather than real time: this many to the
        // millisecond, a millisecond more each time every thread waits
        // (0: real time). Runs are then the same each time, for comparing
        // builds of the interpreter.
        uint32_t instructionsPerMs = 0;
        // Frames whose THRASH calls are logged in full (arguments, results,
        // the memory pointer arguments point at): from, to (inclusive).
        uint64_t thrashLogFrom = 0, thrashLogTo = 0;
        // Draw THRASH's triangles natively rather than in the software
        // renderer's own code (thrash_native.h).
        bool nativeThrash = false;
        // Run the retail nfs3.exe's hottest routines natively (its C
        // library's strcmp and __CHP, its sound mixer's loops), where their
        // code is as expected; false runs them all in the interpreter.
        bool nativeRoutines = true;
        // With nativeThrash: THRASH's frames as lists for the GPU
        // (onThrashFrame) rather than drawn into memory.
        bool gpuThrash = false;
        // With gpuThrash: drawn in memory too, each page flip giving both
        // a frame (onFrame) and a list (onThrashFrame), to compare them.
        bool gpuAndSoftware = false;
        // A joystick offered to the game by this name (DirectInput's
        // devices, as enumerated at start-up); none if empty.
        std::string joystick;
        // It's a racing wheel (DirectInput's wheel, its pedals together on
        // Y); it can give forces (DirectInput's force feedback effects,
        // gamepadForce()).
        bool joystickWheel = false;
        bool forceFeedback = false;
        // The network the game's sockets (WSOCK32) use; none if null, and
        // then it offers no network play.
        std::shared_ptr<net::Sockets> sockets;
        // The address the game's sockets are given wherever it asks for any
        // (0: as asked), for copies on one machine to have one each
        // (127.0.0.2, 127.0.0.3...); and where its broadcasts go instead of
        // 255.255.255.255 (empty: there).
        uint32_t netAddress = 0;
        std::vector<uint32_t> broadcastTo;
        // A file to write each message the game sends or receives to, as a
        // line: the game's milliseconds, > or <, the socket, the peer, the
        // length and the bytes in hex (empty: none).
        std::string netLog;
        // Every netDrop-th packet sent from game millisecond netDropFrom
        // until netDropTo (0: to the end) is lost, to see the game's
        // resends (netDrop 0: none): an IPX datagram or a serial or modem
        // write goes nowhere, a TCP send fails with WSAEWOULDBLOCK (as when
        // the socket's buffer is full). -netlog writes each with x for its
        // direction.
        uint32_t netDrop = 0, netDropFrom = 0, netDropTo = 0;
        // The serial link every COM port is (serial.cpp): "listen:port" to
        // wait for the other copy, "host:port" to connect to it; empty:
        // no serial ports.
        std::string serial;
        // The modem's link (serial.cpp), as `serial`: "listen:port" answers
        // calls, "host:port" is where calls go unless the number dialed is
        // an address; empty: no modem (TAPI32 isn't there).
        std::string modem;
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
    std::map<std::string, uint64_t> callCounts() const;

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
        // Each frame: when it was flipped (ms), the instructions it took, how
        // many of them were the renderer's, and the processor's idle time
        // since the frame before (every thread waiting: microseconds, times).
        struct Frame {
            uint64_t ms = 0, instructions = 0, renderer = 0;
            uint64_t idleMicros = 0, idleWaits = 0;
            // Of the instructions, those run on the thread that flipped (the
            // game's own loop) and on the multimedia timer's thread.
            uint64_t ownThread = 0, timerThread = 0;
        };
        std::vector<Frame> frameLog;
        uint64_t lastIdleMicros = 0, lastIdleWaits = 0, lastOwn = 0, lastTimer = 0;
        std::map<std::string, uint64_t> calls;
        std::map<std::string, uint64_t> callInstructions; // run inside each function, in all
        std::map<int, uint64_t> textureFormats;           // talloc's format -> how many
    };
    const ThrashStats& thrash() const { return thrash_; }
    // Called after each page flip with the frame drawn so far, as RGBA.
    std::function<void(const Surface& frame)> onFrame;
    // With gpuThrash, called after each page flip with what THRASH drew
    // (DirectDraw's frames, the movies', still go to onFrame).
    std::function<void(ThrashFrame&& frame)> onThrashFrame;

    // Input, from any thread: a key ("enter", "up", "a"...) pressed or let
    // go, applied once the game's clock reaches `atMs` (0: at once). False
    // for a name not known.
    bool queueKey(const std::string& name, bool down, uint64_t atMs = 0);
    // The mouse moved by (dx, dy), or its first button pressed or let go.
    void queueMouseMove(int dx, int dy, uint64_t atMs = 0);
    void queueMouseButton(bool down, uint64_t atMs = 0);
    // The joystick's state (Options::joystick), from any thread, applied
    // once the game's clock reaches `atMs` (0: at once).
    void setJoystick(const GamepadState& state, uint64_t atMs = 0);
    // What the force feedback effects the game plays add up to now
    // (Options::forceFeedback), from any thread.
    GamepadForce gamepadForce();

    // What the game's sound buffers play, as a stream for the mixer
    // (thread-safe; the host must outlive it while it plays).
    std::shared_ptr<audio::Stream> soundStream();
    // The sound buffers' blocks played, and of them those the game hadn't
    // written again since they last played (it fell behind).
    struct SoundStats {
        uint64_t blocks = 0, stale = 0;
        uint64_t staleAudible = 0; // of those, the ones not silent: played as silence, if streamed
        std::vector<uint64_t> staleAudibleMs; // when (the game's clock), the first thousand
    };
    SoundStats soundStats() {
        std::lock_guard<std::mutex> lock(soundMutex_);
        return soundStats_;
    }
    // Fills `frames` stereo frames at `rate` with what the playing buffers
    // hold at their play cursors, from any thread.
    void mixSound(int16_t* out, int frames, int rate);
    // Records the sound by the game's clock (-wav): each time the clock
    // passes a 10 ms step, the frames up to the next step are mixed, in
    // the emulation's thread, onto `to` (stereo, `rate` a second). Frame
    // n is then what plays at the game's millisecond n * 1000 / rate, so
    // runs with the same input line up even when their instruction counts
    // differ. Null stops it.
    void recordSound(std::vector<int16_t>* to, int rate);
    // What the game's own mixer (native_sound.cpp) adds to its mix, by
    // window of the game's clock (-voices): the energy (sum of squares, in
    // 16-bit units) going to L+R and to L-R, for voices whose two levels
    // have the same sign (front) and for those with opposite signs (behind,
    // in Dolby). Mixed ahead of the play cursor, by the game's lead.
    struct VoiceEnergy {
        uint64_t ms = 0;
        uint32_t calls = 0, rearCalls = 0;
        double frontC = 0, frontS = 0, rearC = 0, rearS = 0;
    };
    void logVoices(std::vector<VoiceEnergy>* to, uint32_t windowMs);

private:
    void recordSoundDue(uint64_t nowMs);
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
    void addNetwork();  // WSOCK32, on Options::sockets (winsock.cpp)
    void addSerial();   // COM ports over TCP, on Options::serial (serial.cpp)
    void addModem();    // TAPI32 for a modem, on Options::modem (serial.cpp)
    // TAPI's events, delivered when the thread that started it next asks
    // for messages or sleeps (serial.cpp).
    std::function<void(x86::Cpu&)> tapiDeliver_;
    std::function<bool()> tapiNews_; // whether there are events (a wait condition)
    uint32_t tapiThread_ = 0;        // the thread they go to: the one that started TAPI
    std::function<bool()> tapiBusy_; // whether a call is under way (or awaited)
    class CommPort;
    class SerialLink;
    class ModemPort;
    friend class SerialLink;
    void addDirectX();  // DDRAW and DINPUT
    void addDirectSound();
    void addNativeRoutines(); // the C library's hottest routines in nfs3.exe, natively
    void addNativeSound();    // its sound mixer's loops (native_sound.cpp)
    // Replaces the routine at `address` with a native function (a jump to
    // it over its first bytes), if its `size` bytes hash to `hash` (FNV-1a,
    // 64 bits): the retail code, as worked out. False if they don't.
    bool replaceRoutine(uint32_t address, uint32_t size, uint64_t hash, const char* name, x86::Cpu::Native fn);
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
    // IDirectSound3DBuffer and IDirectSound3DListener (directx.cpp), their
    // methods made safe for the audio thread by `locked`.
    void addDirectSound3D(const std::function<void(std::vector<Method>&)>& locked);
    uint32_t callWindow(const Window& w, uint32_t msg, uint32_t wParam, uint32_t lParam);
    Window* window(uint32_t handle);
    void post(uint32_t threadId, const Message& m);

    GameFolders folders_;
    Options options_;
    std::unique_ptr<x86::Cpu> cpu_;
    std::unique_ptr<x86::Win32Process> process_;
    std::atomic<uint64_t> idleMs_{0}; // Options::instructionsPerMs: the milliseconds every thread waited
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
        char ch = 0; // the character it types (WM_CHAR), if any
    };
    std::mutex inputMutex_;
    std::atomic<bool> inputQueued_{false}; // something in the pending queues below
    std::deque<KeyEvent> pendingKeys_;
    std::deque<KeyEvent> bufferedKeys_; // for DirectInput's GetDeviceData
    // The mouse, as DirectInput reports it: buffered moves and buttons
    // (DIDEVICEOBJECTDATA's offset: 0 x, 4 y, 12 the first button).
    struct MouseEvent {
        uint32_t offset = 0, data = 0;
        uint64_t atMs = 0;
    };
    std::deque<MouseEvent> pendingMouse_, bufferedMouse_;
    std::deque<std::pair<uint64_t, GamepadState>> pendingJoystick_;
    GamepadState joystick_; // as applied
    // Each axis's range (DIPROP_RANGE), by its offset in DIJOYSTATE / 4.
    std::array<std::pair<int32_t, int32_t>, 8> axisRange_{};
    void joystickState(x86::Cpu& c, uint32_t to, uint32_t size);
    uint32_t joystickType() const; // DIDEVTYPE
    // Fills a DIEFFECTINFOA for the effect with that GUID; its DIEFT type.
    uint32_t effectInfo(x86::Cpu& c, uint32_t at, uint32_t guidData1);
    void addForceEffects(const std::function<std::vector<Method>(const std::string&, std::function<bool(uint32_t)>)>& unknown);
    // Force feedback: the effects by object, the device's gain
    // (DIPROP_FFGAIN), auto-centring (DIPROP_AUTOCENTER), and whether
    // SendForceFeedbackCommand has paused them or turned the motors off.
    std::mutex forceMutex_;
    std::map<uint32_t, DiEffect> effects_;
    uint32_t forceGain_ = 10000;
    bool autoCentre_ = true, forcePaused_ = false, actuatorsOff_ = false;
    uint32_t effectVtable_ = 0;

    // The game's sockets (winsock.cpp): Winsock's handle -> ours, and how
    // the game has it.
    struct WsSocket {
        int socket = -1;
        bool stream = false, nonBlocking = false, bound = false;
        bool ipx = false; // an IPX datagram socket, carried over UDP (winsock.cpp)
    };
    std::map<uint32_t, WsSocket> wsSockets_;
    uint32_t nextWsSocket_ = 0x100;
    std::map<uint32_t, uint32_t> wsLastError_; // by thread
    bool wsStarted_ = false;
    std::FILE* netLog_ = nullptr;
    void logTraffic(bool sent, uint32_t handle, const net::Address& peer, const uint8_t* data, size_t size,
                    bool dropped = false);
    // Whether this packet sent is one Options::netDrop loses.
    bool dropOutgoing();
    uint64_t netSent_ = 0; // packets sent since netDropFrom
    uint32_t wsBuffer_ = 0; // inet_ntoa's and gethostbyname's results
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
    // Does a THRASH call natively if it can (`result`: what it returns).
    bool nativeDraw(const std::string& name, const std::vector<uint32_t>& args, uint32_t& result);
    // The retail softtria.dll, known well enough that its page flips and
    // clears are done natively too (thrash.md): where it was loaded, less
    // where it asks to be; unset for any other renderer.
    bool softtriaKnown_ = false;
    uint32_t softtria(uint32_t address) const; // one of its globals, by its own address
    // A frame shown straight from a 16-bit 5:6:5 buffer's rectangle.
    void presentBuffer(uint32_t buffer, int pitch, int x0, int y0, int width, int height);

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
        int32_t volume = 0, pan = 0; // hundredths of a decibel
        // Where mixSound has got to, in frames; resynced to the play cursor
        // when it drifts.
        double outCursor = 0;
        bool outSynced = false;
        double rearLow[2] = {0, 0}; // the rear's low-pass, per channel (sound3d.h)
        // Whether each 256-byte block was written since it was last played,
        // for counting the times the game didn't keep up.
        std::vector<uint8_t> fresh;
        bool written = false;
        uint64_t bytesWritten = 0; // more than its size: it's streamed, rewritten as it plays
        uint32_t lastBlock = 0xffffffffu;
        bool staleBlock = false; // the block playing is stale, so silent
        uint32_t flags = 0;      // DSBCAPS_ as created; DSBCAPS_CTRL3D (0x10): placed by `place`
        // Its 3D parameters: those in force, and those set with
        // DS3D_DEFERRED, for CommitDeferredSettings.
        Ds3dSource place, deferredPlace;
        uint32_t position(uint64_t nowMs);
    };
    // Sound memory, by address: how many buffers share it (a buffer and its
    // duplicates), so it's freed with the last.
    std::map<uint32_t, int> soundMemoryUsers_;
    // IDirectSound3DBuffer objects -> the sound buffer each places.
    std::map<uint32_t, uint32_t> sound3dBuffers_;
    uint32_t sound3dBufferVtable_ = 0, listenerObject_ = 0;
    // The listener (the primary buffer's IDirectSound3DListener), as in
    // force and as deferred.
    Ds3dListener listener_, deferredListener_;
    std::mutex soundMutex_; // soundBuffers_, between the game and the audio thread
    std::vector<int32_t> soundMix_;
    SoundStats soundStats_;
    std::vector<VoiceEnergy>* voiceLog_ = nullptr; // logVoices'
    uint32_t voiceWindowMs_ = 100;
    void voiceMixed(bool rear, double c, double s, uint32_t calls);
    std::vector<int16_t>* soundRecord_ = nullptr; // recordSound's
    int soundRecordRate_ = 0;
    uint64_t soundRecordNextMs_ = 0; // when the next step is due
    std::map<uint32_t, DsBuffer> soundBuffers_;
    uint32_t soundBufferVtable_ = 0;
};

} // namespace reluva::nfs
