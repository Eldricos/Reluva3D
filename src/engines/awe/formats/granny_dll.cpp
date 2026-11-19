// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/formats/granny_dll.h"

#include "engines/awe/formats/gr2.h"

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

Error GrannyDll::load(const std::vector<uint8_t> &dll) {
    _cpu = std::make_unique<x86::Cpu>(kMemory);
    // A call that runs away fails instead of hanging: the largest texture
    // takes well under a billion instructions.
    _cpu->setInstructionLimit(4'000'000'000ull);
    _process = std::make_unique<x86::Win32Process>(*_cpu);
    _module = nullptr;
    // Its start-up asks where Windows is (for a log file it never writes
    // here); a buffer and its size in, the length out.
    for (const char *fn : {"GetWindowsDirectoryA", "GetSystemDirectoryA"})
        _process->provide("KERNEL32.dll", fn, [](x86::Cpu &c) {
            const std::string dir = "C:\\WINDOWS";
            c.reg[x86::Cpu::EAX] = c.writeString(c.arg(0), dir, c.arg(1)) ? uint32_t(dir.size()) : 0;
            return 8u;
        });
    _process->provide("KERNEL32.dll", "QueryPerformanceFrequency", [](x86::Cpu &c) {
        c.write32(c.arg(0), 1000000);
        c.write32(c.arg(0) + 4, 0);
        c.reg[x86::Cpu::EAX] = 1;
        return 4u;
    });
    _process->provide("KERNEL32.dll", "QueryPerformanceCounter", [](x86::Cpu &c) {
        c.write32(c.arg(0), 0);
        c.write32(c.arg(0) + 4, 0);
        c.reg[x86::Cpu::EAX] = 1;
        return 4u;
    });
    const x86::Win32Process::Module *module = nullptr;
    if (Error e = _process->load("granny2.dll", dll, module))
        return e;
    if (Error e = _process->attach(*module))
        return Error::fail("granny2.dll start-up: " + e.message);
    _decompressData = module->exportAddress("_GrannyDecompressData@32");
    if (!_decompressData)
        return Error::fail("granny2.dll has no GrannyDecompressData");
    _binkDecompressTexture = module->exportAddress("_GrannyBinkDecompressTexture@32");
    _module = module;
    return Error::success();
}

Error GrannyDll::decompress(uint32_t format, const std::vector<uint8_t> &in, uint32_t stop0, uint32_t stop1,
                            std::vector<uint8_t> &out) {
    if (!_module)
        return Error::fail("granny2.dll isn't loaded");
    x86::Arena &mem = _process->memory();
    const uint32_t src = mem.alloc(uint32_t(in.size()) + kPadding);
    const uint32_t dst = mem.alloc(uint32_t(out.size()) + kPadding);
    if (!src || !dst) {
        mem.free(src);
        mem.free(dst);
        return Error::fail("Out of emulated memory");
    }
    std::memcpy(_cpu->memory().data() + src, in.data(), in.size());
    // GrannyDecompressData(Format, FileIsByteReversed, CompressedBytesSize,
    //                      CompressedBytes, Stop0, Stop1, Stop2, DecompressedBytes)
    uint32_t ok = 0;
    Error e =
        _cpu->call(_decompressData, {format, 0, uint32_t(in.size()), src, stop0, stop1, uint32_t(out.size()), dst}, ok);
    if (!e && !(ok & 0xff))
        e = Error::fail("GrannyDecompressData failed");
    if (!e)
        std::memcpy(out.data(), _cpu->memory().data() + dst, out.size());
    mem.free(src);
    mem.free(dst);
    return e;
}

Error GrannyDll::binkTexture(int width, int height, uint32_t flags, const std::vector<uint8_t> &in,
                             std::vector<uint8_t> &rgba) {
    if (!_module || !_binkDecompressTexture)
        return Error::fail("granny2.dll isn't loaded");
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return Error::fail("Bad texture size");
    x86::Arena &mem = _process->memory();
    const uint32_t stride = uint32_t(width) * 4;
    const uint32_t size = stride * uint32_t(height);
    const uint32_t src = mem.alloc(uint32_t(in.size()) + kPadding);
    const uint32_t dst = mem.alloc(size);
    // A granny_pixel_layout: bytes per pixel, then each component's shift
    // and bit count (R, G, B, A): R, G, B, A bytes in memory.
    const uint32_t layout = mem.alloc(36);
    Error e;
    if (!src || !dst || !layout) {
        e = Error::fail("Out of emulated memory");
    } else {
        std::memcpy(_cpu->memory().data() + src, in.data(), in.size());
        const uint32_t fields[9] = {4, 0, 8, 16, 24, 8, 8, 8, 8};
        for (int i = 0; i < 9; i++)
            _cpu->write32(layout + 4 * uint32_t(i), fields[i]);
        // GrannyBinkDecompressTexture(Width, Height, Flags, CompressedBytesSize,
        //                             Compressed, DestLayout, DestStride, DestBits)
        uint32_t ignored = 0;
        e = _cpu->call(_binkDecompressTexture,
                       {uint32_t(width), uint32_t(height), flags, uint32_t(in.size()), src, layout, stride, dst},
                       ignored);
        if (e) {
            char at[32];
            std::snprintf(at, sizeof at, " (at 0x%08x)", _cpu->faultEip);
            e = Error::fail("Bink texture: " + e.message + at);
        }
        if (!e)
            rgba.assign(_cpu->memory().begin() + dst, _cpu->memory().begin() + dst + size);
    }
    mem.free(src);
    mem.free(dst);
    mem.free(layout);
    return e;
}

Error GrannyDll::camera(const Camera &in, int width, int height, float view[16], float projection[16],
                        const std::vector<float> *points, std::vector<float> *windowPoints) {
    if (!_module)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t init = _module->exportAddress("_GrannyInitializeDefaultCamera@4");
    const uint32_t aspect = _module->exportAddress("_GrannySetCameraAspectRatios@24");
    const uint32_t build = _module->exportAddress("_GrannyBuildCameraMatrices@4");
    const uint32_t toWindow = _module->exportAddress("_GrannyWorldSpaceToWindowSpace@20");
    if (!init || !aspect || !build || !toWindow)
        return Error::fail("granny2.dll has no camera functions");
    x86::Arena &mem = _process->memory();
    const uint32_t cam = mem.alloc(0x200);
    const uint32_t pt = mem.alloc(32);
    auto putFloat = [&](uint32_t a, float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        _cpu->write32(a, v);
    };
    auto getFloat = [&](uint32_t a) {
        const uint32_t v = _cpu->read32(a);
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
    Error e = _cpu->call(init, {cam}, ignored);
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
        const float w = float(width);
        const float h = float(height);
        e = _cpu->call(aspect, {cam, bits(w / h), bits(w), bits(h), bits(w), bits(h)}, ignored);
    }
    if (!e)
        e = _cpu->call(build, {cam}, ignored);
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
                e = _cpu->call(toWindow, {cam, bits(float(width)), bits(float(height)), pt, pt + 16}, ignored);
                for (uint32_t k = 0; k < 3; k++)
                    windowPoints->push_back(getFloat(pt + 16 + 4 * k));
            }
        }
    }
    mem.free(cam);
    mem.free(pt);
    return e;
}

Error GrannyDll::loadFile(const std::vector<uint8_t> &gr2, LoadedFile &out) {
    out = LoadedFile();
    if (!_module)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t read = _module->exportAddress("_GrannyReadEntireFileFromMemory@8");
    const uint32_t info = _module->exportAddress("_GrannyGetFileInfo@4");
    if (!read || !info)
        return Error::fail("granny2.dll can't read files from memory");
    x86::Arena &mem = _process->memory();
    out.bytes = mem.alloc(uint32_t(gr2.size()) + kPadding);
    if (!out.bytes)
        return Error::fail("Out of emulated memory");
    std::memcpy(_cpu->memory().data() + out.bytes, gr2.data(), gr2.size());
    // GrannyReadEntireFileFromMemory(MemorySize, Memory) -> granny_file*
    Error e = _cpu->call(read, {uint32_t(gr2.size()), out.bytes}, out.file);
    if (!e && !out.file)
        e = Error::fail("granny2.dll couldn't read the file");
    // GrannyGetFileInfo(File) -> granny_file_info*
    if (!e)
        e = _cpu->call(info, {out.file}, out.info);
    if (!e && !out.info)
        e = Error::fail("granny2.dll found no file info");
    if (e)
        freeFile(out);
    return e;
}

void GrannyDll::freeFile(LoadedFile &file) {
    if (!_module)
        return;
    if (file.file) {
        uint32_t ignored = 0;
        if (const uint32_t free = _module->exportAddress("_GrannyFreeFile@4"))
            _cpu->call(free, {file.file}, ignored);
    }
    if (file.bytes)
        _process->memory().free(file.bytes);
    file = LoadedFile();
}

Error GrannyDll::worldPose(const LoadedFile &model, const LoadedFile &animation, float t,
                           std::vector<std::array<float, 16>> &out) {
    out.clear();
    if (!_module)
        return Error::fail("granny2.dll isn't loaded");
    const char *names[] = {"_GrannyInstantiateModel@4",       "_GrannyPlayControlledAnimation@12",
                           "_GrannySetModelClock@8",          "_GrannyGetSourceSkeleton@4",
                           "_GrannyNewLocalPose@4",           "_GrannyNewWorldPose@4",
                           "_GrannySampleModelAnimations@16", "_GrannyBuildWorldPose@24",
                           "_GrannyGetWorldPose4x4@8"};
    uint32_t fn[9];
    for (int i = 0; i < 9; i++)
        if (!(fn[i] = _module->exportAddress(names[i])))
            return Error::fail(std::string("granny2.dll has no ") + names[i]);
    const uint32_t infoType = type("GrannyFileInfoType");
    const uint32_t skeletonType = type("GrannySkeletonType");
    uint32_t models = 0;
    uint32_t animations = 0;
    uint32_t bones = 0;
    if (!infoType || !skeletonType || !memberOffset(infoType, "Models", models) ||
        !memberOffset(infoType, "Animations", animations) || !memberOffset(skeletonType, "Bones", bones))
        return Error::fail("granny2.dll's types aren't as expected");
    // file info -> Models[0], Animations[0] (counted arrays of pointers)
    const uint32_t m = read32(read32(model.info + models + 4));
    const uint32_t a = read32(read32(animation.info + animations + 4));
    if (!m || !a)
        return Error::fail("no model or no animation");
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    uint32_t instance = 0;
    uint32_t control = 0;
    uint32_t skeleton = 0;
    uint32_t local = 0;
    uint32_t world = 0;
    uint32_t ignored = 0;
    Error e = _cpu->call(fn[0], {m}, instance);
    if (!e)
        e = _cpu->call(fn[1], {bits(0.0f), a, instance}, control);
    if (!e)
        e = _cpu->call(fn[2], {instance, bits(t)}, ignored);
    if (!e)
        e = _cpu->call(fn[3], {instance}, skeleton);
    const uint32_t count = e ? 0 : read32(skeleton + bones);
    if (!e)
        e = _cpu->call(fn[4], {count}, local);
    if (!e)
        e = _cpu->call(fn[5], {count}, world);
    if (!e)
        e = _cpu->call(fn[6], {instance, 0, count, local}, ignored);
    if (!e)
        e = _cpu->call(fn[7], {skeleton, 0, count, local, 0, world}, ignored);
    for (uint32_t b = 0; !e && b < count; b++) {
        uint32_t p = 0;
        e = _cpu->call(fn[8], {world, b}, p);
        std::array<float, 16> mtx{};
        for (uint32_t k = 0; !e && k < 16; k++) {
            const uint32_t v = read32(p + 4 * k);
            std::memcpy(&mtx[k], &v, 4);
        }
        out.push_back(mtx);
    }
    return e;
}

Error GrannyDll::expandCurve(uint32_t address, const std::vector<float> &identity, PlainCurve &out) {
    out = PlainCurve();
    if (!_module)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t knotCount = _module->exportAddress("_GrannyCurveGetKnotCount@4");
    const uint32_t dimension = _module->exportAddress("_GrannyCurveGetDimension@4");
    const uint32_t degree = _module->exportAddress("_GrannyCurveGetDegree@4");
    const uint32_t extract = _module->exportAddress("_GrannyCurveExtractKnotValues@24");
    if (!knotCount || !dimension || !degree || !extract)
        return Error::fail("granny2.dll has no curve functions");
    uint32_t n = 0;
    uint32_t dim = 0;
    uint32_t deg = 0;
    if (Error e = _cpu->call(knotCount, {address}, n))
        return e;
    if (Error e = _cpu->call(dimension, {address}, dim))
        return e;
    if (Error e = _cpu->call(degree, {address}, deg))
        return e;
    if (dim == 0 || dim > 16)
        dim = uint32_t(identity.size());
    const uint32_t knots = std::max<uint32_t>(n, 1);
    if (knots > 100000)
        return Error::fail("curve with " + std::to_string(knots) + " knots");
    x86::Arena &mem = _process->memory();
    const uint32_t knotOut = mem.alloc(4 * knots);
    const uint32_t controlOut = mem.alloc(4 * knots * dim);
    const uint32_t ident = mem.alloc(4 * uint32_t(identity.size()) + 4);
    Error e;
    if (!knotOut || !controlOut || !ident) {
        e = Error::fail("Out of emulated memory");
    } else {
        for (size_t i = 0; i < identity.size(); i++) {
            uint32_t v;
            std::memcpy(&v, &identity[i], 4);
            _cpu->write32(ident + 4 * uint32_t(i), v);
        }
        // GrannyCurveExtractKnotValues(Curve, KnotIndex, KnotCount,
        //                              KnotResults, ControlResults, IdentityVector)
        uint32_t ignored = 0;
        e = _cpu->call(extract, {address, 0, knots, knotOut, controlOut, ident}, ignored);
    }
    if (!e) {
        auto f = [&](uint32_t a) {
            const uint32_t v = _cpu->read32(a);
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

uint32_t GrannyDll::type(const char *name) const {
    const uint32_t variable = _module ? _module->exportAddress(name) : 0;
    return variable ? read32(variable) : 0;
}

// A member definition is 32 bytes, as in the files: its type, name,
// referenced type and array width first.
uint32_t GrannyDll::typeSize(uint32_t type) const {
    uint32_t size = 0;
    for (uint32_t m = type; m && read32(m) != 0; m += 32) {
        const Gr2Type t = Gr2Type(read32(m));
        const uint32_t one = t == Gr2Type::kInline ? typeSize(read32(m + 8)) : gr2ValueSize(t);
        size += one * std::max<uint32_t>(1, read32(m + 12));
    }
    return size;
}

bool GrannyDll::memberOffset(uint32_t type, std::string_view name, uint32_t &offset) const {
    offset = 0;
    for (uint32_t m = type; m && read32(m) != 0; m += 32) {
        std::string member;
        for (uint32_t a = read32(m + 4); a && member.size() < 256; a++) {
            const char c = char(_cpu->memory()[a]);
            if (!c)
                break;
            member += c;
        }
        if (member == name)
            return true;
        const Gr2Type t = Gr2Type(read32(m));
        const uint32_t one = t == Gr2Type::kInline ? typeSize(read32(m + 8)) : gr2ValueSize(t);
        offset += one * std::max<uint32_t>(1, read32(m + 12));
    }
    return false;
}

Error GrannyDll::evaluateCurve(uint32_t address, const std::vector<float> &identity, bool normalize, bool loopBack,
                               bool loopForward, float duration, float t, std::vector<float> &out) {
    if (!_module)
        return Error::fail("granny2.dll isn't loaded");
    const uint32_t evaluate = _module->exportAddress("_GrannyEvaluateCurveAtT@36");
    if (!evaluate || identity.empty() || identity.size() > 16)
        return Error::fail("granny2.dll has no GrannyEvaluateCurveAtT");
    if (!_curveScratch && !(_curveScratch = _process->memory().alloc(4 * 32)))
        return Error::fail("Out of emulated memory");
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    const uint32_t ident = _curveScratch;
    const uint32_t result = _curveScratch + 4 * 16;
    for (size_t i = 0; i < identity.size(); i++) {
        _cpu->write32(ident + 4 * uint32_t(i), bits(identity[i]));
        _cpu->write32(result + 4 * uint32_t(i), 0);
    }
    // GrannyEvaluateCurveAtT(Dimension, Normalize, BackwardsLoop, Curve,
    //                        ForwardsLoop, CurveDuration, t, Result,
    //                        IdentityVector)
    uint32_t ignored = 0;
    if (Error e = _cpu->call(evaluate,
                             {uint32_t(identity.size()), normalize ? 1u : 0u, loopBack ? 1u : 0u, address,
                              loopForward ? 1u : 0u, bits(duration), bits(t), result, ident},
                             ignored))
        return e;
    out.resize(identity.size());
    for (size_t i = 0; i < identity.size(); i++) {
        const uint32_t v = _cpu->read32(result + 4 * uint32_t(i));
        std::memcpy(&out[i], &v, 4);
    }
    return Error::success();
}

} // namespace reluva::awe
