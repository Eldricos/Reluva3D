// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/host/game_host.h"

#include "common/pe.h"
#include "emulation/x86/format.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>

namespace reluva::nfs {

namespace {

using x86::Cpu;

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

// An argument as a number, and as text where it points at some.
std::string describe(const Cpu &c, uint32_t v) {
    std::string out = hex(v);
    if (v < 0x10000)
        return out;
    const std::string text = c.string(v, 60);
    const bool printable = text.size() >= 2 && std::all_of(text.begin(), text.end(), [](char ch) {
                               return uint8_t(ch) >= 32 && uint8_t(ch) < 127;
                           });
    return printable ? out + " \"" + text + "\"" : out;
}

} // namespace

GameHost::GameHost(GameFolders folders, Options options) : _folders(std::move(folders)), _options(std::move(options)) {
    if (_options.glide) {
        // The game's own Voodoo 2 renderer, drawing through Reluva's Glide.
        _options.renderer = "voodoo2a.dll";
        _options.presentedRenderer.clear();
        _options.nativeThrash = _options.glideCompare;
        _options.gpuThrash = false;
    }
    _cpu = std::make_unique<Cpu>(_options.memoryMb << 20);
    _cpu->setInstructionLimit(_options.instructionLimit);
    _cpu->reportMmx = _options.mmx;
    _cpu->exactX87 = true; // its memcpy copies 8 bytes at a time through FILD and FISTP
    // An EXE is loaded (0x400000 kept clear for it); its threads wait on
    // each other, so they run on host threads of their own.
    _process = std::make_unique<x86::Win32Process>(*_cpu, "nfs3.exe", true);
    _process->useHostThreads();
    _process->output = [this](const std::string &s) { say("[program] " + s); };
    if (_options.instructionsPerMs) {
        const uint64_t perMs = _options.instructionsPerMs;
        _process->milliseconds = [this, perMs] { return uint32_t(_cpu->executed() / perMs + _idleMs); };
        _process->idleTick = [this] { ++_idleMs; };
    } else if (_options.timeScale != 1.0) {
        const auto start = std::chrono::steady_clock::now();
        const double scale = _options.timeScale;
        _process->milliseconds = [start, scale] {
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            return uint32_t(ms * scale);
        };
    }
    // The time stamp counter of a 300 MHz processor (Options::cpuMhz), by
    // the game's clock. The game times it to set its detail levels on a
    // first run (by MHz: above 266 is the highest); counting instructions it
    // found 60 MHz and turned the horizon off (docs/engines/nfs/games/nfs3/PLAN.md, Sky and
    // fog).
    _cpu->timeStamp = [this, last = uint64_t(0), perMs = uint64_t(_options.cpuMhz) * 1000]() mutable {
        last = std::max(last + 1, uint64_t(_process->now()) * perMs);
        return last;
    };
    _process->fileOpened = [this](const std::string &path, bool found) {
        if (_options.callLog)
            say(std::string(found ? "  file " : "  no file ") + path);
    };
    // C: the install folder, with what the game writes kept apart; D: the CD.
    x86::Win32Process::Drive c;
    c.letter = 'C';
    c.root = _folders.install;
    c.writable = _options.saveFolder;
    c.label = "NFS3";
    _process->addDrive(c);
    if (!_folders.cd.empty()) {
        x86::Win32Process::Drive d;
        d.letter = 'D';
        d.root = _folders.cd;
        d.type = 5;
        d.label = "NFS3";
        d.serial = 0x1998;
        _process->addDrive(d);
    }
    _process->setProgram("nfs3.exe", "C:\\");
    _process->setCommandLine(_options.commandLine);
    // Before each native function: input that's due, and the log. Kept
    // cheap: the game makes millions of these calls.
    _cpu->nativeTrace = [this](Cpu &cpu, const std::string &name) {
        if (_inputQueued.load(std::memory_order_relaxed))
            applyInput();
        if (_soundRecord) {
            const uint64_t now = _process->now();
            if (now >= _soundRecordNextMs)
                recordSoundDue(now);
        }
        if (_logged >= _options.callLog)
            return;
        const bool routine = name.find("CriticalSection") != std::string::npos ||
                             name.find("Tls") != std::string::npos || name.ends_with("GetTickCount") ||
                             name.ends_with("timeGetTime");
        if (!routine) {
            ++_logged;
            std::string line = "  " + name + "(";
            for (int i = 0; i < 4; ++i)
                line += (i ? ", " : "") + describe(cpu, cpu.arg(i));
            say(line + ") from " + hex(cpu.read32(cpu.reg[Cpu::ESP])) + " [thread " +
                std::to_string(_process->currentThread() ? _process->currentThread()->id : 0) + "]");
        }
    };
    addSystem();
    addUser();
    addNetwork();
    addSerial();
    addDirectX();
    addDirectSound();
    if (_options.glide)
        addGlide();
    if (_options.nativeThrash) {
        _native = std::make_unique<NativeThrash>(*_cpu);
        _native->setRecording(_options.gpuThrash);
        _native->setAlsoRasterize(_options.gpuThrash && _options.gpuAndSoftware);
        _native->setPointSampled(_options.pointSampled);
        _native->setDirect3D(lower(_options.presentedRenderer) == "d3da.dll");
    }
}

GameHost::~GameHost() {
    if (_netLog)
        std::fclose(_netLog);
}

namespace {

// Keys by name: Windows' virtual-key code and the keyboard's scan code (as
// DirectInput numbers them).
struct KeyName {
    const char *name;
    uint8_t vk;
    uint8_t scan;
};
constexpr KeyName kKeys[] = {
    {"enter", 0x0d, 0x1c},
    {"esc", 0x1b, 0x01},
    {"space", 0x20, 0x39},
    {"tab", 0x09, 0x0f},
    {"up", 0x26, 0xc8},
    {"down", 0x28, 0xd0},
    {"left", 0x25, 0xcb},
    {"right", 0x27, 0xcd},
    {"backspace", 0x08, 0x0e},
    {"shift", 0x10, 0x2a},
    {"ctrl", 0x11, 0x1d},
    {"alt", 0x12, 0x38},
    {"f1", 0x70, 0x3b},
    {"f2", 0x71, 0x3c},
    {"f3", 0x72, 0x3d},
    {"f4", 0x73, 0x3e},
    {"a", 'A', 0x1e},
    {"b", 'B', 0x30},
    {"c", 'C', 0x2e},
    {"d", 'D', 0x20},
    {"e", 'E', 0x12},
    {"f", 'F', 0x21},
    {"g", 'G', 0x22},
    {"h", 'H', 0x23},
    {"i", 'I', 0x17},
    {"j", 'J', 0x24},
    {"k", 'K', 0x25},
    {"l", 'L', 0x26},
    {"m", 'M', 0x32},
    {"n", 'N', 0x31},
    {"o", 'O', 0x18},
    {"p", 'P', 0x19},
    {"q", 'Q', 0x10},
    {"r", 'R', 0x13},
    {"s", 'S', 0x1f},
    {"t", 'T', 0x14},
    {"u", 'U', 0x16},
    {"v", 'V', 0x2f},
    {"w", 'W', 0x11},
    {"x", 'X', 0x2d},
    {"y", 'Y', 0x15},
    {"z", 'Z', 0x2c},
    {"1", '1', 0x02},
    {"2", '2', 0x03},
    {"3", '3', 0x04},
    {"4", '4', 0x05},
    {"5", '5', 0x06},
    {"6", '6', 0x07},
    {"7", '7', 0x08},
    {"8", '8', 0x09},
    {"9", '9', 0x0a},
    {"0", '0', 0x0b},
    {"f5", 0x74, 0x3f},
    {"f6", 0x75, 0x40},
    {"f7", 0x76, 0x41},
    {"f8", 0x77, 0x42},
    {"f9", 0x78, 0x43},
    {"f10", 0x79, 0x44},
    {"home", 0x24, 0xc7},
    {"end", 0x23, 0xcf},
    {"pageup", 0x21, 0xc9},
    {"pagedown", 0x22, 0xd1},
    {"insert", 0x2d, 0xd2},
    {"delete", 0x2e, 0xd3},
    // The keypad's digits (Num Lock off: the game reads their scan codes),
    // its default steering, gas and brake.
    {"kp0", 0x60, 0x52},
    {"kp1", 0x61, 0x4f},
    {"kp2", 0x62, 0x50},
    {"kp3", 0x63, 0x51},
    {"kp4", 0x64, 0x4b},
    {"kp5", 0x65, 0x4c},
    {"kp6", 0x66, 0x4d},
    {"kp7", 0x67, 0x47},
    {"kp8", 0x68, 0x48},
    {"kp9", 0x69, 0x49},
    // Punctuation (OEM keys, a US keyboard's), for typing addresses and names.
    {".", 0xbe, 0x34},
    {",", 0xbc, 0x33},
    {"-", 0xbd, 0x0c},
    {"/", 0xbf, 0x35},
    {";", 0xba, 0x27},
};

// The character a key types, as TranslateMessage gives it (no shift): 0
// for none.
// Queues an event at its time: after those due no later, before those due
// after it, so a script's events needn't be written in time order.
template<typename Queue, typename Event>
void queueInOrder(Queue &queue, Event event) {
    auto at = queue.end();
    while (at != queue.begin() && std::prev(at)->atMs > event.atMs)
        --at;
    queue.insert(at, event);
}

char keyChar(const KeyName &k) {
    if (k.name[0] && !k.name[1] && k.name[0] >= 0x20 && k.name[0] < 0x7f)
        return k.name[0];
    return k.vk == 0x20 ? ' ' : k.vk == 0x0d ? '\r' : k.vk == 0x08 ? '\b' : 0;
}

} // namespace

bool GameHost::queueKey(const std::string &name, bool down, uint64_t atMs) {
    for (const KeyName &k : kKeys)
        if (lower(name) == k.name) {
            std::lock_guard<std::mutex> lock(_inputMutex);
            queueInOrder(_pendingKeys, KeyEvent{k.vk, k.scan, down, atMs, keyChar(k)});
            _inputQueued = true;
            return true;
        }
    return false;
}

void GameHost::queueMouseMove(int dx, int dy, uint64_t atMs) {
    std::lock_guard<std::mutex> lock(_inputMutex);
    if (dx)
        queueInOrder(_pendingMouse, MouseEvent{0, uint32_t(dx), atMs});
    if (dy)
        queueInOrder(_pendingMouse, MouseEvent{4, uint32_t(dy), atMs});
    _inputQueued = true;
}

void GameHost::setJoystick(const GamepadState &state, uint64_t atMs) {
    std::lock_guard<std::mutex> lock(_inputMutex);
    _pendingJoystick.emplace_back(atMs, state);
    _inputQueued = true;
}

void GameHost::queueMouseButton(bool down, uint64_t atMs) {
    std::lock_guard<std::mutex> lock(_inputMutex);
    queueInOrder(_pendingMouse, MouseEvent{12, down ? 0x80u : 0u, atMs});
    _inputQueued = true;
}

std::map<std::string, uint64_t> GameHost::callCounts() const {
    std::map<std::string, uint64_t> out;
    for (auto &[name, count] : _cpu->nativeCallCounts())
        out[name] += count;
    return out;
}

void GameHost::applyInput() {
    std::vector<KeyEvent> due;
    std::vector<MouseEvent> mouse;
    {
        std::lock_guard<std::mutex> lock(_inputMutex);
        const uint64_t t = _process->now();
        while (!_pendingKeys.empty() && _pendingKeys.front().atMs <= t) {
            due.push_back(_pendingKeys.front());
            _pendingKeys.pop_front();
        }
        while (!_pendingJoystick.empty() && _pendingJoystick.front().first <= t) {
            _joystick = _pendingJoystick.front().second;
            _pendingJoystick.pop_front();
        }
        while (!_pendingMouse.empty() && _pendingMouse.front().atMs <= t) {
            mouse.push_back(_pendingMouse.front());
            _pendingMouse.pop_front();
        }
        _inputQueued = !_pendingKeys.empty() || !_pendingJoystick.empty() || !_pendingMouse.empty();
    }
    for (const MouseEvent &m : mouse) {
        if (m.offset == 0)
            _mouseDx += int32_t(m.data);
        else if (m.offset == 4)
            _mouseDy += int32_t(m.data);
        else
            _mouseButton = m.data != 0;
        _bufferedMouse.push_back(m);
        if (_bufferedMouse.size() > 64)
            _bufferedMouse.pop_front();
    }
    if (!mouse.empty() && _mouseEvent)
        _process->setEvent(_mouseEvent);
    if (!due.empty() && _keyboardEvent)
        _process->setEvent(_keyboardEvent);
    for (const KeyEvent &e : due) {
        _keys[e.scan] = e.down ? 0x80 : 0;
        _vkDown[e.vk] = e.down;
        _bufferedKeys.push_back(e);
        if (_bufferedKeys.size() > 64)
            _bufferedKeys.pop_front();
        // WM_KEYDOWN / WM_KEYUP to the game's window: the scan code in
        // bits 16-23, the previous state and transition bits for a release.
        if (!_windows.empty()) {
            const uint32_t lParam =
                1u | uint32_t(e.scan & 0x7f) << 16 | (e.scan & 0x80 ? 1u << 24 : 0u) | (e.down ? 0u : 0xc0000000u);
            _queues[_windows.front().threadId].push_back(
                {_windows.front().handle, e.down ? 0x100u : 0x101u, e.vk, lParam, uint32_t(_process->now())});
            if (e.down && e.ch) // WM_CHAR, as TranslateMessage would make
                _queues[_windows.front().threadId].push_back(
                    {_windows.front().handle, 0x102u, uint32_t(uint8_t(e.ch)), lParam, uint32_t(_process->now())});
        }
    }
}

void GameHost::provide(const char *dll, const char *name, int args, std::function<uint32_t(Cpu &)> fn) {
    const uint32_t pop = uint32_t(args) * 4;
    const uint32_t address = _process->provide(dll, name, [fn = std::move(fn), pop](Cpu &c) {
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    });
    // LoadLibraryA of a system DLL gives a module with these as its exports.
    const std::string key = lower(dll);
    if (!_nativeModules.count(key))
        addNativeModule(key, 0x6d000000u + uint32_t(_nativeModules.size()) * 0x10000u);
    _nativeExports[_nativeModules[key]][name] = address;
}

void GameHost::addNativeModule(const std::string &name, uint32_t handle) {
    _nativeModules[lower(name)] = handle;
}

void GameHost::addExport(uint32_t module, const std::string &name, int argBytes, std::function<uint32_t(Cpu &)> fn) {
    const uint32_t pop = uint32_t(argBytes);
    _nativeExports[module][name] = _cpu->addNative(name, [fn = std::move(fn), pop](Cpu &c) {
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    });
}

Error GameHost::load() {
    const std::filesystem::path path = _folders.install / "nfs3.exe";
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Can't read " + path.string());
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
    if (Error e = _process->load("nfs3.exe", file, _exe))
        return e;
    say("nfs3.exe loaded at " + hex(_exe->base));
    for (const std::string &m : _process->missingImports())
        say("  not stood in for: " + m);
    if (_options.nativeRoutines) {
        addNativeRoutines();
        addNativeSound();
    }
    if (_options.positionalUpdates)
        choosePositionalUpdates();
    return Error::success();
}

void GameHost::choosePositionalUpdates() {
    // The network dialog's set-up 0x43f320 starts 0x6fd50c (Positional
    // rather than Controller Updates) at 0, from edx, in its kinds 0 and 1
    // (0x43f43e, 0x43f451), and its handler can only clear it (0x43f927),
    // as no dialog shows that choice (net.md, Positional updates). Both
    // stores are made to write 1 from the register holding 1 there (esi,
    // eax), where the code is as expected; kind 2 is left as it is.
    const uint8_t expected[6] = {0x89, 0x15, 0x0c, 0xd5, 0x6f, 0x00}; // mov [0x6fd50c], edx
    auto matches = [&](uint32_t a) {
        for (uint32_t i = 0; i < 6; ++i)
            if (_cpu->read8(a + i) != expected[i])
                return false;
        return true;
    };
    if (!matches(0x43f43e) || !matches(0x43f451)) {
        say("positional updates: nfs3.exe's network dialog isn't the expected code; left as it is");
        return;
    }
    _cpu->write8(0x43f43f, 0x35);                                     // mov [0x6fd50c], esi (1)
    const uint8_t storeEax[6] = {0xa3, 0x0c, 0xd5, 0x6f, 0x00, 0x90}; // mov [0x6fd50c], eax (1); nop
    for (uint32_t i = 0; i < 6; ++i)
        _cpu->write8(0x43f451 + i, storeEax[i]);
    say("positional updates chosen for network races");
}

bool GameHost::replaceRoutine(uint32_t address, uint32_t size, uint64_t hash, const char *name, Cpu::Native fn) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < size; ++i)
        h = (h ^ _cpu->read8(address + i)) * 0x100000001b3ull;
    if (h != hash || size < 5)
        return false;
    const uint32_t native = _cpu->addNative(name, std::move(fn));
    _cpu->write8(address, 0xe9); // JMP rel32
    _cpu->write32(address + 1, native - (address + 5));
    return true;
}

void GameHost::addNativeRoutines() {
    // Routines of the retail nfs3.exe's C library run natively, where its
    // code is as expected (docs/engines/nfs/games/nfs3/PLAN.md, Loading time): a jump to
    // the native function over the routine's first bytes. Each uses
    // Watcom's register convention (arguments in EAX, EDX...; the result
    // in EAX) and returns as the routine would.
    auto replace = [this](uint32_t address, std::vector<uint8_t> expected, const char *name, Cpu::Native fn) {
        for (size_t i = 0; i < expected.size(); ++i)
            if (_cpu->read8(address + uint32_t(i)) != expected[i])
                return;
        const uint32_t native = _cpu->addNative(name, std::move(fn));
        _cpu->write8(address, 0xe9); // JMP rel32
        _cpu->write32(address + 1, native - (address + 5));
    };
    // strcmp(EAX, EDX): -1, 0 or 1, the bytes compared unsigned.
    replace(0x4ee310, {0x53, 0x51, 0x89, 0xc3, 0x39, 0xd0, 0x74, 0x6c}, "nfs3.exe strcmp", [](Cpu &c) {
        uint32_t a = c.reg[Cpu::EAX];
        uint32_t b = c.reg[Cpu::EDX];
        int result = 0;
        for (;; ++a, ++b) {
            const uint8_t x = c.read8(a);
            const uint8_t y = c.read8(b);
            if (x != y) {
                result = x < y ? -1 : 1;
                break;
            }
            if (!x)
                break;
        }
        c.reg[Cpu::EAX] = uint32_t(result);
        return 0u;
    });
    // __CHP: ST0 rounded towards zero (frndint with the control word set to
    // chop, then put back), everything else as it was. Images are scaled
    // with it a pixel at a time as a track loads.
    replace(0x4dfd56, {0x50, 0x9b, 0xd9, 0x3c, 0x24, 0x9b, 0xff, 0x34, 0x24, 0xc6, 0x44, 0x24, 0x01, 0x1f},
            "nfs3.exe __CHP", [](Cpu &c) {
                double &top = c.fpu.regs[c.fpu.top];
                const double chopped = std::trunc(top);
                if (chopped != top || std::signbit(chopped) != std::signbit(top)) {
                    top = chopped;
                    c.fpu.exactValid &= uint8_t(~(1u << c.fpu.top)); // its exact integer, if any, no longer it
                }
                return 0u;
            });
}

Error GameHost::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail("nfs3.exe isn't loaded");
    return _process->run(*_exe, exitCode);
}

uint32_t GameHost::newObject(uint32_t vtable) {
    // The object: its vtable pointer, then room for whatever it keeps.
    const uint32_t object = _process->memory().alloc(64);
    _cpu->write32(object, vtable);
    return object;
}

uint32_t GameHost::makeVtable(const std::string &interface, const std::vector<Method> &methods) {
    const uint32_t vtable = _process->memory().alloc(uint32_t(methods.size()) * 4);
    for (size_t i = 0; i < methods.size(); ++i) {
        const Method &m = methods[i];
        const std::string name = interface + "::" + m.name;
        auto fn = m.fn;
        if (!fn)
            fn = [this, name](Cpu &) {
                say("  " + name + " isn't stood in for: E_NOTIMPL");
                return 0x80004001u;
            };
        const uint32_t pop = uint32_t(m.args) * 4;
        _cpu->write32(vtable + 4 * uint32_t(i), _cpu->addNative(name, [fn, pop](Cpu &c) {
            c.reg[Cpu::EAX] = fn(c);
            return pop;
        }));
    }
    return vtable;
}

void GameHost::addSystem() {
    const char *k = "KERNEL32.dll";
    // Native modules first: LoadLibraryA answers for them.
    provide(k, "LoadLibraryA", 1, [this](Cpu &c) -> uint32_t {
        std::string name = lower(c.string(c.arg(0)));
        const size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        // The game's own DLLs (the renderers, the sound) from its folder.
        if (auto it = _gameDlls.find(name); it != _gameDlls.end())
            return it->second->base;
        if (name.find("kernel32") == std::string::npos && name.find("user32") == std::string::npos &&
            !_nativeModules.count(name)) {
            // Told of another renderer, the game asks for it: renderer it is.
            const std::string dllFile =
                !_options.presentedRenderer.empty() && name == _options.presentedRenderer ? _options.renderer : name;
            // Options::glideDll: the renderer from outside the install.
            const bool outside = _options.glide && !_options.glideDll.empty() && dllFile == _options.renderer;
            std::ifstream in(outside ? std::filesystem::path(_options.glideDll)
                                     : _process->hostPath("C:\\" + dllFile, false),
                             std::ios::binary);
            if (in) {
                const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
                const x86::Win32Process::Module *m = nullptr;
                if (Error e = _process->load(name, file, m)) {
                    say("  LoadLibraryA(\"" + name + "\"): " + e.message);
                    return 0u;
                }
                _gameDlls[name] = m;
                if (name.find("eacsnd") == std::string::npos) {
                    _thrash.dll = name;
                    _thrashBase = m->base;
                    // The retail softtria.dll (June 1998), whose globals thrash.md describes.
                    _softtriaKnown = dllFile == "softtria.dll" && m->image.timestamp == 0x35e4344f &&
                                     m->image.imageBase == 0x60000000;
                }
                say("  " + name + (dllFile != name ? " (" + dllFile + ")" : std::string()) + " loaded at " +
                    hex(m->base));
                if (Error e = _process->attach(*m)) {
                    say("  " + name + ": " + e.message);
                    return 0u;
                }
                return m->base;
            }
        }
        if (auto it = _nativeModules.find(name); it != _nativeModules.end())
            return it->second;
        if (const auto *m = _process->module(name))
            return m->base;
        say("  LoadLibraryA(\"" + name + "\"): not there");
        _process->setLastError(126);
        return 0u;
    });
    provide(k, "FreeLibrary", 1, [](Cpu &) { return 1u; });
    // CompareString(locale, flags, a, aLength, b, bLength): 1, 2 or 3 for
    // less, equal, greater, by code unit (case folded for
    // NORM_IGNORECASE). The C library of the CD's Voodoo3 patch DLL asks.
    auto compareStrings = [](Cpu &c, int unit) -> uint32_t {
        auto read = [&](uint32_t at, int32_t n) {
            std::vector<uint32_t> s;
            for (int32_t i = 0; n < 0 || i < n; ++i) {
                const uint32_t ch = unit == 1 ? c.read8(at + uint32_t(i)) : c.read16(at + 2 * uint32_t(i));
                if (n < 0 && !ch)
                    break;
                s.push_back((c.arg(1) & 1) && ch < 0x80 ? uint32_t(std::tolower(int(ch))) : ch);
            }
            return s;
        };
        const std::vector<uint32_t> a = read(c.arg(2), int32_t(c.arg(3)));
        const std::vector<uint32_t> b = read(c.arg(4), int32_t(c.arg(5)));
        return a < b ? 1u : a == b ? 2u : 3u;
    };
    provide(k, "CompareStringA", 6, [compareStrings](Cpu &c) { return compareStrings(c, 1); });
    provide(k, "CompareStringW", 6, [compareStrings](Cpu &c) { return compareStrings(c, 2); });
    provide(k, "IsProcessorFeaturePresent", 1, [](Cpu &) { return 0u; });
    provide(k, "GetProcAddress", 2, [this](Cpu &c) -> uint32_t {
        const uint32_t handle = c.arg(0);
        const uint32_t name = c.arg(1);
        const std::string function = name < 0x10000 ? "#" + std::to_string(name) : c.string(name);
        if (const auto *m = _process->moduleAt(handle)) {
            for (const PeImage::Export &e : m->image.exports)
                if ((name < 0x10000 ? e.ordinal == name : e.name == function) && e.forwarder.empty())
                    return handle == _thrashBase ? wrapThrash(function, m->base + e.rva) : m->base + e.rva;
            say("  GetProcAddress: " + m->name + " has no " + function);
            return 0u;
        }
        if (auto it = _nativeExports.find(handle); it != _nativeExports.end()) {
            auto f = it->second.find(function);
            if (f != it->second.end())
                return f->second;
            say("  GetProcAddress: no " + function);
            return 0u;
        }
        _process->setLastError(127);
        say("  GetProcAddress(" + hex(handle) + ", " + function + "): not there");
        return 0u;
    });

    // Windows 98, which the game was made for (4.10, build 1998): it then
    // reads low memory to tell, as that allows.
    _cpu->readableFirstPage = true;
    provide(k, "GetVersion", 0, [](Cpu &) { return 0xc0000a04u; });
    provide(k, "GetVersionExA", 1, [](Cpu &c) {
        const uint32_t p = c.arg(0);
        if (c.read32(p) < 148)
            return 0u;
        c.write32(p + 4, 4);
        c.write32(p + 8, 10);
        c.write32(p + 12, 0x040a07ce); // the build, with the version above it
        c.write32(p + 16, 1);          // VER_PLATFORM_WIN32_WINDOWS
        c.writeString(p + 20, " A ", 128);
        return 1u;
    });

    // The machine: a Pentium of the time, with 64 MB.
    provide(k, "GetSystemInfo", 1, [](Cpu &c) {
        const uint32_t p = c.arg(0);
        const uint32_t v[] = {0, 4096, 0x10000, 0x7ffeffff, 1, 1, 586, 0x10000};
        for (uint32_t i = 0; i < 8; ++i)
            c.write32(p + 4 * i, v[i]);
        c.write16(p + 32, 5);
        c.write16(p + 34, 0);
        return 0u;
    });
    provide(k, "GlobalMemoryStatus", 1, [](Cpu &c) {
        const uint32_t p = c.arg(0);
        const uint32_t v[] = {32, 20, 64u << 20, 48u << 20, 128u << 20, 100u << 20, 0x7ffe0000, 0x7f000000};
        for (uint32_t i = 0; i < 8; ++i)
            c.write32(p + 4 * i, v[i]);
        return 0u;
    });
    provide(k, "GetLocalTime", 1, [](Cpu &c) {
        const std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        const uint16_t v[] = {
            uint16_t(tm.tm_year + 1900), uint16_t(tm.tm_mon + 1), uint16_t(tm.tm_wday), uint16_t(tm.tm_mday),
            uint16_t(tm.tm_hour),        uint16_t(tm.tm_min),     uint16_t(tm.tm_sec),  0};
        for (uint32_t i = 0; i < 8; ++i)
            c.write16(c.arg(0) + 2 * i, v[i]);
        return 0u;
    });
    provide(k, "GetTimeZoneInformation", 1, [](Cpu &c) {
        for (uint32_t i = 0; i < 172; i += 4)
            c.write32(c.arg(0) + i, 0);
        return 0u; // TIME_ZONE_ID_UNKNOWN
    });
    // VirtualQuery(address, info*, size): committed, read-write memory.
    provide(k, "VirtualQuery", 3, [](Cpu &c) {
        const uint32_t a = c.arg(0) & ~0xfffu;
        const uint32_t p = c.arg(1);
        const uint32_t v[] = {a, a, 4, 0x1000, 0x1000, 4, 0x20000};
        for (uint32_t i = 0; i < 7; ++i)
            c.write32(p + 4 * i, v[i]);
        return 28u;
    });
    provide(k, "DebugBreak", 0, [](Cpu &c) {
        c.abort("the program called DebugBreak");
        return 0u;
    });
    provide(k, "GetCommandLineW", 0, [this](Cpu &c) {
        static uint32_t wide = 0;
        if (!wide) {
            const std::string s = _options.commandLine;
            wide = _process->memory().alloc(uint32_t(s.size() + 1) * 2);
            for (size_t i = 0; i < s.size(); ++i)
                c.write16(wide + 2 * uint32_t(i), uint8_t(s[i]));
        }
        return wide;
    });
    provide(k, "GetModuleFileNameW", 3, [](Cpu &c) {
        const std::string s = "C:\\nfs3.exe";
        const uint32_t n = std::min<uint32_t>(uint32_t(s.size()), c.arg(2) ? c.arg(2) - 1 : 0);
        for (uint32_t i = 0; i < n; ++i)
            c.write16(c.arg(1) + 2 * i, uint8_t(s[i]));
        c.write16(c.arg(1) + 2 * n, 0);
        return n;
    });
    provide(k, "SetEnvironmentVariableA", 2, [](Cpu &) { return 1u; });
    provide(k, "SetEnvironmentVariableW", 2, [](Cpu &) { return 1u; });
    // No console: the game is a window.
    for (const char *name : {"GetConsoleMode", "SetConsoleMode"})
        provide(k, name, 2, [](Cpu &) { return 0u; });
    // The console's keyboard: never anything waiting, and saying so (the C
    // library's kbhit, as the game drains the keyboard when it ends, reads
    // the count of events; left unwritten it was taken as a key waiting
    // for ever).
    auto noInput = [](Cpu &c) {
        if (c.arg(3))
            c.write32(c.arg(3), 0);
        return 1u;
    };
    provide(k, "PeekConsoleInputA", 4, noInput);
    provide(k, "ReadConsoleInputA", 4, noInput);
    provide(k, "WriteConsoleA", 5, [this](Cpu &c) {
        say("[console] " + std::string(reinterpret_cast<const char *>(c.memory().data() + c.arg(1)),
                                       std::min<uint32_t>(c.arg(2), 4096)));
        if (c.arg(3))
            c.write32(c.arg(3), c.arg(2));
        return 1u;
    });

    // Display settings: GDI32.
    provide("GDI32.dll", "GetDeviceCaps", 2, [](Cpu &c) -> uint32_t {
        switch (c.arg(1)) {
        case 8:
            return 640; // HORZRES
        case 10:
            return 480; // VERTRES
        case 12:
            return 16; // BITSPIXEL
        case 14:
            return 1; // PLANES
        case 116:
            return 60; // VREFRESH
        default:
            return 0;
        }
    });
    provide("GDI32.dll", "GetStockObject", 1, [](Cpu &c) { return 0x50000u + c.arg(0); });

    // The registry: the key the game's installer writes, with the
    // renderer chosen (as 3dSetup would set it). The game only reads it.
    struct Value {
        uint32_t type; // 1 REG_SZ, 4 REG_DWORD
        std::string text;
        uint32_t number = 0;
    };
    auto registry = std::make_shared<std::map<std::string, std::map<std::string, Value>>>();
    (*registry)["software\\electronic arts\\need for speed iii"] = {
        {"thrash driver", {1, _options.presentedRenderer.empty() ? _options.renderer : _options.presentedRenderer}},
        {"d3d device", {4, "", 0}},
    };
    // For d3da, the Direct3D device 3dSetup would have found: the game reads
    // its description only to look for two cards' names (0x472d10: "STB
    // Velocity 128" halves the effects intensity's default; 0x4a4410, the
    // menus' "texteffects", is left out for "RAGE PRO"), so a plain one
    // gives the general path.
    if (lower(_options.presentedRenderer.empty() ? _options.renderer : _options.presentedRenderer) == "d3da.dll")
        (*registry)["software\\electronic arts\\need for speed iii"]["3d device description"] = {1, "Direct3D HAL"};
    auto keys = std::make_shared<std::map<uint32_t, std::string>>();
    const char *a = "ADVAPI32.dll";
    provide(a, "RegOpenKeyExA", 5, [this, registry, keys](Cpu &c) {
        const std::string name = lower(c.string(c.arg(1)));
        if (!registry->count(name)) {
            say("  registry: no key " + c.string(c.arg(1)));
            return 2u; // ERROR_FILE_NOT_FOUND
        }
        const uint32_t handle = 0x80001000u + uint32_t(keys->size()) * 4;
        (*keys)[handle] = name;
        c.write32(c.arg(4), handle);
        return 0u;
    });
    // RegQueryValueExA(key, name, reserved, type*, data*, size*)
    provide(a, "RegQueryValueExA", 6, [this, registry, keys](Cpu &c) {
        auto k = keys->find(c.arg(0));
        if (k == keys->end())
            return 6u; // ERROR_INVALID_HANDLE
        const auto &values = (*registry)[k->second];
        auto v = values.find(lower(c.string(c.arg(1))));
        if (v == values.end()) {
            say("  registry: no value " + c.string(c.arg(1)));
            return 2u;
        }
        std::vector<uint8_t> data;
        if (v->second.type == 1) {
            data.assign(v->second.text.begin(), v->second.text.end());
            data.push_back(0);
        } else {
            for (int i = 0; i < 4; ++i)
                data.push_back(uint8_t(v->second.number >> (8 * i)));
        }
        if (c.arg(3))
            c.write32(c.arg(3), v->second.type);
        const uint32_t room = c.arg(5) ? c.read32(c.arg(5)) : 0;
        if (c.arg(5))
            c.write32(c.arg(5), uint32_t(data.size()));
        if (!c.arg(4))
            return 0u;
        if (room < data.size())
            return 234u; // ERROR_MORE_DATA
        for (size_t i = 0; i < data.size(); ++i)
            c.write8(c.arg(4) + uint32_t(i), data[i]);
        return 0u;
    });
    provide(a, "RegCloseKey", 1, [keys](Cpu &c) {
        keys->erase(c.arg(0));
        return 0u;
    });
    provide(a, "RegFlushKey", 1, [](Cpu &) { return 0u; });

    // File versions, from the files' version resources. The size asked for
    // is twice the resource's: VerQueryValueA writes text values, converted
    // to ANSI, into the second half, as Windows NT does.
    auto versionInfo = [this](const std::string &windowsPath) {
        std::ifstream in(_process->hostPath(windowsPath, false), std::ios::binary);
        const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), {});
        PeImage pe;
        if (file.empty() || parsePe(file, pe))
            return std::vector<uint8_t>{};
        return findResource(pe, file, 16);
    };
    provide("VERSION.dll", "GetFileVersionInfoSizeA", 2, [versionInfo](Cpu &c) {
        if (c.arg(1))
            c.write32(c.arg(1), 0);
        return uint32_t(versionInfo(c.string(c.arg(0))).size() * 2);
    });
    provide("VERSION.dll", "GetFileVersionInfoA", 4, [versionInfo](Cpu &c) {
        const std::vector<uint8_t> v = versionInfo(c.string(c.arg(0)));
        if (v.empty() || !c.valid(c.arg(3), c.arg(2)))
            return 0u;
        std::memcpy(c.memory().data() + c.arg(3), v.data(), std::min<size_t>(v.size(), c.arg(2)));
        return 1u;
    });
    // VerQueryValueA(block, path, value**, length*): the tree's nodes are
    // length, value length, type (1 text), a UTF-16 key, the value and the
    // children, each 4-byte aligned.
    provide("VERSION.dll", "VerQueryValueA", 4, [](Cpu &c) {
        const uint32_t block = c.arg(0);
        std::string path = c.string(c.arg(1));
        std::vector<std::string> want;
        for (size_t at = 0; at < path.size();) {
            const size_t slash = path.find('\\', at);
            const size_t end = slash == std::string::npos ? path.size() : slash;
            if (end > at)
                want.push_back(lower(path.substr(at, end - at)));
            at = end + 1;
        }
        auto align = [](uint32_t v) { return (v + 3) & ~3u; };
        auto key = [&](uint32_t node, uint32_t &end) {
            std::string k;
            uint32_t p = node + 6;
            for (uint16_t ch; (ch = c.read16(p)) != 0; p += 2)
                k.push_back(char(ch < 128 ? ch : '?'));
            end = align(p + 2);
            return lower(k);
        };
        uint32_t node = block;
        uint32_t valueAt = 0;
        key(node, valueAt);
        for (const std::string &part : want) {
            const uint32_t length = c.read16(node);
            const uint32_t valueBytes = c.read16(node + 2) * (c.read16(node + 4) == 1 ? 2u : 1u);
            uint32_t child = align(valueAt + valueBytes);
            uint32_t found = 0;
            while (child < node + length && c.read16(child)) {
                uint32_t childValue = 0;
                if (key(child, childValue) == part) {
                    found = child;
                    valueAt = childValue;
                    break;
                }
                child = align(child + c.read16(child));
            }
            if (!found)
                return 0u;
            node = found;
        }
        const uint32_t valueLength = c.read16(node + 2);
        if (c.read16(node + 4) == 1) {
            // Text: converted into the second half of the block.
            const uint32_t to = block + c.read16(block) + (node - block);
            uint32_t n = 0;
            for (uint16_t ch; n + 1 < valueLength * 2 && (ch = c.read16(valueAt + 2 * n)) != 0; ++n)
                c.write8(to + n, uint8_t(ch < 256 ? ch : '?'));
            c.write8(to + n, 0);
            c.write32(c.arg(2), to);
            c.write32(c.arg(3), n + 1);
        } else {
            c.write32(c.arg(2), valueAt);
            c.write32(c.arg(3), valueLength);
        }
        return valueLength ? 1u : 0u;
    });
    provide("SHELL32.dll", "ShellExecuteA", 6, [this](Cpu &c) {
        say("  ShellExecuteA(" + c.string(c.arg(2)) + "): not done");
        return 2u;
    });
    provide("ole32.dll", "CoCreateGuid", 1, [](Cpu &c) {
        static std::mt19937 random(1998);
        for (uint32_t i = 0; i < 16; i += 4)
            c.write32(c.arg(0) + i, random());
        return 0u;
    });
}

uint32_t GameHost::callWindow(const Window &w, uint32_t msg, uint32_t wParam, uint32_t lParam) {
    uint32_t result = 0;
    if (Error e = _cpu->call(w.wndProc, {w.handle, msg, wParam, lParam}, result))
        _cpu->abort("the window procedure failed: " + e.message);
    return result;
}

GameHost::Window *GameHost::window(uint32_t handle) {
    for (Window &w : _windows)
        if (w.handle == handle)
            return &w;
    return nullptr;
}

void GameHost::post(uint32_t threadId, const Message &m) {
    _queues[threadId].push_back(m);
    _process->preempt();
}

void GameHost::addUser() {
    const char *u = "USER32.dll";
    auto self = [this]() { return _process->currentThread() ? _process->currentThread()->id : 0x104u; };

    // RegisterClassA(WNDCLASSA*): the procedure at +4, the name at +36.
    provide(u, "RegisterClassA", 1, [this](Cpu &c) {
        const uint32_t p = c.arg(0);
        const std::string name = c.read32(p + 36) < 0x10000 ? hex(c.read32(p + 36)) : c.string(c.read32(p + 36));
        _classes[lower(name)] = c.read32(p + 4);
        return 0xc000u + uint32_t(_classes.size());
    });
    // CreateWindowExA(exStyle, class, title, style, x, y, w, h, parent, menu, instance, param)
    provide(u, "CreateWindowExA", 12, [this, self](Cpu &c) -> uint32_t {
        const std::string cls = c.arg(1) < 0x10000 ? hex(c.arg(1)) : c.string(c.arg(1));
        auto it = _classes.find(lower(cls));
        if (it == _classes.end()) {
            say("  CreateWindowExA: no class " + cls);
            return 0u;
        }
        Window w;
        w.handle = (_nextWindow += 4);
        w.wndProc = it->second;
        w.className = cls;
        w.title = c.string(c.arg(2));
        w.style = c.arg(3);
        w.threadId = self();
        _windows.push_back(w);
        say("  window \"" + w.title + "\" (" + cls + ") made");
        // WM_NCCREATE and WM_CREATE, with a CREATESTRUCT.
        const uint32_t cs = _process->memory().alloc(48);
        const uint32_t fields[] = {c.arg(11), c.arg(10), c.arg(9), c.arg(8), c.arg(7), c.arg(6),
                                   c.arg(5),  c.arg(4),  c.arg(3), c.arg(2), c.arg(1), c.arg(0)};
        for (uint32_t i = 0; i < 12; ++i)
            c.write32(cs + 4 * i, fields[i]);
        const Window copy = w;
        if (!callWindow(copy, 0x81, 0, cs)) {
            say("  the window refused WM_NCCREATE");
        }
        if (int32_t(callWindow(copy, 0x01, 0, cs)) == -1)
            return 0u;
        return w.handle;
    });
    provide(u, "DefWindowProcA", 4, [this](Cpu &c) -> uint32_t {
        switch (c.arg(1)) {
        case 0x81:
            return 1; // WM_NCCREATE: go ahead
        case 0x10:    // WM_CLOSE: destroy it
            if (Window *w = window(c.arg(0)))
                callWindow(*w, 0x02, 0, 0);
            return 0;
        default:
            return 0;
        }
    });
    provide(u, "DestroyWindow", 1, [this](Cpu &c) {
        if (Window *w = window(c.arg(0))) {
            const Window copy = *w;
            callWindow(copy, 0x02, 0, 0); // WM_DESTROY
            std::erase_if(_windows, [&](const Window &x) { return x.handle == copy.handle; });
        }
        return 1u;
    });
    provide(u, "ShowWindow", 2, [](Cpu &) { return 0u; });
    provide(u, "SetWindowPos", 7, [](Cpu &) { return 1u; });
    provide(u, "SetActiveWindow", 1, [](Cpu &c) { return c.arg(0); });
    provide(u, "SetForegroundWindow", 1, [](Cpu &) { return 1u; });
    // GetWindowThreadProcessId(window, processId): the thread that made it
    // (the CD's Voodoo3 patch DLL asks, to post to the window's thread).
    provide(u, "GetWindowThreadProcessId", 2, [this](Cpu &c) {
        if (c.arg(1))
            c.write32(c.arg(1), 0x100u); // GetCurrentProcessId's
        for (const Window &w : _windows)
            if (w.handle == c.arg(0))
                return w.threadId;
        return 0u;
    });
    provide(u, "IsIconic", 1, [](Cpu &) { return 0u; });
    provide(u, "FindWindowA", 2, [](Cpu &) { return 0u; }); // no other copy running
    provide(u, "GetDesktopWindow", 0, [](Cpu &) { return 0x10010u; });
    provide(u, "GetWindowDC", 1, [](Cpu &) { return 0x20000u; });
    // GetWindowLongA(hwnd, index): the procedure (-4), style (-16), user data (-21).
    provide(u, "GetWindowLongA", 2, [this](Cpu &c) -> uint32_t {
        Window *w = window(c.arg(0));
        if (!w)
            return 0;
        switch (int32_t(c.arg(1))) {
        case -4:
            return w->wndProc;
        case -16:
            return w->style;
        case -21:
            return w->userData;
        default:
            return 0;
        }
    });
    auto rect = [](Cpu &c, uint32_t p, int l, int t, int r, int b) {
        c.write32(p, uint32_t(l));
        c.write32(p + 4, uint32_t(t));
        c.write32(p + 8, uint32_t(r));
        c.write32(p + 12, uint32_t(b));
    };
    provide(u, "GetClientRect", 2, [this, rect](Cpu &c) {
        Window *w = window(c.arg(0));
        rect(c, c.arg(1), 0, 0, w ? w->width : 640, w ? w->height : 480);
        return 1u;
    });
    provide(u, "AdjustWindowRect", 3, [](Cpu &) { return 1u; });
    provide(u, "ClientToScreen", 2, [](Cpu &) { return 1u; });
    provide(u, "BeginPaint", 2, [](Cpu &c) {
        for (uint32_t i = 0; i < 64; i += 4)
            c.write32(c.arg(1) + i, 0);
        c.write32(c.arg(1), 0x20000);
        return 0x20000u;
    });
    provide(u, "EndPaint", 2, [](Cpu &) { return 1u; });

    // Messages: each thread's queue.
    auto writeMsg = [](Cpu &c, uint32_t p, const Message &m) {
        const uint32_t v[] = {m.hwnd, m.message, m.wParam, m.lParam, m.time, 320, 240};
        for (uint32_t i = 0; i < 7; ++i)
            c.write32(p + 4 * i, v[i]);
    };
    provide(u, "GetMessageA", 4, [this, self, writeMsg](Cpu &c) {
        auto &q = _queues[self()];
        if (_tapiDeliver)
            _tapiDeliver(c);
        // While a call is under way, TAPI's window gets status messages:
        // the game's loop waiting on it comes round at least every 100 ms.
        const bool tapi = _tapiNews && _tapiThread == self();
        _process->wait([&] { return !q.empty() || (tapi && _tapiNews()); }, tapi && _tapiBusy() ? 100u : 0xffffffffu);
        if (_process->ending()) {
            c.abort("the process is ending");
            return 0u;
        }
        if (q.empty() && tapi) {
            // TAPI's news: its callbacks run, and WM_NULL comes back, as
            // the message to TAPI's own window would.
            _tapiDeliver(c);
            writeMsg(c, c.arg(0), {0, 0, 0, 0, uint32_t(_process->now())});
            return 1u;
        }
        const Message m = q.front();
        q.pop_front();
        writeMsg(c, c.arg(0), m);
        return m.message == 0x12 ? 0u : 1u; // WM_QUIT
    });
    provide(u, "PeekMessageA", 5, [this, self, writeMsg](Cpu &c) {
        auto &q = _queues[self()];
        if (_tapiDeliver)
            _tapiDeliver(c);
        if (q.empty()) {
            _process->yield();
            return 0u;
        }
        writeMsg(c, c.arg(0), q.front());
        if (c.arg(4) & 1) // PM_REMOVE
            q.pop_front();
        return 1u;
    });
    provide(u, "TranslateMessage", 1, [](Cpu &) { return 0u; });
    provide(u, "DispatchMessageA", 1, [this](Cpu &c) {
        Window *w = window(c.read32(c.arg(0)));
        if (!w)
            return 0u;
        const Window copy = *w;
        return callWindow(copy, c.read32(c.arg(0) + 4), c.read32(c.arg(0) + 8), c.read32(c.arg(0) + 12));
    });
    provide(u, "PostMessageA", 4, [this](Cpu &c) {
        Window *w = window(c.arg(0));
        post(w ? w->threadId : 0x104u, {c.arg(0), c.arg(1), c.arg(2), c.arg(3), uint32_t(_process->now())});
        return 1u;
    });
    provide(u, "SendNotifyMessageA", 4, [this, self](Cpu &c) {
        Window *w = window(c.arg(0));
        if (w && w->threadId == self()) {
            const Window copy = *w;
            callWindow(copy, c.arg(1), c.arg(2), c.arg(3));
        } else {
            post(w ? w->threadId : 0x104u, {c.arg(0), c.arg(1), c.arg(2), c.arg(3), uint32_t(_process->now())});
        }
        return 1u;
    });
    provide(u, "PostThreadMessageA", 4, [this](Cpu &c) {
        post(c.arg(0), {0, c.arg(1), c.arg(2), c.arg(3), uint32_t(_process->now())});
        return 1u;
    });
    provide(u, "PostQuitMessage", 1, [this, self](Cpu &c) {
        post(self(), {0, 0x12, c.arg(0), 0, uint32_t(_process->now())});
        return 0u;
    });

    // Input: keys from Reluva's events (none yet), the cursor.
    provide(u, "GetAsyncKeyState", 1, [this](Cpu &c) { return _vkDown[c.arg(0) & 0xff] ? 0x8000u : 0u; });
    provide(u, "GetKeyState", 1, [this](Cpu &c) { return _vkDown[c.arg(0) & 0xff] ? 0xff80u : 0u; });
    provide(u, "GetKeyboardType", 1, [](Cpu &c) -> uint32_t { return c.arg(0) == 0 ? 4 : c.arg(0) == 2 ? 12 : 0; });
    provide(u, "keybd_event", 4, [](Cpu &) { return 0u; });
    provide(u, "SetCursor", 1, [](Cpu &) { return 0u; });
    provide(u, "SetCursorPos", 2, [](Cpu &) { return 1u; });
    provide(u, "ShowCursor", 1, [this](Cpu &c) { return uint32_t(_cursorCount += c.arg(0) ? 1 : -1); });
    provide(u, "SwapMouseButton", 1, [](Cpu &) { return 0u; });
    provide(u, "LoadIconA", 2, [](Cpu &) { return 0x30001u; });
    // The screen's size; SM_NETWORK (63) when there's a network.
    provide(u, "GetSystemMetrics", 1, [this](Cpu &c) -> uint32_t {
        return c.arg(0) == 0 ? 640 : c.arg(0) == 1 ? 480 : c.arg(0) == 63 ? (_options.sockets ? 1 : 0) : 0;
    });
    provide(u, "SystemParametersInfoA", 4, [](Cpu &) { return 1u; });
    provide(u, "SetWindowsHookExA", 4, [](Cpu &) { return 0x40001u; });
    provide(u, "UnhookWindowsHookEx", 1, [](Cpu &) { return 1u; });
    provide(u, "CallNextHookEx", 4, [](Cpu &) { return 0u; });
    provide(u, "MessageBoxA", 4, [this](Cpu &c) {
        say("  message box \"" + c.string(c.arg(2)) + "\": " + c.string(c.arg(1)));
        return 1u; // IDOK
    });
    provide(u, "CharUpperA", 1, [](Cpu &c) -> uint32_t {
        const uint32_t p = c.arg(0);
        if (p < 0x10000)
            return uint32_t(std::toupper(int(p & 0xff)));
        for (uint32_t a = p; c.read8(a); ++a)
            c.write8(a, uint8_t(std::toupper(c.read8(a))));
        return p;
    });
    provide(u, "CharUpperBuffA", 2, [](Cpu &c) {
        for (uint32_t i = 0; i < c.arg(1); ++i)
            c.write8(c.arg(0) + i, uint8_t(std::toupper(c.read8(c.arg(0) + i))));
        return c.arg(1);
    });
    // wsprintfA(buffer, format, ...): cdecl.
    _process->provide(u, "wsprintfA", [](Cpu &c) {
        const std::string s = x86::formatPrintf(c, c.arg(1), c.reg[Cpu::ESP] + 12);
        c.writeString(c.arg(0), s, 1024);
        c.reg[Cpu::EAX] = uint32_t(std::min<size_t>(s.size(), 1023));
        return 0u;
    });
    provide(u, "SetRect", 5, [rect](Cpu &c) {
        rect(c, c.arg(0), int(c.arg(1)), int(c.arg(2)), int(c.arg(3)), int(c.arg(4)));
        return 1u;
    });
    provide(u, "OffsetRect", 3, [](Cpu &c) {
        for (uint32_t i = 0; i < 16; i += 4)
            c.write32(c.arg(0) + i, c.read32(c.arg(0) + i) + ((i / 4) % 2 ? c.arg(2) : c.arg(1)));
        return 1u;
    });
    // DDE (the online lobby's): not there.
    provide(u, "DdeInitializeA", 4, [](Cpu &) { return 0x4009u; }); // DMLERR_SYS_ERROR
    provide(u, "DdeUninitialize", 1, [](Cpu &) { return 1u; });
    provide(u, "DdeConnect", 4, [](Cpu &) { return 0u; });
    provide(u, "DdeCreateStringHandleA", 3, [](Cpu &) { return 0u; });
    provide(u, "DdeClientTransaction", 8, [](Cpu &) { return 0u; });
    provide(u, "DdeGetLastError", 1, [](Cpu &) { return 0x4009u; });
    provide(u, "DdeQueryStringA", 5, [](Cpu &) { return 0u; });
}

} // namespace reluva::nfs
