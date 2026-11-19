// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "emulation/x86/glide/glide2_x86.h"
#include "emulation/x86/glide/glide3_x86.h"
#include "graphics/glide/gpu_renderer.h"
#include "common/net.h"
#include "common/types.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
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

// The retail nfs3.exe run in the x86 interpreter (docs/engines/nfs/games/nfs3/PLAN.md,
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
        // drawing is Reluva's (thrash.md). "d3da.dll" gives the game's
        // Direct3D path (docs/engines/nfs/games/nfs3/formats/hrz.md, The Direct3D renderer: no
        // fog, the clouds' constant alpha, the dimming by the effects
        // intensity), the registry describing a Direct3D device, and with
        // nativeThrash the drawing follows d3da's device set-up (bilinear
        // filtering, no dithering; NativeThrash::setDirect3D).
        std::string presentedRenderer;
        // With nativeThrash: textures point-sampled instead of the
        // bilinear filtering both DLLs ask for (NativeThrash::setPointSampled).
        bool pointSampled = false;
        // Network races with positional updates (net.md, Positional
        // updates): each copy runs its own car without waiting for the
        // others, which it places from their statuses every 32 steps. The
        // game has the choice but its host and join dialogs never show it;
        // this makes them choose it. Every player needs it the same.
        bool positionalUpdates = false;
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
        // The processor's clock the time stamp counter reports, in MHz, by
        // the game's clock. The game sets its detail defaults from it on a
        // first run, and its movies' display mode (mad.md: 1 under 133, 2
        // under 200, else 3).
        uint32_t cpuMhz = 300;
        // CPUID reports MMX (Cpu::reportMmx): the game's sound mixer then
        // takes its integer MMX routines (bnk.md) instead of the float ones
        // (which run natively, nativeRoutines).
        bool mmx = false;
        // Frames whose THRASH calls are logged in full (arguments, results,
        // the memory pointer arguments point at): from, to (inclusive).
        uint64_t thrashLogFrom = 0;
        uint64_t thrashLogTo = 0;
        // Draw THRASH's triangles natively rather than in the software
        // renderer's own code (thrash_native.h).
        bool nativeThrash = false;
        // Run the retail nfs3.exe's hottest routines natively (its C
        // library's strcmp and __CHP, its sound mixer's loops), where their
        // code is as expected; false runs them all in the interpreter.
        bool nativeRoutines = true;
        // The retail voodoo2a.dll run as the renderer, unchanged, on Reluva's
        // stand-in for 3dfx's glide2x.dll (graphics/glide, docs/common/graphics/glide.md)
        // rather than any drawing of Reluva's own: its frames are the Glide
        // pipeline's front buffer at each grBufferSwap. Overrides renderer,
        // presentedRenderer and nativeThrash.
        bool glide = false;
        // With glide: THRASH's calls also drawn natively (thrash_native.h)
        // into a buffer of the host's, each page flip giving that frame
        // (onCompareFrame) after the Glide one (onFrame), of the same state.
        bool glideCompare = false;
        // With glide, for comparisons: the texture filter held to this
        // (0 point-sampled, 1 bilinear) whatever the DLL asks; -1 as asked.
        int glideFilter = -1;
        // With glide: a THRASH DLL file outside the install (a host path)
        // run in voodoo2a.dll's place, as the CD's Voodoo3 patch DLL
        // (Patch/Voodoo3/voodoo2a.dll). One that loads glide3x.dll gets the
        // Glide 3 stand-in on a Voodoo3 board (thrash.md, The Voodoo3
        // patch's voodoo2a.dll); empty: the install's own.
        std::string glideDll;
        // With glide: the board Glide stands in for (glide::boardByName:
        // "voodoo1", "rush", "voodoo2", "banshee", "voodoo3"); empty for the
        // DLL's own (a Voodoo2 for Glide 2, a Voodoo3 for Glide 3).
        std::string glideBoard;
        // With glide: what draws Glide's pixels. 0 the software reference
        // rasterizer; 1 the GPU backend (graphics/glide/gpu_renderer.h) when
        // there's a GPU, else the software one; 2 the GPU, with every call
        // drawn in software too and each frame compared (onGlideComparison).
        int glideGpu = 0;
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
        uint32_t netDrop = 0;
        uint32_t netDropFrom = 0;
        uint32_t netDropTo = 0;
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
    Error run(uint32_t &exitCode);

    x86::Cpu &cpu() { return *_cpu; }
    x86::Win32Process &process() { return *_process; }
    const x86::Win32Process::Module *exe() const { return _exe; }

    // Lines for the report: the calls logged, files opened, messages.
    std::function<void(const std::string &line)> log;
    // How many times each native function was called.
    std::map<std::string, uint64_t> callCounts() const;

    // The window, as the game made it.
    struct Window {
        uint32_t handle = 0;
        uint32_t wndProc = 0;
        uint32_t style = 0;
        uint32_t userData = 0;
        std::string className;
        std::string title;
        uint32_t threadId = 0;
        int width = 640;
        int height = 480;
    };
    const std::vector<Window> &windows() const { return _windows; }

    // THRASH's state, for reports.
    struct ThrashStats {
        std::string dll;
        uint64_t frames = 0;
        uint64_t textures = 0;
        uint64_t rendererInstructions = 0; // inside THRASH's functions (the software renderer's drawing)
        int width = 0;
        int height = 0;
        int bits = 0;
        uint64_t lastFrameInstructions = 0;
        uint64_t frameInstructionsTotal = 0;
        // Each frame: when it was flipped (ms), the instructions it took, how
        // many of them were the renderer's, and the processor's idle time
        // since the frame before (every thread waiting: microseconds, times).
        struct Frame {
            uint64_t ms = 0;
            uint64_t instructions = 0;
            uint64_t renderer = 0;
            uint64_t idleMicros = 0;
            uint64_t idleWaits = 0;
            // Of the instructions, those run on the thread that flipped (the
            // game's own loop) and on the multimedia timer's thread.
            uint64_t ownThread = 0;
            uint64_t timerThread = 0;
        };
        std::vector<Frame> frameLog;
        uint64_t lastIdleMicros = 0;
        uint64_t lastIdleWaits = 0;
        uint64_t lastOwn = 0;
        uint64_t lastTimer = 0;
        std::map<std::string, uint64_t> calls;
        std::map<std::string, uint64_t> callInstructions; // run inside each function, in all
        std::map<int, uint64_t> textureFormats;           // talloc's format -> how many
    };
    const ThrashStats &thrash() const { return _thrash; }
    // The Glide stand-in (Options::glide), if there is one: glide2x.dll's,
    // or glide3x.dll's for a DLL that loads it (Options::glideDll).
    glide::GlideDll *glide() { return _glide.get(); }
    // With Options::glideGpu 2: each Glide frame's difference from the
    // software reference, at its buffer swap.
    std::function<void(const glide::FrameComparison &)> onGlideComparison;
    // What draws Glide's pixels ("software", or the GPU device's name).
    const std::string &glideRenderer() const { return _glideRenderer; }
    // Called after each page flip with the frame drawn so far, as RGBA.
    std::function<void(const Surface &frame)> onFrame;
    // With gpuThrash, called after each page flip with what THRASH drew
    // (DirectDraw's frames, the movies', still go to onFrame).
    std::function<void(ThrashFrame &&frame)> onThrashFrame;
    // With Options::glideCompare: the native drawing of each page flip.
    std::function<void(const Surface &frame)> onCompareFrame;

    // Input, from any thread: a key ("enter", "up", "a"...) pressed or let
    // go, applied once the game's clock reaches `atMs` (0: at once). False
    // for a name not known.
    bool queueKey(const std::string &name, bool down, uint64_t atMs = 0);
    // The mouse moved by (dx, dy), or its first button pressed or let go.
    void queueMouseMove(int dx, int dy, uint64_t atMs = 0);
    void queueMouseButton(bool down, uint64_t atMs = 0);
    // The joystick's state (Options::joystick), from any thread, applied
    // once the game's clock reaches `atMs` (0: at once).
    void setJoystick(const GamepadState &state, uint64_t atMs = 0);
    // What the force feedback effects the game plays add up to now
    // (Options::forceFeedback), from any thread.
    GamepadForce gamepadForce();

    // What the game's sound buffers play, as a stream for the mixer
    // (thread-safe; the host must outlive it while it plays).
    std::shared_ptr<audio::OutputStream> soundStream();
    // The sound buffers' blocks played, and of them those the game hadn't
    // written again since they last played (it fell behind).
    struct SoundStats {
        uint64_t blocks = 0;
        uint64_t stale = 0;
        uint64_t staleAudible = 0;            // of those, the ones not silent: played as silence, if streamed
        std::vector<uint64_t> staleAudibleMs; // when (the game's clock), the first thousand
    };
    SoundStats soundStats() {
        std::lock_guard<std::mutex> lock(_soundMutex);
        return _soundStats;
    }
    // Fills `frames` stereo frames at `rate` with what the playing buffers
    // hold at their play cursors, from any thread.
    void mixSound(int16_t *out, int frames, int rate);
    // Records the sound by the game's clock (-wav): each time the clock
    // passes a 10 ms step, the frames up to the next step are mixed, in
    // the emulation's thread, onto `to` (stereo, `rate` a second). Frame
    // n is then what plays at the game's millisecond n * 1000 / rate, so
    // runs with the same input line up even when their instruction counts
    // differ. Null stops it.
    void recordSound(std::vector<int16_t> *to, int rate);
    // What the game's own mixer (native_sound.cpp) adds to its mix, by
    // window of the game's clock (-voices): the energy (sum of squares, in
    // 16-bit units) going to L+R and to L-R, for voices whose two levels
    // have the same sign (front) and for those with opposite signs (behind,
    // in Dolby). Mixed ahead of the play cursor, by the game's lead.
    // With Options::mmx, taken at the MMX mixers' entries instead (the mix
    // itself still the game's, in the interpreter).
    struct VoiceEnergy {
        uint64_t ms = 0;
        uint32_t calls = 0;
        uint32_t rearCalls = 0;
        double frontC = 0;
        double frontS = 0;
        double rearC = 0;
        double rearS = 0;
    };
    void logVoices(std::vector<VoiceEnergy> *to, uint32_t windowMs);

private:
    void recordSoundDue(uint64_t nowMs);
    struct Message {
        uint32_t hwnd = 0;
        uint32_t message = 0;
        uint32_t wParam = 0;
        uint32_t lParam = 0;
        uint32_t time = 0;
    };
    void say(const std::string &line) const {
        if (log)
            log(line);
    }
    // A stdcall stand-in of `args` arguments.
    void provide(const char *dll, const char *name, int args, std::function<uint32_t(x86::Cpu &)> fn);
    void addSystem();  // KERNEL32 left over, GDI32, ADVAPI32, VERSION, SHELL32, ole32
    void addUser();    // USER32: windows and messages
    void addNetwork(); // WSOCK32, on Options::sockets (winsock.cpp)
    void addSerial();  // COM ports over TCP, on Options::serial (serial.cpp)
    void addModem();   // TAPI32 for a modem, on Options::modem (serial.cpp)
    // TAPI's events, delivered when the thread that started it next asks
    // for messages or sleeps (serial.cpp).
    std::function<void(x86::Cpu &)> _tapiDeliver;
    std::function<bool()> _tapiNews; // whether there are events (a wait condition)
    uint32_t _tapiThread = 0;        // the thread they go to: the one that started TAPI
    std::function<bool()> _tapiBusy; // whether a call is under way (or awaited)
    class CommPort;
    class SerialLink;
    class ModemPort;
    friend class SerialLink;
    void addDirectX(); // DDRAW and DINPUT
    void addGlide();   // glide2x.dll or glide3x.dll, on Options::glide (thrash.cpp)
    std::unique_ptr<glide::GlideDll> _glide;
    std::string _glideRenderer = "software";
    // Options::glideCompare: THRASH's calls drawn natively too, into a buffer
    // of 640x480 5:6:5 pixels in guest memory (thrash.cpp).
    void mirrorNative(const std::string &name, const std::vector<uint32_t> &args);
    uint32_t _compareBuffer = 0;
    uint32_t _compareClear = 0;
    std::array<int, 4> _compareClip{0, 0, 640, 480};
    void addDirectSound();
    void choosePositionalUpdates();
    void addNativeRoutines(); // the C library's hottest routines in nfs3.exe, natively
    void addNativeSound();    // its sound mixer's loops (native_sound.cpp)
    // Replaces the routine at `address` with a native function (a jump to
    // it over its first bytes), if its `size` bytes hash to `hash` (FNV-1a,
    // 64 bits): the retail code, as worked out. False if they don't.
    bool replaceRoutine(uint32_t address, uint32_t size, uint64_t hash, const char *name, x86::Cpu::Native fn);
    // A COM object whose methods (stdcall, `this` first) are native
    // functions: names and argument counts (this included), in vtable order.
    struct Method {
        const char *name;
        int args;
        std::function<uint32_t(x86::Cpu &)> fn; // empty: logs and returns E_NOTIMPL
    };
    uint32_t makeVtable(const std::string &interface, const std::vector<Method> &methods);
    uint32_t newObject(uint32_t vtable);
    uint32_t makeObject(const std::string &interface, const std::vector<Method> &methods) {
        return newObject(makeVtable(interface, methods));
    }
    // IDirectSound3DBuffer and IDirectSound3DListener (directx.cpp), their
    // methods made safe for the audio thread by `locked`.
    void addDirectSound3D(const std::function<void(std::vector<Method> &)> &locked);
    uint32_t callWindow(const Window &w, uint32_t msg, uint32_t wParam, uint32_t lParam);
    Window *window(uint32_t handle);
    void post(uint32_t threadId, const Message &m);

    GameFolders _folders;
    Options _options;
    std::unique_ptr<x86::Cpu> _cpu;
    std::unique_ptr<x86::Win32Process> _process;
    std::atomic<uint64_t> _idleMs{0}; // Options::instructionsPerMs: the milliseconds every thread waited
    const x86::Win32Process::Module *_exe = nullptr;
    int _logged = 0;

    std::map<std::string, uint32_t> _classes; // class name (lower case) -> window procedure
    std::vector<Window> _windows;
    std::map<uint32_t, std::deque<Message>> _queues; // by thread id
    uint32_t _nextWindow = 0x10020;
    int _cursorCount = 0;
    uint8_t _keys[256] = {};   // DirectInput's state, by scan code (0x80 down)
    uint8_t _vkDown[256] = {}; // by virtual-key code
    struct KeyEvent {
        uint8_t vk = 0;
        uint8_t scan = 0;
        bool down = false;
        uint64_t atMs = 0;
        char ch = 0; // the character it types (WM_CHAR), if any
    };
    std::mutex _inputMutex;
    std::atomic<bool> _inputQueued{false}; // something in the pending queues below
    std::deque<KeyEvent> _pendingKeys;
    std::deque<KeyEvent> _bufferedKeys; // for DirectInput's GetDeviceData
    // The mouse, as DirectInput reports it: buffered moves and buttons
    // (DIDEVICEOBJECTDATA's offset: 0 x, 4 y, 12 the first button).
    struct MouseEvent {
        uint32_t offset = 0;
        uint32_t data = 0;
        uint64_t atMs = 0;
    };
    std::deque<MouseEvent> _pendingMouse;
    std::deque<MouseEvent> _bufferedMouse;
    std::deque<std::pair<uint64_t, GamepadState>> _pendingJoystick;
    GamepadState _joystick; // as applied
    // Each axis's range (DIPROP_RANGE), by its offset in DIJOYSTATE / 4.
    std::array<std::pair<int32_t, int32_t>, 8> _axisRange{};
    void joystickState(x86::Cpu &c, uint32_t to, uint32_t size);
    uint32_t joystickType() const; // DIDEVTYPE
    // Fills a DIEFFECTINFOA for the effect with that GUID; its DIEFT type.
    uint32_t effectInfo(x86::Cpu &c, uint32_t at, uint32_t guidData1);
    void addForceEffects(
        const std::function<std::vector<Method>(const std::string &, std::function<bool(uint32_t)>)> &unknown);
    // Force feedback: the effects by object, the device's gain
    // (DIPROP_FFGAIN), auto-centring (DIPROP_AUTOCENTER), and whether
    // SendForceFeedbackCommand has paused them or turned the motors off.
    std::mutex _forceMutex;
    std::map<uint32_t, DiEffect> _effects;
    uint32_t _forceGain = 10000;
    bool _autoCentre = true;
    bool _forcePaused = false;
    bool _actuatorsOff = false;
    uint32_t _effectVtable = 0;

    // The game's sockets (winsock.cpp): Winsock's handle -> ours, and how
    // the game has it.
    struct WsSocket {
        int socket = -1;
        bool stream = false;
        bool nonBlocking = false;
        bool bound = false;
        bool ipx = false; // an IPX datagram socket, carried over UDP (winsock.cpp)
    };
    std::map<uint32_t, WsSocket> _wsSockets;
    uint32_t _nextWsSocket = 0x100;
    std::map<uint32_t, uint32_t> _wsLastError; // by thread
    bool _wsStarted = false;
    std::FILE *_netLog = nullptr;
    void logTraffic(bool sent, uint32_t handle, const net::Address &peer, const uint8_t *data, size_t size,
                    bool dropped = false);
    // Whether this packet sent is one Options::netDrop loses.
    bool dropOutgoing();
    uint64_t _netSent = 0;  // packets sent since netDropFrom
    uint32_t _wsBuffer = 0; // inet_ntoa's and gethostbyname's results
    int _mouseDx = 0;
    int _mouseDy = 0;
    bool _mouseButton = false;
    // SetEventNotification's handles
    uint32_t _keyboardEvent = 0;
    uint32_t _mouseEvent = 0;
    uint32_t _inputSequence = 0;
    void applyInput();

    // Native modules for LoadLibraryA: lower-case name -> handle, and each
    // handle's exports.
    std::map<std::string, uint32_t> _nativeModules;
    std::map<uint32_t, std::map<std::string, uint32_t>> _nativeExports;
    void addNativeModule(const std::string &name, uint32_t handle);
    void addExport(uint32_t module, const std::string &name, int argBytes, std::function<uint32_t(x86::Cpu &)> fn);

    ThrashStats _thrash;
    // instructions run at the last page flip
    uint64_t _lastFlip = 0;
    uint64_t _lastFlipRenderer = 0;
    // The game's own DLLs, loaded on LoadLibraryA: lower-case name -> module.
    std::map<std::string, const x86::Win32Process::Module *> _gameDlls;
    uint32_t _thrashBase = 0;
    uint32_t wrapThrash(const std::string &name, uint32_t address);
    std::unique_ptr<NativeThrash> _native;
    uint32_t _thrashWindow = 2; // THRASH_window's choice: 1 the front buffer, 2 the back
    // Each window's buffer, as THRASH_lockwindow reports it: address, pitch,
    // width, height (the renderer draws there; a page flip shows it).
    std::map<uint32_t, std::array<uint32_t, 4>> _windowBuffers;
    // Does a THRASH call natively if it can (`result`: what it returns).
    bool nativeDraw(const std::string &name, const std::vector<uint32_t> &args, uint32_t &result);
    // The retail softtria.dll, known well enough that its page flips and
    // clears are done natively too (thrash.md): where it was loaded, less
    // where it asks to be; unset for any other renderer.
    bool _softtriaKnown = false;
    uint32_t softtria(uint32_t address) const; // one of its globals, by its own address
    // A frame shown straight from a 16-bit 5:6:5 buffer's rectangle.
    void presentBuffer(uint32_t buffer, int pitch, int x0, int y0, int width, int height);

    // DirectDraw's surfaces and palettes (directx.cpp), by object address.
    struct DdSurface {
        int width = 0;
        int height = 0;
        int bits = 16;
        int pitch = 0;
        uint32_t memory = 0;
        uint32_t back = 0;
        uint32_t caps = 0;
        uint32_t palette = 0;
        uint32_t colorKey = 0;
        bool primary = false;
        bool hasColorKey = false;
    };
    std::map<uint32_t, DdSurface> _surfaces;
    std::map<uint32_t, std::array<uint32_t, 256>> _palettes; // RGBA entries
    uint32_t _surfaceVtable = 0;
    uint32_t _paletteVtable = 0;
    uint32_t _clipperVtable = 0;
    int _modeWidth = 640;
    int _modeHeight = 480;
    int _modeBits = 16;
    uint32_t newSurface(int width, int height, int bits, uint32_t caps);
    void present(const DdSurface &s);
    Surface _frame;

    // DirectSound's buffers: played in time, not heard yet.
    struct DsBuffer {
        uint32_t memory = 0;
        uint32_t size = 0;
        int rate = 22050;
        int blockAlign = 4;
        int channels = 2;
        int bits = 16;
        bool primary = false;
        bool playing = false;
        bool looping = false;
        uint64_t startMs = 0;
        uint32_t startPosition = 0;
        // hundredths of a decibel
        int32_t volume = 0;
        int32_t pan = 0;
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
        Ds3dSource place;
        Ds3dSource deferredPlace;
        uint32_t position(uint64_t nowMs);
    };
    // Sound memory, by address: how many buffers share it (a buffer and its
    // duplicates), so it's freed with the last.
    std::map<uint32_t, int> _soundMemoryUsers;
    // IDirectSound3DBuffer objects -> the sound buffer each places.
    std::map<uint32_t, uint32_t> _sound3dBuffers;
    uint32_t _sound3dBufferVtable = 0;
    uint32_t _listenerObject = 0;
    // The listener (the primary buffer's IDirectSound3DListener), as in
    // force and as deferred.
    Ds3dListener _listener;
    Ds3dListener _deferredListener;
    std::mutex _soundMutex; // soundBuffers_, between the game and the audio thread
    std::vector<int32_t> _soundMix;
    SoundStats _soundStats;
    std::vector<VoiceEnergy> *_voiceLog = nullptr; // logVoices'
    uint32_t _voiceWindowMs = 100;
    void voiceMixed(bool rear, double c, double s, uint32_t calls);
    bool _mmxVoicesHooked = false;
    void hookMmxMixers();                         // -voices with MMX: at the MMX mixers' entries (native_sound.cpp)
    std::vector<int16_t> *_soundRecord = nullptr; // recordSound's
    int _soundRecordRate = 0;
    uint64_t _soundRecordNextMs = 0; // when the next step is due
    std::map<uint32_t, DsBuffer> _soundBuffers;
    uint32_t _soundBufferVtable = 0;
};

} // namespace reluva::nfs
