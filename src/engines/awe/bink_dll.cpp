// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/bink_dll.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace reluva::awe {

namespace {

constexpr uint32_t kMemory = 256u << 20;
constexpr uint32_t kFileHandle = 0x4b4e4942; // the movie's, from CreateFileA
// BinkOpen's flag for reading in the calling thread (bink.h's
// BINKNOTHREADEDIO).
constexpr uint32_t kNoThreadedIo = 0x08000000;

} // namespace

Error BinkDll::load(const std::vector<uint8_t>& dll) {
    cpu_ = std::make_unique<x86::Cpu>(kMemory);
    // A frame takes far fewer than this; a runaway call fails instead.
    cpu_->setInstructionLimit(4'000'000'000ull);
    process_ = std::make_unique<x86::Win32Process>(*cpu_);
    module_ = nullptr;
    // A stdcall stand-in: its arguments popped, EAX set.
    auto give = [this](const char* dll, const char* fn, uint32_t args, auto&& body) {
        process_->provide(dll, fn, [args, body](x86::Cpu& c) {
            c.reg[x86::Cpu::EAX] = body(c);
            return 4u * args;
        });
    };
    // Its one file: the movie, from memory.
    give("KERNEL32.dll", "CreateFileA", 7, [this](x86::Cpu&) {
        filePos_ = 0;
        return kFileHandle;
    });
    give("KERNEL32.dll", "ReadFile", 5, [this](x86::Cpu& c) -> uint32_t {
        const uint32_t handle = c.arg(0), buffer = c.arg(1), size = c.arg(2), read = c.arg(3);
        if (handle != kFileHandle || !c.valid(buffer, size))
            return 0;
        const uint32_t n = std::min<uint32_t>(size, uint32_t(file_.size()) - std::min<uint32_t>(filePos_, uint32_t(file_.size())));
        if (n)
            std::memcpy(c.memory().data() + buffer, file_.data() + filePos_, n);
        filePos_ += n;
        if (read)
            c.write32(read, n);
        return 1;
    });
    give("KERNEL32.dll", "SetFilePointer", 4, [this](x86::Cpu& c) -> uint32_t {
        const int32_t distance = int32_t(c.arg(1));
        const uint32_t method = c.arg(3); // FILE_BEGIN, FILE_CURRENT, FILE_END
        const int64_t from = method == 0 ? 0 : method == 1 ? int64_t(filePos_) : int64_t(file_.size());
        filePos_ = uint32_t(std::clamp<int64_t>(from + distance, 0, int64_t(file_.size())));
        if (c.arg(2))
            c.write32(c.arg(2), 0);
        return filePos_;
    });
    // Synchronisation and time: one thread, no waiting.
    for (const char* fn : {"CreateMutexA", "CreateEventA"})
        give("KERNEL32.dll", fn, 3 + (fn[6] == 'E'), [](x86::Cpu&) { return 0x10000u; });
    give("KERNEL32.dll", "ReleaseMutex", 1, [](x86::Cpu&) { return 1u; });
    give("KERNEL32.dll", "SetEvent", 1, [](x86::Cpu&) { return 1u; });
    give("KERNEL32.dll", "WaitForSingleObject", 2, [](x86::Cpu&) { return 0u; });
    give("KERNEL32.dll", "WaitForMultipleObjects", 4, [](x86::Cpu&) { return 0u; });
    give("KERNEL32.dll", "Sleep", 1, [](x86::Cpu&) { return 0u; });
    give("KERNEL32.dll", "QueryPerformanceFrequency", 1, [](x86::Cpu& c) {
        c.write32(c.arg(0), 1000000);
        c.write32(c.arg(0) + 4, 0);
        return 1u;
    });
    give("KERNEL32.dll", "QueryPerformanceCounter", 1, [](x86::Cpu& c) {
        c.write32(c.arg(0), 0);
        c.write32(c.arg(0) + 4, 0);
        return 1u;
    });
    for (const char* fn : {"GetWindowsDirectoryA", "GetSystemDirectoryA"})
        give("KERNEL32.dll", fn, 2, [](x86::Cpu& c) {
            const std::string dir = "C:\\WINDOWS";
            return c.writeString(c.arg(0), dir, c.arg(1)) ? uint32_t(dir.size()) : 0u;
        });
    // No sound device.
    give("WINMM.dll", "waveOutOpen", 6, [](x86::Cpu&) { return 2u; }); // MMSYSERR_BADDEVICEID
    const x86::Win32Process::Module* module = nullptr;
    if (Error e = process_->load("binkw32.dll", dll, module))
        return e;
    if (Error e = process_->attach(*module))
        return Error::fail("binkw32.dll start-up: " + e.message);
    module_ = module;
    return Error::success();
}

Error BinkDll::open(const std::vector<uint8_t>& bik, int& width, int& height, uint32_t& frames) {
    close();
    if (!module_)
        return Error::fail("binkw32.dll isn't loaded");
    const uint32_t setTracks = module_->exportAddress("_BinkSetSoundTrack@8");
    const uint32_t openFn = module_->exportAddress("_BinkOpen@8");
    if (!openFn)
        return Error::fail("binkw32.dll has no BinkOpen");
    file_ = bik;
    filePos_ = 0;
    x86::Arena& mem = process_->memory();
    const uint32_t name = mem.alloc(16);
    cpu_->writeString(name, "movie.bik", 16);
    uint32_t ignored = 0;
    // No sound tracks: BinkSetSoundTrack(0, 0) before opening.
    if (setTracks)
        cpu_->call(setTracks, {0, 0}, ignored);
    Error e = cpu_->call(openFn, {name, kNoThreadedIo}, bink_);
    mem.free(name);
    if (!e && !bink_)
        e = Error::fail("BinkOpen failed");
    if (e)
        return e;
    // The BINK structure starts Width, Height, Frames (bink.h).
    width_ = int(cpu_->read32(bink_)), height_ = int(cpu_->read32(bink_ + 4));
    frames = cpu_->read32(bink_ + 8);
    width = width_, height = height_;
    buffer_ = mem.alloc(uint32_t(width_) * uint32_t(height_) * 4);
    if (!buffer_)
        return Error::fail("Out of emulated memory");
    return Error::success();
}

Error BinkDll::frame(std::vector<uint8_t>& bgra, bool alpha) {
    if (!bink_)
        return Error::fail("no movie open");
    const uint32_t doFrame = module_->exportAddress("_BinkDoFrame@4");
    const uint32_t copy = module_->exportAddress("_BinkCopyToBuffer@28");
    const uint32_t next = module_->exportAddress("_BinkNextFrame@4");
    uint32_t ignored = 0;
    if (Error e = cpu_->call(doFrame, {bink_}, ignored))
        return e;
    // BinkCopyToBuffer(bink, dest, pitch, height, x, y, flags): the whole
    // frame, 32-bit (3; 5 with alpha), BINKCOPYALL as the game asks.
    const uint32_t flags = (alpha ? 5u : 3u) | 0x80000000u;
    if (Error e = cpu_->call(copy, {bink_, buffer_, uint32_t(width_) * 4, uint32_t(height_), 0, 0, flags}, ignored))
        return e;
    bgra.assign(cpu_->memory().data() + buffer_, cpu_->memory().data() + buffer_ + size_t(width_) * height_ * 4);
    return cpu_->call(next, {bink_}, ignored);
}

Error BinkDll::frameYuy2(std::vector<uint8_t>& yuy2) {
    if (!bink_)
        return Error::fail("no movie open");
    uint32_t ignored = 0;
    if (Error e = cpu_->call(module_->exportAddress("_BinkDoFrame@4"), {bink_}, ignored))
        return e;
    if (Error e = cpu_->call(module_->exportAddress("_BinkCopyToBuffer@28"),
                             {bink_, buffer_, uint32_t(width_) * 2, uint32_t(height_), 0, 0, 13u | 0x80000000u}, ignored))
        return e;
    yuy2.assign(cpu_->memory().data() + buffer_, cpu_->memory().data() + buffer_ + size_t(width_) * height_ * 2);
    return cpu_->call(module_->exportAddress("_BinkNextFrame@4"), {bink_}, ignored);
}

void BinkDll::close() {
    if (bink_ && module_) {
        uint32_t ignored = 0;
        if (const uint32_t closeFn = module_->exportAddress("_BinkClose@4"))
            cpu_->call(closeFn, {bink_}, ignored);
    }
    if (buffer_ && process_)
        process_->memory().free(buffer_);
    bink_ = buffer_ = 0;
}

} // namespace reluva::awe
