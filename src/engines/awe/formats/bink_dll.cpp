// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/formats/bink_dll.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace reluva::awe {

namespace {

constexpr uint32_t kMemory = 256u << 20;
constexpr uint32_t kFileHandle = 0x4b4e4942; // the movie's, from CreateFileA
// BinkOpen's flags: the game's for a scene's video (0x405c43: 0x100000,
// the alpha plane decoded, and 0x2000), and reading in the calling thread
// (bink.h's BINKNOTHREADEDIO).
constexpr uint32_t kOpenFlags = 0x100000 | 0x2000 | 0x08000000;

} // namespace

Error BinkDll::load(const std::vector<uint8_t> &dll) {
    _cpu = std::make_unique<x86::Cpu>(kMemory);
    // A frame takes far fewer than this; a runaway call fails instead.
    _cpu->setInstructionLimit(4'000'000'000ull);
    _process = std::make_unique<x86::Win32Process>(*_cpu);
    _module = nullptr;
    // A stdcall stand-in: its arguments popped, EAX set.
    auto give = [this](const char *dll, const char *fn, uint32_t args, auto &&body) {
        _process->provide(dll, fn, [args, body](x86::Cpu &c) {
            c.reg[x86::Cpu::EAX] = body(c);
            return 4u * args;
        });
    };
    // Its one file: the movie, from memory.
    give("KERNEL32.dll", "CreateFileA", 7, [this](x86::Cpu &) {
        _filePos = 0;
        return kFileHandle;
    });
    give("KERNEL32.dll", "ReadFile", 5, [this](x86::Cpu &c) -> uint32_t {
        const uint32_t handle = c.arg(0);
        const uint32_t buffer = c.arg(1);
        const uint32_t size = c.arg(2);
        const uint32_t read = c.arg(3);
        if (handle != kFileHandle || !c.valid(buffer, size))
            return 0;
        const uint32_t n =
            std::min<uint32_t>(size, uint32_t(_file.size()) - std::min<uint32_t>(_filePos, uint32_t(_file.size())));
        if (n)
            std::memcpy(c.memory().data() + buffer, _file.data() + _filePos, n);
        _filePos += n;
        if (read)
            c.write32(read, n);
        return 1;
    });
    give("KERNEL32.dll", "SetFilePointer", 4, [this](x86::Cpu &c) -> uint32_t {
        const int32_t distance = int32_t(c.arg(1));
        const uint32_t method = c.arg(3); // FILE_BEGIN, FILE_CURRENT, FILE_END
        const int64_t from = method == 0 ? 0 : method == 1 ? int64_t(_filePos) : int64_t(_file.size());
        _filePos = uint32_t(std::clamp<int64_t>(from + distance, 0, int64_t(_file.size())));
        if (c.arg(2))
            c.write32(c.arg(2), 0);
        return _filePos;
    });
    // Synchronisation and time: one thread, no waiting.
    for (const char *fn : {"CreateMutexA", "CreateEventA"})
        give("KERNEL32.dll", fn, 3 + (fn[6] == 'E'), [](x86::Cpu &) { return 0x10000u; });
    give("KERNEL32.dll", "ReleaseMutex", 1, [](x86::Cpu &) { return 1u; });
    give("KERNEL32.dll", "SetEvent", 1, [](x86::Cpu &) { return 1u; });
    give("KERNEL32.dll", "WaitForSingleObject", 2, [](x86::Cpu &) { return 0u; });
    give("KERNEL32.dll", "WaitForMultipleObjects", 4, [](x86::Cpu &) { return 0u; });
    give("KERNEL32.dll", "Sleep", 1, [](x86::Cpu &) { return 0u; });
    give("KERNEL32.dll", "QueryPerformanceFrequency", 1, [](x86::Cpu &c) {
        c.write32(c.arg(0), 1000000);
        c.write32(c.arg(0) + 4, 0);
        return 1u;
    });
    give("KERNEL32.dll", "QueryPerformanceCounter", 1, [](x86::Cpu &c) {
        c.write32(c.arg(0), 0);
        c.write32(c.arg(0) + 4, 0);
        return 1u;
    });
    for (const char *fn : {"GetWindowsDirectoryA", "GetSystemDirectoryA"})
        give("KERNEL32.dll", fn, 2, [](x86::Cpu &c) {
            const std::string dir = "C:\\WINDOWS";
            return c.writeString(c.arg(0), dir, c.arg(1)) ? uint32_t(dir.size()) : 0u;
        });
    // No sound device.
    give("WINMM.dll", "waveOutOpen", 6, [](x86::Cpu &) { return 2u; }); // MMSYSERR_BADDEVICEID
    const x86::Win32Process::Module *module = nullptr;
    if (Error e = _process->load("binkw32.dll", dll, module))
        return e;
    if (Error e = _process->attach(*module))
        return Error::fail("binkw32.dll start-up: " + e.message);
    _module = module;
    return Error::success();
}

Error BinkDll::open(const std::vector<uint8_t> &bik, int &width, int &height, uint32_t &frames) {
    close();
    if (!_module)
        return Error::fail("binkw32.dll isn't loaded");
    const uint32_t setTracks = _module->exportAddress("_BinkSetSoundTrack@8");
    const uint32_t openFn = _module->exportAddress("_BinkOpen@8");
    if (!openFn)
        return Error::fail("binkw32.dll has no BinkOpen");
    _file = bik;
    _filePos = 0;
    x86::Arena &mem = _process->memory();
    const uint32_t name = mem.alloc(16);
    _cpu->writeString(name, "movie.bik", 16);
    uint32_t ignored = 0;
    // No sound tracks: BinkSetSoundTrack(0, 0) before opening.
    if (setTracks)
        _cpu->call(setTracks, {0, 0}, ignored);
    Error e = _cpu->call(openFn, {name, kOpenFlags}, _bink);
    mem.free(name);
    if (!e && !_bink)
        e = Error::fail("BinkOpen failed");
    if (e)
        return e;
    // The BINK structure starts Width, Height, Frames (bink.h).
    _width = int(_cpu->read32(_bink)), _height = int(_cpu->read32(_bink + 4));
    frames = _cpu->read32(_bink + 8);
    width = _width, height = _height;
    _buffer = mem.alloc(uint32_t(_width) * uint32_t(_height) * 4);
    if (!_buffer)
        return Error::fail("Out of emulated memory");
    return Error::success();
}

Error BinkDll::frame(std::vector<uint8_t> &bgra, bool alpha) {
    if (!_bink)
        return Error::fail("no movie open");
    const uint32_t doFrame = _module->exportAddress("_BinkDoFrame@4");
    const uint32_t copy = _module->exportAddress("_BinkCopyToBuffer@28");
    const uint32_t next = _module->exportAddress("_BinkNextFrame@4");
    uint32_t ignored = 0;
    if (Error e = _cpu->call(doFrame, {_bink}, ignored))
        return e;
    // BinkCopyToBuffer(bink, dest, pitch, height, x, y, flags): the whole
    // frame, 32-bit (3; 5 with alpha), BINKCOPYALL as the game asks.
    const uint32_t flags = (alpha ? 5u : 3u) | 0x80000000u;
    if (Error e = _cpu->call(copy, {_bink, _buffer, uint32_t(_width) * 4, uint32_t(_height), 0, 0, flags}, ignored))
        return e;
    bgra.assign(_cpu->memory().data() + _buffer, _cpu->memory().data() + _buffer + size_t(_width) * _height * 4);
    return _cpu->call(next, {_bink}, ignored);
}

Error BinkDll::frameYuy2(std::vector<uint8_t> &yuy2) {
    if (!_bink)
        return Error::fail("no movie open");
    uint32_t ignored = 0;
    if (Error e = _cpu->call(_module->exportAddress("_BinkDoFrame@4"), {_bink}, ignored))
        return e;
    if (Error e =
            _cpu->call(_module->exportAddress("_BinkCopyToBuffer@28"),
                       {_bink, _buffer, uint32_t(_width) * 2, uint32_t(_height), 0, 0, 13u | 0x80000000u}, ignored))
        return e;
    yuy2.assign(_cpu->memory().data() + _buffer, _cpu->memory().data() + _buffer + size_t(_width) * _height * 2);
    return _cpu->call(_module->exportAddress("_BinkNextFrame@4"), {_bink}, ignored);
}

void BinkDll::close() {
    if (_bink && _module) {
        uint32_t ignored = 0;
        if (const uint32_t closeFn = _module->exportAddress("_BinkClose@4"))
            _cpu->call(closeFn, {_bink}, ignored);
    }
    if (_buffer && _process)
        _process->memory().free(_buffer);
    _bink = _buffer = 0;
}

} // namespace reluva::awe
