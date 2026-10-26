// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/granny_dll.h"

#include <algorithm>
#include <cstdio>
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
    // A call that runs away fails instead of hanging: the largest texture
    // takes well under a billion instructions.
    cpu_->setInstructionLimit(4'000'000'000ull);
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
        if (e) {
            char at[32];
            std::snprintf(at, sizeof at, " (at 0x%08x)", cpu_->faultEip);
            e = Error::fail("Bink texture: " + e.message + at);
        }
        if (!e)
            rgba.assign(cpu_->memory().begin() + dst, cpu_->memory().begin() + dst + size);
    }
    mem.free(src);
    mem.free(dst);
    mem.free(layout);
    return e;
}

Error GrannyDll::camera(const Camera& in, int width, int height, float view[16], float projection[16],
                        const std::vector<float>* points, std::vector<float>* windowPoints) {
    if (!module_)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t init = module_->exportAddress("_GrannyInitializeDefaultCamera@4");
    const uint32_t aspect = module_->exportAddress("_GrannySetCameraAspectRatios@24");
    const uint32_t build = module_->exportAddress("_GrannyBuildCameraMatrices@4");
    const uint32_t toWindow = module_->exportAddress("_GrannyWorldSpaceToWindowSpace@20");
    if (!init || !aspect || !build || !toWindow)
        return Error::fail("granny2.dll has no camera functions");
    x86::Arena& mem = process_->memory();
    const uint32_t cam = mem.alloc(0x200), pt = mem.alloc(32);
    auto putFloat = [&](uint32_t a, float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        cpu_->write32(a, v);
    };
    auto getFloat = [&](uint32_t a) {
        const uint32_t v = cpu_->read32(a);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    };
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    uint32_t ignored = 0;
    Error e = cpu_->call(init, {cam}, ignored);
    // Its layout, from GrannyInitializeDefaultCamera: the field of view at
    // 0x0c, near and far at 0x10 and 0x14, a flag at 0x20 (whether the
    // orientation comes from the quaternion at 0x24), the 3x3 matrix at
    // 0x34, then position (0x58), elevation/azimuth/roll (0x64) and offset
    // (0x70); the matrices follow from 0x7c.
    if (!e) {
        putFloat(cam + 0x0c, in.fovY);
        putFloat(cam + 0x10, in.nearPlane);
        putFloat(cam + 0x14, in.farPlane);
        for (uint32_t k = 0; k < 3; k++) {
            putFloat(cam + 0x58 + 4 * k, in.position[k]);
            putFloat(cam + 0x64 + 4 * k, in.elevAzimRoll[k]);
            putFloat(cam + 0x70 + 4 * k, in.offset[k]);
        }
        // GrannySetCameraAspectRatios(Camera, PhysicalAspectRatio, ScreenWidth,
        //                             ScreenHeight, WindowWidth, WindowHeight)
        const float w = float(width), h = float(height);
        e = cpu_->call(aspect, {cam, bits(w / h), bits(w), bits(h), bits(w), bits(h)}, ignored);
    }
    if (!e)
        e = cpu_->call(build, {cam}, ignored);
    if (!e) {
        for (uint32_t k = 0; k < 16; k++) {
            view[k] = getFloat(cam + 0x7c + 4 * k);
            projection[k] = getFloat(cam + 0x7c + 128 + 4 * k);
        }
        if (points && windowPoints) {
            windowPoints->clear();
            for (size_t i = 0; i + 2 < points->size() && !e; i += 3) {
                for (uint32_t k = 0; k < 3; k++)
                    putFloat(pt + 4 * k, (*points)[i + k]);
                // GrannyWorldSpaceToWindowSpace(Camera, Width, Height, WorldPoint, Result)
                e = cpu_->call(toWindow, {cam, bits(float(width)), bits(float(height)), pt, pt + 16}, ignored);
                for (uint32_t k = 0; k < 3; k++)
                    windowPoints->push_back(getFloat(pt + 16 + 4 * k));
            }
        }
    }
    mem.free(cam);
    mem.free(pt);
    return e;
}

Error GrannyDll::loadFile(const std::vector<uint8_t>& gr2, LoadedFile& out) {
    out = LoadedFile();
    if (!module_)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t read = module_->exportAddress("_GrannyReadEntireFileFromMemory@8");
    const uint32_t info = module_->exportAddress("_GrannyGetFileInfo@4");
    if (!read || !info)
        return Error::fail("granny2.dll can't read files from memory");
    x86::Arena& mem = process_->memory();
    out.bytes = mem.alloc(uint32_t(gr2.size()) + kPadding);
    if (!out.bytes)
        return Error::fail("Out of emulated memory");
    std::memcpy(cpu_->memory().data() + out.bytes, gr2.data(), gr2.size());
    // GrannyReadEntireFileFromMemory(MemorySize, Memory) -> granny_file*
    Error e = cpu_->call(read, {uint32_t(gr2.size()), out.bytes}, out.file);
    if (!e && !out.file)
        e = Error::fail("granny2.dll couldn't read the file");
    // GrannyGetFileInfo(File) -> granny_file_info*
    if (!e)
        e = cpu_->call(info, {out.file}, out.info);
    if (!e && !out.info)
        e = Error::fail("granny2.dll found no file info");
    if (e)
        freeFile(out);
    return e;
}

void GrannyDll::freeFile(LoadedFile& file) {
    if (!module_)
        return;
    if (file.file) {
        uint32_t ignored = 0;
        if (const uint32_t free = module_->exportAddress("_GrannyFreeFile@4"))
            cpu_->call(free, {file.file}, ignored);
    }
    if (file.bytes)
        process_->memory().free(file.bytes);
    file = LoadedFile();
}

Error GrannyDll::expandCurve(uint32_t address, const std::vector<float>& identity, PlainCurve& out) {
    out = PlainCurve();
    if (!module_)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t knotCount = module_->exportAddress("_GrannyCurveGetKnotCount@4");
    const uint32_t dimension = module_->exportAddress("_GrannyCurveGetDimension@4");
    const uint32_t degree = module_->exportAddress("_GrannyCurveGetDegree@4");
    const uint32_t extract = module_->exportAddress("_GrannyCurveExtractKnotValues@24");
    if (!knotCount || !dimension || !degree || !extract)
        return Error::fail("granny2.dll has no curve functions");
    uint32_t n = 0, dim = 0, deg = 0;
    if (Error e = cpu_->call(knotCount, {address}, n))
        return e;
    if (Error e = cpu_->call(dimension, {address}, dim))
        return e;
    if (Error e = cpu_->call(degree, {address}, deg))
        return e;
    if (dim == 0 || dim > 16)
        dim = uint32_t(identity.size());
    const uint32_t knots = std::max<uint32_t>(n, 1);
    if (knots > 100000)
        return Error::fail("curve with " + std::to_string(knots) + " knots");
    x86::Arena& mem = process_->memory();
    const uint32_t knotOut = mem.alloc(4 * knots), controlOut = mem.alloc(4 * knots * dim),
                   ident = mem.alloc(4 * uint32_t(identity.size()) + 4);
    Error e;
    if (!knotOut || !controlOut || !ident) {
        e = Error::fail("Out of emulated memory");
    } else {
        for (size_t i = 0; i < identity.size(); i++) {
            uint32_t v;
            std::memcpy(&v, &identity[i], 4);
            cpu_->write32(ident + 4 * uint32_t(i), v);
        }
        // GrannyCurveExtractKnotValues(Curve, KnotIndex, KnotCount,
        //                              KnotResults, ControlResults, IdentityVector)
        uint32_t ignored = 0;
        e = cpu_->call(extract, {address, 0, knots, knotOut, controlOut, ident}, ignored);
    }
    if (!e) {
        auto f = [&](uint32_t a) {
            const uint32_t v = cpu_->read32(a);
            float r;
            std::memcpy(&r, &v, 4);
            return r;
        };
        out.degree = int(deg);
        out.dimension = int(dim);
        for (uint32_t i = 0; i < knots; i++)
            out.knots.push_back(n ? f(knotOut + 4 * i) : 0.0f);
        for (uint32_t i = 0; i < knots * dim; i++)
            out.controls.push_back(f(controlOut + 4 * i));
    }
    mem.free(knotOut);
    mem.free(controlOut);
    mem.free(ident);
    return e;
}

} // namespace reluva::awe
