// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/granny_dll.h"

#include <cstring>
#include <string>

namespace reluva::awe {

namespace {

constexpr uint32_t kMemory = 64u << 20;
// The DLL reads a little past the end of its input (its padding size,
// GrannyGetOodle1DecompressBufferPaddingSize, is small); allow plenty.
constexpr uint32_t kPadding = 64;

} // namespace

Error GrannyDll::load(const std::vector<uint8_t>& dll) {
    cpu_ = std::make_unique<x86::Cpu>(kMemory);
    process_ = std::make_unique<x86::Win32Process>(*cpu_);
    module_ = nullptr;
    // Its start-up asks where Windows is (for a log file it never writes
    // here); a buffer and its size in, the length out.
    for (const char* fn : {"GetWindowsDirectoryA", "GetSystemDirectoryA"})
        process_->provide("KERNEL32.dll", fn, [](x86::Cpu& c) {
            const std::string dir = "C:\\WINDOWS";
            c.reg[x86::Cpu::EAX] = c.writeString(c.arg(0), dir, c.arg(1)) ? uint32_t(dir.size()) : 0;
            return 8u;
        });
    process_->provide("KERNEL32.dll", "QueryPerformanceFrequency", [](x86::Cpu& c) {
        c.write32(c.arg(0), 1000000);
        c.write32(c.arg(0) + 4, 0);
        c.reg[x86::Cpu::EAX] = 1;
        return 4u;
    });
    process_->provide("KERNEL32.dll", "QueryPerformanceCounter", [](x86::Cpu& c) {
        c.write32(c.arg(0), 0);
        c.write32(c.arg(0) + 4, 0);
        c.reg[x86::Cpu::EAX] = 1;
        return 4u;
    });
    const x86::Win32Process::Module* module = nullptr;
    if (Error e = process_->load("granny2.dll", dll, module))
        return e;
    if (Error e = process_->attach(*module))
        return Error::fail("granny2.dll start-up: " + e.message);
    decompressData_ = module->exportAddress("_GrannyDecompressData@32");
    if (!decompressData_)
        return Error::fail("granny2.dll has no GrannyDecompressData");
    binkDecompressTexture_ = module->exportAddress("_GrannyBinkDecompressTexture@32");
    module_ = module;
    return Error::success();
}

Error GrannyDll::decompress(uint32_t format, const std::vector<uint8_t>& in, uint32_t stop0, uint32_t stop1,
                            std::vector<uint8_t>& out) {
    if (!module_)
        return Error::fail("granny2.dll isn't loaded");
    x86::Arena& mem = process_->memory();
    const uint32_t src = mem.alloc(uint32_t(in.size()) + kPadding);
    const uint32_t dst = mem.alloc(uint32_t(out.size()) + kPadding);
    if (!src || !dst) {
        mem.free(src);
        mem.free(dst);
        return Error::fail("Out of emulated memory");
    }
    std::memcpy(cpu_->memory().data() + src, in.data(), in.size());
    // GrannyDecompressData(Format, FileIsByteReversed, CompressedBytesSize,
    //                      CompressedBytes, Stop0, Stop1, Stop2, DecompressedBytes)
    uint32_t ok = 0;
    Error e = cpu_->call(decompressData_,
                         {format, 0, uint32_t(in.size()), src, stop0, stop1, uint32_t(out.size()), dst}, ok);
    if (!e && !(ok & 0xff))
        e = Error::fail("GrannyDecompressData failed");
    if (!e)
        std::memcpy(out.data(), cpu_->memory().data() + dst, out.size());
    mem.free(src);
    mem.free(dst);
    return e;
}

Error GrannyDll::binkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t>& in,
                             std::vector<uint8_t>& rgba) {
    if (!module_ || !binkDecompressTexture_)
        return Error::fail("granny2.dll isn't loaded");
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return Error::fail("Bad texture size");
    x86::Arena& mem = process_->memory();
    const uint32_t stride = uint32_t(width) * 4, size = stride * uint32_t(height);
    const uint32_t src = mem.alloc(uint32_t(in.size()) + kPadding);
    const uint32_t dst = mem.alloc(size);
    // A granny_pixel_layout: bytes per pixel, then each component's shift
    // and bit count (R, G, B, A): R, G, B, A bytes in memory.
    const uint32_t layout = mem.alloc(36);
    Error e;
    if (!src || !dst || !layout) {
        e = Error::fail("Out of emulated memory");
    } else {
        std::memcpy(cpu_->memory().data() + src, in.data(), in.size());
        const uint32_t fields[9] = {4, 0, 8, 16, 24, 8, 8, 8, 8};
        for (int i = 0; i < 9; i++)
            cpu_->write32(layout + 4 * uint32_t(i), fields[i]);
        // GrannyBinkDecompressTexture(Width, Height, Flags, CompressedBytesSize,
        //                             Compressed, DestLayout, DestStride, DestBits)
        uint32_t ignored = 0;
        e = cpu_->call(binkDecompressTexture_,
                       {uint32_t(width), uint32_t(height), flags, uint32_t(in.size()), src, layout, stride, dst},
                       ignored);
        if (!e)
            rgba.assign(cpu_->memory().begin() + dst, cpu_->memory().begin() + dst + size);
    }
    mem.free(src);
    mem.free(dst);
    mem.free(layout);
    return e;
}

} // namespace reluva::awe
