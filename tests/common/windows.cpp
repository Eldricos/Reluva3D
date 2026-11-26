// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Windows a program sees in the x86 emulator: threads, the C and C++
// runtimes, files, windows and the registry, DirectDraw and Direct3D 7,
// DirectInput, Aureal A3D (and the mixer's SoundStream it plays through).
//
//   reluva-tests common.unit.windows

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/types.h"
#include "emulation/x86/a3d.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/miles.h"
#include "emulation/x86/com.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/directx/internal.h"
#include "emulation/x86/msvcp60.h"
#include "emulation/x86/msvcrt.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;

static void testMixerStream() {
    using namespace reluva::audio;
    std::vector<int16_t> out(40);
    Mixer m(22050);
    auto stream = std::make_shared<SoundStream>(22050, 1);
    const SoundHandle a = m.playStream(stream, SoundType::kEffects, kMaxVolume, 0, 0);
    CHECK(a != kNoSound && m.isPlaying(a));
    m.mix(out.data(), 4);
    CHECK(out[0] == 0 && m.isPlaying(a)); // nothing yet: it waits
    const std::vector<int16_t> ten(10, 500);
    stream->push(ten.data(), ten.size());
    m.mix(out.data(), 12);
    // All but the last frame, which waits to be blended with what comes next.
    CHECK(out[0] == 500 && out[17] == 500 && out[18] == 0 && m.isPlaying(a));
    stream->push(ten.data(), ten.size());
    stream->end();
    m.mix(out.data(), 20);
    CHECK(out[0] == 500 && out[21] == 500 && out[22] == 0 && !m.isPlaying(a));
    CHECK(m.playStream(nullptr, SoundType::kEffects) == kNoSound);
}

static void testThreads() {
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    uint32_t now = 1000;
    process.milliseconds = [&] { return now; };
    const uint32_t slot = process.memory().alloc(4);
    const uint32_t counter = process.memory().alloc(4);
    cpu.write32(slot, process.provided("KERNEL32.dll", "Sleep"));
    auto code = [&](std::vector<uint8_t> bytes) {
        const uint32_t at = process.memory().alloc(uint32_t(bytes.size()));
        std::copy(bytes.begin(), bytes.end(), cpu.memory().begin() + at);
        return at;
    };
    auto le = [](uint32_t v) {
        return std::vector<uint8_t>{uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    };
    std::vector<uint8_t> counting = {0xff, 0x05}; // inc dword [counter]
    for (uint8_t b : le(counter))
        counting.push_back(b);
    counting.insert(counting.end(), {0x6a, 50, 0xff, 0x15}); // push 50; call [Sleep]
    for (uint8_t b : le(slot))
        counting.push_back(b);
    counting.insert(counting.end(), {0xeb, 0xf0});         // jmp back to the start
    std::vector<uint8_t> sleeping = {0x6a, 0, 0xff, 0x15}; // the main thread: Sleep(0); return
    for (uint8_t b : le(slot))
        sleeping.push_back(b);
    sleeping.push_back(0xc3);
    const uint32_t thread = code(counting);
    const uint32_t main = code(sleeping);
    const uint32_t returning = code({0xc3});
    process.startThread(thread, 7);
    process.startThread(returning, 0);
    CHECK(process.threadsStarted() == 2 && process.threadsRunning() == 2);
    uint32_t r = 0;
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 1 && process.threadsRunning() == 1);
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 1); // not due yet
    now += 50;
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 2);
    now += 120;
    CHECK(!cpu.call(main, {}, r) && cpu.read32(counter) == 3 && !process.inThread());
}

// Direct3D 6 (IDirect3D3 from IDirectDraw4), called as a program would:
// the devices it offers, a viewport cleared to its background material,
// draws through its clip volume, lighting by the draw's flags and the
// MATERIAL light state, and a texture's handle.
static void testDirect3D6() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectX dx(process);
    dx.modes.push_back({64, 48, 16});
    dx.drawThreads = x86::DirectX::DrawThreads::kOne;
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    };
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    const uint32_t out = mem(4);
    CHECK(!cpu.call(process.provided("DDRAW.dll", "DirectDrawCreate"), {0, out, 0}, r) && r == 0);
    const uint32_t dd1 = cpu.read32(out);
    CHECK(call(dd1, 0, {guid("{9c59509a-39bd-11d1-8c4a-00c04fd930c5}"), out}) == 0); // IDirectDraw4
    const uint32_t dd = cpu.read32(out);
    CHECK(call(dd, 20, {0, 0x11}) == 0);          // SetCooperativeLevel
    CHECK(call(dd, 21, {64, 48, 16, 0, 0}) == 0); // SetDisplayMode
    const uint32_t desc = mem(124);
    auto clearDesc = [&] {
        std::memset(cpu.memory().data() + desc, 0, 124);
        cpu.write32(desc, 124);
    };
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x20);
    cpu.write32(desc + 20, 1);
    cpu.write32(desc + 104, 0x200 | 0x10 | 0x8 | 0x2000); // PRIMARY, FLIP, COMPLEX, 3DDEVICE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t primary = cpu.read32(out);
    const uint32_t caps = mem(16);
    cpu.write32(caps, 0x4);
    CHECK(call(primary, 12, {caps, out}) == 0); // GetAttachedSurface: the back buffer
    const uint32_t back = cpu.read32(out);

    CHECK(call(dd, 0, {guid("{bb223240-e72b-11d0-a9b4-00aa00c0993e}"), out}) == 0); // IDirect3D3
    const uint32_t d3d = cpu.read32(out);
    // EnumDevices: software emulation, then the hardware device, whose
    // hardware description alone says what it can do.
    std::vector<std::pair<uint32_t, uint32_t>> devices; // hardware's dwFlags, software's
    const uint32_t enumerate = cpu.addNative("test!EnumDevices6", [&](x86::Cpu &c) {
        devices.push_back({c.read32(c.arg(3) + 4), c.read32(c.arg(4) + 4)});
        CHECK(c.read32(c.arg(3)) == 252 && (c.read32(c.arg(4) + 156) & 0x400)); // 16-bit targets
        c.reg[x86::Cpu::EAX] = 1;
        return 24u;
    });
    CHECK(call(d3d, 3, {enumerate, 0}) == 0);
    CHECK(devices.size() == 2 && devices[0].first == 0 && devices[0].second != 0 && devices[1].first != 0);
    CHECK(call(d3d, 8, {guid("{84e63de0-46aa-11cf-816f-0000c020156e}"), back, out, 0}) == 0); // CreateDevice
    const uint32_t device = cpu.read32(out);
    CHECK(call(device, 0, {guid("{b0ab3b60-33d7-11d1-a981-00c04fd7b174}"), out}) == 0); // it is a Device3

    // A viewport on the whole target, cleared to its background: a blue material.
    CHECK(call(d3d, 6, {out, 0}) == 0); // CreateViewport
    const uint32_t viewport = cpu.read32(out);
    CHECK(call(device, 5, {viewport}) == 0); // AddViewport
    const uint32_t vp2 = mem(44);
    auto setViewport = [&](float clipX, float clipY, float clipW, float clipH) {
        const uint32_t v[11] = {44, 0, 0, 64, 48, bits(clipX), bits(clipY), bits(clipW), bits(clipH), bits(0), bits(1)};
        for (uint32_t i = 0; i < 11; ++i)
            cpu.write32(vp2 + 4 * i, v[i]);
        CHECK(call(viewport, 17, {vp2}) == 0); // SetViewport2
    };
    setViewport(-1, 1, 2, 2);
    CHECK(call(device, 12, {viewport}) == 0); // SetCurrentViewport
    CHECK(call(d3d, 5, {out, 0}) == 0);       // CreateMaterial
    const uint32_t material = cpu.read32(out);
    const uint32_t mat = mem(80);
    auto setMaterial = [&](float r, float g, float b) {
        std::memset(cpu.memory().data() + mat, 0, 80);
        cpu.write32(mat, 80);
        for (uint32_t at : {4u, 20u}) { // diffuse and ambient
            cpu.write32(mat + at, bits(r));
            cpu.write32(mat + at + 4, bits(g));
            cpu.write32(mat + at + 8, bits(b));
            cpu.write32(mat + at + 12, bits(1));
        }
        CHECK(call(material, 3, {mat}) == 0); // SetMaterial
    };
    setMaterial(0, 0, 1);
    CHECK(call(material, 5, {device, out}) == 0); // GetHandle
    const uint32_t blue = cpu.read32(out);
    CHECK(blue != 0);
    CHECK(call(viewport, 8, {blue}) == 0);     // SetBackground
    CHECK(call(viewport, 12, {0, 0, 1}) == 0); // Clear: the target
    CHECK(call(device, 22, {22, 1}) == 0);     // CULLMODE: none

    // Unlit (D3DDP_DONOTLIGHT), untransformed: XYZ and DIFFUSE.
    const uint32_t verts = mem(3 * 32);
    auto vertex = [&](uint32_t i, float x, float y, uint32_t color) {
        const uint32_t p = verts + i * 16;
        cpu.write32(p, bits(x));
        cpu.write32(p + 4, bits(y));
        cpu.write32(p + 8, bits(0.5f));
        cpu.write32(p + 12, color);
    };
    vertex(0, -1, 1, 0xffff0000u);
    vertex(1, 1, 1, 0xffff0000u);
    vertex(2, -1, -1, 0xffff0000u);
    CHECK(call(device, 9, {}) == 0);                               // BeginScene
    CHECK(call(device, 28, {4, 0x2 | 0x40, verts, 3, 0x10}) == 0); // DrawPrimitive
    CHECK(call(device, 10, {}) == 0);                              // EndScene
    CHECK(call(primary, 11, {0, 1}) == 0);                         // Flip
    const x86::DirectX::Frame &f = dx.frame();
    auto at = [&](int x, int y) { return f.pixels[size_t(y * f.width + x)] & 0xffffff; };
    CHECK(at(4, 4) == 0xff0000 && at(60, 44) == 0x0000ff);

    // A clip volume twice the size: the same triangle covers the middle
    // quarter's top left, a quarter of the target each way.
    CHECK(call(viewport, 12, {0, 0, 1}) == 0);
    setViewport(-2, 2, 4, 4);
    CHECK(call(device, 28, {4, 0x2 | 0x40, verts, 3, 0x10}) == 0);
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(at(20, 14) == 0xff0000 && at(4, 4) == 0x0000ff && at(40, 30) == 0x0000ff);
    setViewport(-1, 1, 2, 2);

    // Lit (normals, no DONOTLIGHT): no lights, so the material's ambient
    // colour under the AMBIENT light state; the MATERIAL light state names it.
    setMaterial(0, 1, 0);
    CHECK(call(viewport, 12, {0, 0, 1}) == 0);
    CHECK(call(device, 24, {1, blue}) == 0);        // SetLightState: MATERIAL (the same handle, now green)
    CHECK(call(device, 24, {2, 0xffffffffu}) == 0); // AMBIENT: white
    CHECK(call(device, 23, {2, out}) == 0 && cpu.read32(out) == 0xffffffffu);
    const uint32_t lit = mem(3 * 32);
    for (uint32_t i = 0; i < 3; ++i) { // D3DFVF_VERTEX: position, normal, texture coordinates
        const float xy[3][2] = {{-1, 1}, {1, 1}, {-1, -1}};
        const uint32_t p = lit + i * 32;
        const uint32_t v[8] = {bits(xy[i][0]), bits(xy[i][1]), bits(0.5f), 0, 0, bits(-1), 0, 0};
        for (uint32_t k = 0; k < 8; ++k)
            cpu.write32(p + 4 * k, v[k]);
    }
    CHECK(call(device, 40, {0, 1, 2}) == 0); // stage 0 COLOROP: SELECTARG1
    CHECK(call(device, 40, {0, 2, 0}) == 0); // COLORARG1: diffuse
    CHECK(call(device, 28, {4, 0x112, lit, 3, 0}) == 0);
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(at(4, 4) == 0x00ff00 && at(60, 44) == 0x00ff00); // the background is the same material

    // A texture's IDirect3DTexture2: a handle, and SetTexture with it.
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4);
    cpu.write32(desc + 8, 2);
    cpu.write32(desc + 12, 2);
    cpu.write32(desc + 104, 0x1000); // TEXTURE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t surface = cpu.read32(out);
    CHECK(call(surface, 0, {guid("{93281502-8cf8-11d0-89ab-00a0c9054129}"), out}) == 0);
    const uint32_t texture = cpu.read32(out);
    CHECK(call(texture, 3, {device, out}) == 0 && cpu.read32(out) != 0 && cpu.read32(out) != blue);
    CHECK(call(device, 38, {0, texture}) == 0);                                          // SetTexture
    CHECK(call(device, 37, {0, out}) == 0 && cpu.read32(out) == texture);                // GetTexture
    CHECK(call(primary, 0, {guid("{93281502-8cf8-11d0-89ab-00a0c9054129}"), out}) != 0); // not a texture
}

// Structured exceptions, with handlers assembled here and registered at
// fs:[0] as a program's are: one that goes on with the program, its
// context changed; one that searches on and one that catches (unwinding
// the first, then jumping to its continuation); and one nothing handles.
static void testExceptions() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    auto code = [&](std::vector<uint8_t> bytes) {
        const uint32_t at = process.memory().alloc(uint32_t(bytes.size()));
        std::copy(bytes.begin(), bytes.end(), cpu.memory().begin() + at);
        return at;
    };
    auto put32 = [](std::vector<uint8_t> &b, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    };
    const uint32_t raise = process.provided("KERNEL32.dll", "RaiseException");
    const uint32_t unwind = process.provided("KERNEL32.dll", "RtlUnwind");
    // push handler; push fs:[0]; mov fs:[0], esp
    auto registerHandler = [&](std::vector<uint8_t> &b, uint32_t handler) {
        b.push_back(0x68);
        put32(b, handler);
        b.insert(b.end(), {0x64, 0xff, 0x35, 0, 0, 0, 0, 0x64, 0x89, 0x25, 0, 0, 0, 0});
    };
    // RaiseException(code, 0, 0, 0) through eax
    auto raiseException = [&](std::vector<uint8_t> &b, uint32_t exceptionCode) {
        b.insert(b.end(), {0x6a, 0, 0x6a, 0, 0x6a, 0});
        b.push_back(0x68);
        put32(b, exceptionCode);
        b.push_back(0xb8);
        put32(b, raise);
        b.insert(b.end(), {0xff, 0xd0});
    };
    // mov ecx, [esp]; mov fs:[0], ecx; add esp, 8
    const std::vector<uint8_t> unregister = {0x8b, 0x0c, 0x24, 0x64, 0x89, 0x0d, 0, 0, 0, 0, 0x83, 0xc4, 0x08};
    const uint32_t chainStart = cpu.read32(cpu.fsBase);
    uint32_t r = 0;

    // ExceptionContinueExecution: the handler sets the context's eax to 42.
    const uint32_t goOn = code({0x8b, 0x44, 0x24, 0x0c,                 // mov eax, [esp+12]: the context
                                0xc7, 0x80, 0xb0, 0, 0, 0, 42, 0, 0, 0, // mov dword [eax+0xb0], 42
                                0x31, 0xc0, 0xc3});                     // xor eax, eax; ret
    std::vector<uint8_t> first;
    registerHandler(first, goOn);
    raiseException(first, 0x1234);
    first.insert(first.end(), unregister.begin(), unregister.end());
    first.push_back(0xc3);
    CHECK(!cpu.call(code(first), {}, r) && r == 42 && cpu.read32(cpu.fsBase) == chainStart);

    // Searched on by the inner handler, caught by the outer, which unwinds
    // to its own frame and goes on at its continuation with eax 7.
    const uint32_t searched = process.memory().alloc(4);
    const uint32_t unwound = process.memory().alloc(4);
    std::vector<uint8_t> inner = {0xff, 0x05};
    put32(inner, searched);                                  // inc dword [searched]
    inner.insert(inner.end(), {0x8b, 0x44, 0x24, 0x04,       // mov eax, [esp+4]: the record
                               0xf7, 0x40, 0x04, 2, 0, 0, 0, // test dword [eax+4], EXCEPTION_UNWINDING
                               0x74, 0x06, 0xff, 0x05});     // jz +6; inc dword [unwound]
    put32(inner, unwound);
    inner.insert(inner.end(), {0xb8, 1, 0, 0, 0, 0xc3}); // mov eax, ExceptionContinueSearch; ret
    std::vector<uint8_t> continuation(unregister);
    continuation.push_back(0xc3);
    const uint32_t landing = code(continuation);
    std::vector<uint8_t> outer = {0x8b, 0x44, 0x24, 0x08,          // mov eax, [esp+8]: its frame
                                  0x8b, 0x4c, 0x24, 0x04,          // mov ecx, [esp+4]: the record
                                  0x6a, 0,    0x51, 0x6a, 0, 0x50, // RtlUnwind(frame, 0, record, 0)
                                  0xb8};
    put32(outer, unwind);
    outer.insert(outer.end(), {0xff, 0xd0,             // call eax
                               0x8b, 0x44, 0x24, 0x08, // mov eax, [esp+8]
                               0x89, 0xc4,             // mov esp, eax: its frame
                               0xb8, 7, 0, 0, 0,       // mov eax, 7
                               0x68});                 // push continuation; ret
    put32(outer, landing);
    outer.push_back(0xc3);
    std::vector<uint8_t> second;
    registerHandler(second, code(outer));
    registerHandler(second, code(inner));
    raiseException(second, 0x5678);
    second.insert(second.end(), {0xb8, 99, 0, 0, 0, 0xc3}); // not reached
    CHECK(!cpu.call(code(second), {}, r) && r == 7);
    CHECK(cpu.read32(searched) == 2 && cpu.read32(unwound) == 1 && cpu.read32(cpu.fsBase) == chainStart);

    // Nothing handles it: the run ends, saying so; told first.
    uint32_t told = 0;
    process.exceptionRaised = [&](x86::Cpu &c, uint32_t record) { told = c.read32(record); };
    std::vector<uint8_t> third;
    raiseException(third, 0xe0000001u);
    third.push_back(0xc3);
    const Error err = cpu.call(code(third), {}, r);
    CHECK(err && err.message.find("nothing handled") != std::string::npos && told == 0xe0000001u);
}

// MSVCRT's stand-ins and KERNEL32's files, called as the program would.
static void testMsvcrtAndFiles(const std::filesystem::path &tmp) {
    std::filesystem::create_directories(tmp / "Game");
    {
        std::ofstream(tmp / "Game" / "Data.txt", std::ios::binary) << "hello world";
        std::ofstream(tmp / "Game" / "Other.bin", std::ios::binary) << "x";
        std::ofstream(tmp / "Game" / "Lines.dat", std::ios::binary) << "ab\r\ncd\r\n";
    }
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    process.setProgram("Prog.exe", "C:\\Game\\");
    process.mapPath = [&](const std::string &path, bool forWriting) -> std::optional<std::filesystem::path> {
        if (path.rfind("C:\\Game\\", 0) != 0)
            return std::nullopt;
        if (forWriting)
            return path == "C:\\Game\\Out.dat"     ? std::optional(tmp / "Game" / "Out.dat")
                   : path == "C:\\Game\\Moved.dat" ? std::optional(tmp / "Game" / "Moved.dat")
                                                   : std::nullopt;
        if (path.size() == 8)
            return tmp / "Game";
        return fs::findFile(tmp / "Game", path.substr(8)).value_or(tmp / "Game" / "missing");
    };
    process.cdLabel = "LEGOATEN";
    x86::addMsvcrt(process);
    auto fn = [&](const char *dll, const char *name) { return process.provided(dll, name); };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    CHECK(!cpu.call(fn("MSVCRT.dll", "strlen"), {text("abcdef")}, r) && r == 6);
    CHECK(!cpu.call(fn("MSVCRT.dll", "_stricmp"), {text("Alpha"), text("aLPHA")}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "strcmp"), {text("a"), text("b")}, r) && int32_t(r) < 0);
    // Visual C++'s rand: from seed 1, 41, 18467, 6334.
    CHECK(!cpu.call(fn("MSVCRT.dll", "srand"), {1}, r));
    for (uint32_t expected : {41u, 18467u, 6334u})
        CHECK(!cpu.call(fn("MSVCRT.dll", "rand"), {}, r) && r == expected);
    // sscanf and sprintf.
    const uint32_t vars = process.memory().alloc(64);
    cpu.write32(vars + 48, vars);
    cpu.write32(vars + 52, vars + 4);
    cpu.write32(vars + 56, vars + 8);
    CHECK(x86::scanFormat(cpu, "12 3.5 word", "%d %f %s", vars + 48) == 3);
    float f = 0;
    const uint32_t fbits = cpu.read32(vars + 4);
    std::memcpy(&f, &fbits, 4);
    CHECK(cpu.read32(vars) == 12 && f == 3.5f && cpu.string(vars + 8) == "word");
    CHECK(!cpu.call(fn("MSVCRT.dll", "sprintf"), {vars + 16, text("%s-%03d"), text("id"), 7}, r) && r == 6 &&
          cpu.string(vars + 16) == "id-007");
    // The FPU-stack ones: _ftol cuts towards zero, _CIpow takes x below y.
    cpu.fpuPush(-2.7);
    CHECK(!cpu.call(fn("MSVCRT.dll", "_ftol"), {}, r) && r == uint32_t(-2) && cpu.reg[x86::Cpu::EDX] == 0xffffffffu);
    cpu.fpuPush(2);
    cpu.fpuPush(10);
    CHECK(!cpu.call(fn("MSVCRT.dll", "_CIpow"), {}, r) && cpu.fpuPop() == 1024);
    // _EH_prolog: a frame of the previous FS:[0], the handler (EAX) and -1,
    // linked in, with EBP pointing at the caller's EBP kept above it.
    const uint32_t chain = cpu.read32(cpu.fsBase);
    cpu.reg[x86::Cpu::EAX] = 0x1234;
    cpu.reg[x86::Cpu::EBP] = 0x5555;
    const uint32_t espBefore = cpu.reg[x86::Cpu::ESP];
    CHECK(!cpu.call(fn("MSVCRT.dll", "_EH_prolog"), {}, r));
    const uint32_t frame = cpu.read32(cpu.fsBase);
    CHECK(frame == espBefore - 4 - 12 && cpu.read32(frame) == chain && cpu.read32(frame + 4) == 0x1234 &&
          cpu.read32(frame + 8) == 0xffffffffu && cpu.read32(frame + 12) == 0x5555 &&
          cpu.reg[x86::Cpu::EBP] == frame + 12);
    cpu.write32(cpu.fsBase, chain);

    // Files: fopen relative to the program's folder, fread, fclose.
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("data.txt"), text("r")}, r) && r != 0);
    const uint32_t file = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fread"), {vars, 1, 32, file}, r) && r == 11);
    CHECK(std::string(reinterpret_cast<const char *>(cpu.memory().data() + vars), 11) == "hello world");
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {file}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("nothere.txt"), text("rb")}, r) && r == 0);
    // Text mode: CR-LF read as LF (fgets, fgetc, fread) and written from LF;
    // binary mode keeps both.
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("Lines.dat"), text("r")}, r) && r != 0);
    const uint32_t lines = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fgets"), {vars, 32, lines}, r) && r == vars && cpu.string(vars) == "ab\n");
    CHECK(!cpu.call(fn("MSVCRT.dll", "fgetc"), {lines}, r) && r == 'c');
    CHECK(!cpu.call(fn("MSVCRT.dll", "fread"), {vars, 1, 32, lines}, r) && r == 2 && cpu.string(vars, 2) == "d\n");
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {lines}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("Lines.dat"), text("rb")}, r) && r != 0);
    const uint32_t raw = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fread"), {vars, 1, 32, raw}, r) && r == 8);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {raw}, r) && r == 0);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fopen"), {text("Out.dat"), text("w")}, r) && r != 0);
    const uint32_t written = r;
    CHECK(!cpu.call(fn("MSVCRT.dll", "fwrite"), {text("x\ny\n"), 1, 4, written}, r) && r == 4);
    CHECK(!cpu.call(fn("MSVCRT.dll", "fclose"), {written}, r) && r == 0);
    {
        std::ifstream in(tmp / "Game" / "Out.dat", std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "x\r\ny\r\n");
    }
    // CreateFileA, a mapped view, a search, the CD's label.
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateFileA"), {text("C:\\Game\\DATA.TXT"), 0x80000000u, 1, 0, 3, 0, 0}, r) &&
          r != 0xffffffffu);
    const uint32_t handle = r;
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetFileType"), {handle}, r) && r == 1); // FILE_TYPE_DISK
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateFileMappingA"), {handle, 0, 2, 0, 0, 0}, r) && r != 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "MapViewOfFile"), {r, 4, 0, 6, 0}, r) && cpu.string(r, 5) == "world");
    const uint32_t find = process.memory().alloc(320);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindFirstFileA"), {text("C:\\Game\\*.txt"), find}, r) && r != 0xffffffffu &&
          cpu.string(find + 44) == "Data.txt" && cpu.read32(find + 32) == 11);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindNextFileA"), {r, find}, r) && r == 0);
    // A name without wildcards finds that one file, as named.
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindFirstFileA"), {text("C:\\Game\\DATA.TXT"), find}, r) && r != 0xffffffffu &&
          cpu.string(find + 44) == "DATA.TXT" && cpu.read32(find + 32) == 11);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindNextFileA"), {r, find}, r) && r == 0);
    // MoveFileA: what the program wrote is renamed; its own files aren't
    // moved, nor anything onto a file that's there.
    std::ofstream(tmp / "Game" / "Out.dat", std::ios::binary) << "moved";
    const uint32_t move = fn("KERNEL32.dll", "MoveFileA");
    const uint32_t lastError = fn("KERNEL32.dll", "GetLastError");
    CHECK(!cpu.call(move, {text("C:\\Game\\Out.dat"), text("C:\\Game\\Other.bin")}, r) && r == 0);
    CHECK(!cpu.call(lastError, {}, r) && r == 183); // ERROR_ALREADY_EXISTS
    CHECK(!cpu.call(move, {text("C:\\Game\\Data.txt"), text("C:\\Game\\Moved.dat")}, r) && r == 0);
    CHECK(!cpu.call(lastError, {}, r) && r == 5); // ERROR_ACCESS_DENIED
    CHECK(!cpu.call(move, {text("C:\\Game\\Out.dat"), text("C:\\Game\\Moved.dat")}, r) && r == 1);
    CHECK(!std::filesystem::exists(tmp / "Game" / "Out.dat") &&
          std::filesystem::file_size(tmp / "Game" / "Moved.dat") == 5);
    CHECK(std::filesystem::exists(tmp / "Game" / "Data.txt"));
    // CopyFileA: the program's own file copied to where it can write; not
    // over a file that's there when asked not to.
    const uint32_t copy = fn("KERNEL32.dll", "CopyFileA");
    CHECK(!cpu.call(copy, {text("C:\\Game\\Data.txt"), text("C:\\Game\\Moved.dat"), 1}, r) && r == 0);
    CHECK(!cpu.call(lastError, {}, r) && r == 183);
    CHECK(!cpu.call(copy, {text("C:\\Game\\Data.txt"), text("C:\\Game\\Moved.dat"), 0}, r) && r == 1);
    CHECK(std::filesystem::file_size(tmp / "Game" / "Moved.dat") == 11 &&
          std::filesystem::exists(tmp / "Game" / "Data.txt"));
    CHECK(!cpu.call(copy, {text("C:\\Game\\None.dat"), text("C:\\Game\\Moved.dat"), 0}, r) && r == 0);
    // Folders made and removed only where the program writes: not its own
    // folder, which is there.
    const uint32_t mkdir = fn("KERNEL32.dll", "CreateDirectoryA");
    CHECK(!cpu.call(mkdir, {text("C:\\Game\\"), 0}, r) && r == 0);
    CHECK(!cpu.call(lastError, {}, r) && r == 183);
    CHECK(!cpu.call(mkdir, {text("C:\\Game\\Moved.dat"), 0}, r) && r == 0); // a file's there
    CHECK(!cpu.call(fn("KERNEL32.dll", "SetFileAttributesA"), {text("C:\\Game\\Data.txt"), 1}, r) && r == 1);
    CHECK(!cpu.call(fn("KERNEL32.dll", "SetFileAttributesA"), {text("C:\\Game\\None.dat"), 1}, r) && r == 0);
    // The UTF-16 twins, by the ANSI stand-ins; "." in a full path resolved.
    auto wide = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(2 * s.size() + 2));
        for (size_t i = 0; i <= s.size(); ++i)
            cpu.write16(a + 2 * uint32_t(i), i < s.size() ? uint8_t(s[i]) : 0);
        return a;
    };
    CHECK(
        !cpu.call(fn("KERNEL32.dll", "CreateFileW"), {wide("C:\\Game\\.\\Data.txt"), 0x80000000u, 1, 0, 3, 0, 0}, r) &&
        r != 0xffffffffu);
    CHECK(!cpu.call(fn("KERNEL32.dll", "CloseHandle"), {r}, r));
    const uint32_t findW = process.memory().alloc(592);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindFirstFileW"), {wide("C:\\Game\\*.txt"), findW}, r) && r != 0xffffffffu &&
          cpu.read16(findW + 44) == 'D' && cpu.read16(findW + 44 + 2 * 8) == 0 && cpu.read32(findW + 32) == 11);
    const uint32_t folderW = process.memory().alloc(64);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetCurrentDirectoryW"), {32, folderW}, r) && r == 7 &&
          cpu.read16(folderW + 6) == 'G' && cpu.read16(folderW + 14) == 0); // "C:\\Game", as Windows gives it
    // Volumes by their roots only, the backslash required.
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetVolumeInformationA"), {text("C:\\Game"), 0, 0, 0, 0, 0, 0, 0}, r) &&
          r == 0 && process.lastError() == 144);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetVolumeInformationW"), {wide("C:\\"), 0, 0, 0, 0, 0, 0, 0}, r) && r == 1);
    const uint32_t label = process.memory().alloc(32);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetVolumeInformationA"), {text("D:\\"), label, 32, 0, 0, 0, 0, 0}, r) &&
          r == 1 && cpu.string(label) == "LEGOATEN");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetDriveTypeA"), {text("D:\\")}, r) && r == 5);

    // MulDiv rounds half away from zero; date and time text from a
    // SYSTEMTIME (2001-02-03, a Saturday, 14:05:09).
    CHECK(!cpu.call(fn("KERNEL32.dll", "MulDiv"), {10, 3, 4}, r) && r == 8);
    CHECK(!cpu.call(fn("KERNEL32.dll", "MulDiv"), {uint32_t(-10), 3, 4}, r) && r == uint32_t(-8));
    CHECK(!cpu.call(fn("KERNEL32.dll", "MulDiv"), {1, 2, 0}, r) && r == 0xffffffffu);
    const uint32_t st = process.memory().alloc(16);
    const uint16_t when[8] = {2001, 2, 6, 3, 14, 5, 9, 0};
    for (int i = 0; i < 8; ++i)
        cpu.write16(st + 2 * i, when[i]);
    const uint32_t out = process.memory().alloc(64);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetDateFormatA"), {0x409, 0, st, 0, out, 64}, r) && r == 9 &&
          cpu.string(out) == "2/3/2001");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetDateFormatA"), {0x409, 2, st, 0, out, 64}, r) &&
          cpu.string(out) == "Saturday, February 03, 2001");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetTimeFormatA"), {0x409, 0, st, 0, out, 64}, r) &&
          cpu.string(out) == "2:05:09 PM");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetTimeFormatA"), {0x409, 0, st, text("HH'h'mm"), out, 0}, r) && r == 6);
    CHECK(!cpu.call(fn("KERNEL32.dll", "QueryPerformanceFrequency"), {out}, r) && r == 1 && cpu.read32(out) == 1000000);
}

// Reluva's MSVCP60.dll, called as the games call it: strings in Visual C++
// 6's layout, a file stream reading lines, a growing string stream buffer
// and cerr.
static void testMsvcp60(const std::filesystem::path &tmp) {
    std::filesystem::create_directories(tmp / "Game");
    std::ofstream(tmp / "Game" / "Lines.txt", std::ios::binary) << "first line\nsecond\n";
    x86::Cpu cpu(64u << 20);
    x86::Win32Process process(cpu);
    process.setProgram("Prog.exe", "C:\\Game\\");
    process.mapPath = [&](const std::string &path, bool forWriting) -> std::optional<std::filesystem::path> {
        if (forWriting || path.rfind("C:\\Game\\", 0) != 0)
            return std::nullopt;
        return fs::findFile(tmp / "Game", path.substr(8)).value_or(tmp / "Game" / "missing");
    };
    std::string output;
    process.output = [&](const std::string &text) { output += text; };
    x86::addMsvcrt(process);
    const x86::Win32Process::Module *lib = nullptr;
    CHECK(!x86::loadMsvcp60(process, lib));
    if (!lib)
        return;
    CHECK(!process.attach(*lib));
    CHECK(process.missingImports().empty());
    auto fn = [&](const char *name) {
        const uint32_t a = lib->exportAddress(name);
        CHECK(a != 0);
        return a;
    };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    auto method = [&](uint32_t self, const char *name, const std::vector<uint32_t> &args) {
        cpu.reg[x86::Cpu::ECX] = self;
        const bool ok = !cpu.call(fn(name), args, r);
        CHECK(ok);
        return r;
    };
    // string: the characters at +4, length +8, capacity +12, a 0 count
    // before them.
    const char *kCtorChars =
        "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBDABV?$allocator@D@1@@Z";
    const char *kAppend = "?append@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@PBDI@Z";
    const char *kFind = "?find@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z";
    const char *kSubstr = "?substr@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBE?AV12@II@Z";
    const char *kErase = "?erase@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAEAAV12@II@Z";
    const char *kFreeze = "?_Freeze@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@AAEXXZ";
    const char *kDtor = "??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ";
    const char *kEq = "??8std@@YA_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@0@PBD@Z";
    const char *kRfind = "?rfind@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEIPBDII@Z";
    const uint32_t s1 = process.memory().alloc(16);
    const uint32_t s2 = process.memory().alloc(16);
    method(s1, kCtorChars, {text("hello world"), 0});
    auto chars = [&](uint32_t s) { return cpu.read32(s + 4) ? cpu.string(cpu.read32(s + 4)) : std::string(); };
    CHECK(chars(s1) == "hello world" && cpu.read32(s1 + 8) == 11 && cpu.read32(s1 + 12) >= 11 &&
          cpu.read8(cpu.read32(s1 + 4) - 1) == 0);
    method(s1, kAppend, {text(", again and again"), 17});
    CHECK(chars(s1) == "hello world, again and again" && cpu.read32(s1 + 8) == 28);
    CHECK(method(s1, kFind, {text("again"), 0, 5}) == 13 && method(s1, kRfind, {text("again"), 0xffffffffu, 5}) == 23);
    CHECK(method(s1, kFind, {text("absent"), 0, 6}) == 0xffffffffu);
    cpu.reg[x86::Cpu::ECX] = s1;
    CHECK(method(s1, kSubstr, {s2, 6, 5}) == s2 && chars(s2) == "world");
    CHECK(!cpu.call(fn(kEq), {s2, text("world")}, r) && (r & 0xff) == 1);
    method(s1, kErase, {5, 0xffffffffu});
    CHECK(chars(s1) == "hello" && cpu.read32(s1 + 8) == 5);
    method(s1, kFreeze, {});
    CHECK(cpu.read8(cpu.read32(s1 + 4) - 1) == 255);
    method(s1, kDtor, {});
    method(s2, kDtor, {});
    CHECK(cpu.read32(s1 + 4) == 0 && cpu.read32(s2 + 8) == 0);

    // ifstream: the ios at +0x5c, its state at +4; lines, then bytes.
    const uint32_t in = process.memory().alloc(0x5c + 0x34);
    method(in, "??0?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAE@PBDH@Z", {text("C:\\Game\\Lines.txt"), 1, 1});
    const uint32_t ios = in + cpu.read32(cpu.read32(in) + 4);
    CHECK(ios == in + 0x5c && cpu.read32(ios + 4) == 0 && cpu.read32(in + 8 + 0x50) != 0);
    CHECK(cpu.read32(ios) == lib->exportAddress("??_7?$basic_ifstream@DU?$char_traits@D@std@@@std@@6B@"));
    const uint32_t buffer = process.memory().alloc(64);
    method(in, "?getline@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADHD@Z", {buffer, 64, '\n'});
    CHECK(cpu.string(buffer) == "first line" && cpu.read32(in + 4) == 11 && cpu.read32(ios + 4) == 0);
    method(in, "?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z", {buffer, 3});
    CHECK(cpu.string(buffer, 3) == "sec" && cpu.read32(in + 4) == 3);
    CHECK(method(in, "?get@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEHXZ", {}) == 'o');
    method(in, "?read@?$basic_istream@DU?$char_traits@D@std@@@std@@QAEAAV12@PADH@Z", {buffer, 10});
    CHECK(cpu.read32(in + 4) == 3 && cpu.read32(ios + 4) == 3); // "nd\n", then eof and fail
    method(in, "??_D?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAEXXZ", {});
    const uint32_t missing = process.memory().alloc(0x5c + 0x34);
    method(missing, "??0?$basic_ifstream@DU?$char_traits@D@std@@@std@@QAE@PBDH@Z", {text("C:\\Game\\No.txt"), 1, 1});
    CHECK(cpu.read32(missing + 0x5c + 4) == 2);

    // A strstreambuf as the games build one: the base constructor, their
    // own vftable of the library's functions, then _Init (here growing as
    // it's written).
    const uint32_t sb = process.memory().alloc(0x50);
    method(sb, "??0?$basic_streambuf@DU?$char_traits@D@std@@@std@@IAE@XZ", {});
    const uint32_t vft = process.memory().alloc(13 * 4);
    const uint32_t base = cpu.read32(sb);
    for (int i = 0; i < 13; ++i)
        cpu.write32(vft + 4 * i, cpu.read32(base + 4 * i));
    cpu.write32(vft + 4 * 1, fn("?overflow@strstreambuf@std@@MAEHH@Z"));
    cpu.write32(vft + 4 * 2, fn("?pbackfail@strstreambuf@std@@MAEHH@Z"));
    cpu.write32(vft + 4 * 4, fn("?underflow@strstreambuf@std@@MAEHXZ"));
    cpu.write32(vft + 4 * 8, fn("?seekoff@strstreambuf@std@@MAE?AV?$fpos@H@2@JW4seekdir@ios_base@2@H@Z"));
    cpu.write32(vft + 4 * 9, fn("?seekpos@strstreambuf@std@@MAE?AV?$fpos@H@2@V32@H@Z"));
    cpu.write32(sb, vft);
    method(sb, "?_Init@strstreambuf@std@@IAEXHPAD0H@Z", {0, 0, 0, 0});
    const std::string longText = "0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHECK(method(sb, "?xsputn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPBDH@Z",
                 {text(longText), uint32_t(longText.size())}) == longText.size());
    const uint32_t back = process.memory().alloc(128);
    CHECK(method(sb, "?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPADH@Z", {back, 128}) ==
              longText.size() &&
          cpu.string(back, uint32_t(longText.size())) == longText);
    const uint32_t pos = process.memory().alloc(24);
    method(sb, "?seekoff@strstreambuf@std@@MAE?AV?$fpos@H@2@JW4seekdir@ios_base@2@H@Z", {pos, 10, 0, 1});
    CHECK(cpu.read32(pos) == 10);
    CHECK(method(sb, "?xsgetn@?$basic_streambuf@DU?$char_traits@D@std@@@std@@MAEHPADH@Z", {back, 3}) == 3 &&
          cpu.string(back, 3) == "abc");
    method(sb, "??1strstreambuf@std@@UAE@XZ", {});

    // cerr << "n = " << 42 << " x = " << 1.5f << endl
    const uint32_t cerr = fn("?cerr@std@@3V?$basic_ostream@DU?$char_traits@D@std@@@1@A");
    const uint32_t putChars = fn("??6std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@0@AAV10@PBD@Z");
    CHECK(!cpu.call(putChars, {cerr, text("n = ")}, r) && r == cerr);
    method(cerr, "??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@H@Z", {42});
    CHECK(!cpu.call(putChars, {cerr, text(" x = ")}, r));
    const float x = 1.5f;
    uint32_t xbits;
    std::memcpy(&xbits, &x, 4);
    method(cerr, "??6?$basic_ostream@DU?$char_traits@D@std@@@std@@QAEAAV01@M@Z", {xbits});
    CHECK(!cpu.call(fn("?endl@std@@YAAAV?$basic_ostream@DU?$char_traits@D@std@@@1@AAV21@@Z"), {cerr}, r));
    CHECK(output == "n = 42 x = 1.5\n");
}

// USER32's windows and messages, the registry and COM, called as a
// program calls them; the window procedure is a native function recording
// the messages it gets.
static void testWindowsAndRegistry() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    auto fn = [&](const char *dll, const char *name) { return process.provided(dll, name); };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    std::vector<uint32_t> seen;
    const uint32_t proc = cpu.addNative("test!WndProc", [&](x86::Cpu &c) {
        seen.push_back(c.arg(1));
        c.reg[x86::Cpu::EAX] = c.arg(1) == 0x81 ? 1 : 0; // WM_NCCREATE goes on
        return 16u;
    });
    uint32_t r = 0;
    const uint32_t wc = process.memory().alloc(40);
    cpu.write32(wc + 4, proc);
    cpu.write32(wc + 36, text("TestClass"));
    CHECK(!cpu.call(fn("USER32.dll", "RegisterClassA"), {wc}, r) && r != 0);
    CHECK(!cpu.call(fn("USER32.dll", "CreateWindowExA"),
                    {0, text("testclass"), text("Title"), 0x00cf0000, 10, 20, 648, 507, 0, 0, 0, 0}, r) &&
          r != 0);
    const uint32_t hwnd = r;
    CHECK((seen == std::vector<uint32_t>{0x81, 0x1}));
    CHECK(!cpu.call(fn("USER32.dll", "FindWindowA"), {text("TESTCLASS"), 0}, r) && r == hwnd);
    CHECK(!cpu.call(fn("USER32.dll", "FindWindowA"), {text("Other"), 0}, r) && r == 0);
    // Subclassed: messages go to the new procedure, which can pass them to
    // the old one; other longs are kept.
    std::vector<uint32_t> subclassSeen;
    const uint32_t subclass = cpu.addNative("test!SubclassProc", [&](x86::Cpu &c) {
        subclassSeen.push_back(c.arg(1));
        c.reg[x86::Cpu::EAX] = 7;
        return 16u;
    });
    CHECK(!cpu.call(fn("USER32.dll", "SetWindowLongA"), {hwnd, uint32_t(-4), subclass}, r) && r == proc);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {hwnd, uint32_t(-4)}, r) && r == subclass);
    CHECK(!cpu.call(fn("USER32.dll", "SendMessageA"), {hwnd, 0x400, 0, 0}, r) && r == 7 &&
          subclassSeen == std::vector<uint32_t>{0x400});
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "CallWindowProcA"), {proc, hwnd, 0x401, 0, 0}, r) &&
          seen == std::vector<uint32_t>{0x401});
    CHECK(!cpu.call(fn("USER32.dll", "SetWindowLongA"), {hwnd, uint32_t(-4), proc}, r) && r == subclass);
    CHECK(!cpu.call(fn("USER32.dll", "SetWindowLongA"), {hwnd, uint32_t(-21), 0x1234}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {hwnd, uint32_t(-21)}, r) && r == 0x1234);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {hwnd, uint32_t(-16)}, r) && r == 0x00cf0000);
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "ShowWindow"), {hwnd, 1}, r) && r == 0);
    CHECK((seen == std::vector<uint32_t>{0x18, 0x1c, 0x6, 0x7}));
    // A thick frame (4) and a caption (19) around the client area.
    const uint32_t rc = process.memory().alloc(16);
    for (uint32_t v : {0u, 4u, 8u, 12u})
        cpu.write32(rc + v, v == 8 ? 640 : v == 12 ? 480 : 0);
    CHECK(!cpu.call(fn("USER32.dll", "AdjustWindowRect"), {rc, 0x00cf0000, 0}, r) && int32_t(cpu.read32(rc)) == -4 &&
          int32_t(cpu.read32(rc + 4)) == -23 && cpu.read32(rc + 8) == 644 && cpu.read32(rc + 12) == 484);
    CHECK(!cpu.call(fn("USER32.dll", "GetClientRect"), {hwnd, rc}, r) && cpu.read32(rc + 8) == 640 &&
          cpu.read32(rc + 12) == 480);
    // Posted messages come back from PeekMessageA and go to the procedure.
    const uint32_t msg = process.memory().alloc(28);
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 0);
    process.postMessage(hwnd, 0x100, 'Q', 0); // WM_KEYDOWN
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 && cpu.read32(msg) == hwnd &&
          cpu.read32(msg + 4) == 0x100);
    CHECK(!cpu.call(fn("USER32.dll", "TranslateMessage"), {msg}, r) && r == 1);
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "DispatchMessageA"), {msg}, r));
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 && cpu.read32(msg + 4) == 0x102 &&
          cpu.read32(msg + 8) == 'q');
    CHECK((seen == std::vector<uint32_t>{0x100}));
    // With Shift held, capitals and the symbols over the keys; without, the key's own.
    auto typed = [&](uint32_t vk) {
        while (!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1)
            ; // what's queued before, Shift's own WM_KEYDOWN among it
        process.postMessage(hwnd, 0x100, vk, 0);
        cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r);
        cpu.call(fn("USER32.dll", "TranslateMessage"), {msg}, r);
        return !cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 &&
                       cpu.read32(msg + 4) == 0x102
                   ? cpu.read32(msg + 8)
                   : 0u;
    };
    process.key(0x10, 0x2a, true);
    CHECK(typed('Q') == 'Q' && typed('2') == '@' && typed(0xbd) == '_');
    process.key(0x10, 0x2a, false);
    CHECK(typed(0xbd) == '-' && typed(0x63) == '3' && typed(0x70) == 0);
    CHECK(!cpu.call(fn("USER32.dll", "PostQuitMessage"), {3}, r));
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, hwnd, 0, 0, 1}, r) && r == 1 &&
          cpu.read32(msg + 4) == 0x12 && cpu.read32(msg + 8) == 3);
    // Closing through DefWindowProcA destroys the window.
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "DefWindowProcA"), {hwnd, 0x10, 0, 0}, r));
    CHECK((seen == std::vector<uint32_t>{0x2, 0x82}) && process.window(hwnd) == nullptr);

    // The registry: a key made, a value set and read back; Windows' own
    // locale list enumerated.
    const uint32_t key = process.memory().alloc(4);
    const uint32_t disposition = process.memory().alloc(4);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegCreateKeyExA"),
                    {0x80000002u, text("Software\\Test\\Game"), 0, 0, 0, 0, 0, key, disposition}, r) &&
          r == 0 && cpu.read32(disposition) == 1);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegSetValueExA"), {cpu.read32(key), text("Path"), 0, 1, text("D:\\"), 4}, r) &&
          r == 0);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegCloseKey"), {cpu.read32(key)}, r) && r == 0);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"), {0x80000002u, text("SOFTWARE\\test"), 0, 1, key}, r) &&
          r == 0);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"), {0x80000002u, text("Software\\Absent"), 0, 1, key}, r) &&
          r == 2);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"), {0x80000002u, text("Software\\Test\\Game"), 0, 1, key}, r) &&
          r == 0);
    const uint32_t type = process.memory().alloc(4);
    const uint32_t data = process.memory().alloc(16);
    const uint32_t size = process.memory().alloc(4);
    cpu.write32(size, 2);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegQueryValueExA"), {cpu.read32(key), text("path"), 0, type, data, size}, r) &&
          r == 234 && cpu.read32(size) == 4);
    cpu.write32(size, 16);
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegQueryValueExA"), {cpu.read32(key), text("path"), 0, type, data, size}, r) &&
          r == 0 && cpu.read32(type) == 1 && cpu.string(data) == "D:\\");
    CHECK(process.registryValue("HKEY_LOCAL_MACHINE\\Software\\Test\\Game", "Path").has_value());
    CHECK(!cpu.call(fn("ADVAPI32.dll", "RegOpenKeyExA"),
                    {0x80000002u, text("System\\CurrentControlSet\\Control\\Nls\\Locale"), 0, 1, key}, r) &&
          r == 0);
    const uint32_t name = process.memory().alloc(32);
    const uint32_t nameSize = process.memory().alloc(4);
    cpu.write32(nameSize, 32);
    cpu.write32(size, 16);
    CHECK(
        !cpu.call(fn("ADVAPI32.dll", "RegEnumValueA"), {cpu.read32(key), 1, name, nameSize, 0, type, data, size}, r) &&
        r == 0 && cpu.string(name) == "00000409" && cpu.read32(nameSize) == 8 && cpu.string(data) == "1");
    CHECK(
        !cpu.call(fn("ADVAPI32.dll", "RegEnumValueA"), {cpu.read32(key), 2, name, nameSize, 0, type, data, size}, r) &&
        r == 259);

    // COM: no class without the engine's; with it, the class asked for.
    const uint32_t guids = process.memory().alloc(32);
    const uint32_t out = process.memory().alloc(4);
    const uint8_t clsid[16] = {0xe0, 0xee, 0xf1, 0xd8, 0x34, 0xf6, 0xcf, 0x11,
                               0x87, 0,    0,    0xa0, 0x24, 0x5d, 0x91, 0x8b};
    for (uint32_t i = 0; i < 16; ++i)
        cpu.write8(guids + i, clsid[i]);
    CHECK(!cpu.call(fn("ole32.dll", "CoCreateInstance"), {guids, 0, 1, guids + 16, out}, r) && r == 0x80040154u);
    std::string asked;
    process.createComObject = [&](const std::string &c, const std::string &, uint32_t) {
        asked = c;
        return 0u;
    };
    CHECK(!cpu.call(fn("ole32.dll", "CoCreateInstance"), {guids, 0, 1, guids + 16, out}, r) && r == 0 &&
          asked == "{d8f1eee0-f634-11cf-8700-00a0245d918b}");
}

// DirectDraw and Direct3D 7 called through their vtables as a program
// calls them: a 64x48 fullscreen mode with a back buffer and Z buffer, a
// clear, a Gouraud triangle, a textured quad, the depth test, a flip.
static void testDirectX() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectX dx(process);
    dx.modes.push_back({64, 48, 16});
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    // Method `index` of the interface at `self`.
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        const bool ok = !cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r);
        CHECK(ok);
        return r;
    };
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    const uint32_t out = mem(4);
    CHECK(!cpu.call(process.provided("DDRAW.dll", "DirectDrawCreateEx"),
                    {0, out, guid("{15e65ec0-3b9c-11d2-b92f-00609797ea5b}"), 0}, r) &&
          r == 0);
    const uint32_t dd = cpu.read32(out);
    CHECK(call(dd, 20, {0, 0x11}) == 0);          // SetCooperativeLevel: exclusive, fullscreen
    CHECK(call(dd, 21, {64, 48, 16, 0, 0}) == 0); // SetDisplayMode
    CHECK(call(dd, 21, {65, 48, 16, 0, 0}) != 0); // a mode not offered
    // A flipping primary with one back buffer.
    const uint32_t desc = mem(124);
    auto clearDesc = [&] {
        std::memset(cpu.memory().data() + desc, 0, 124);
        cpu.write32(desc, 124);
    };
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x20); // CAPS, BACKBUFFERCOUNT
    cpu.write32(desc + 20, 1);
    cpu.write32(desc + 104, 0x200 | 0x10 | 0x8 | 0x2000); // PRIMARY, FLIP, COMPLEX, 3DDEVICE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);              // CreateSurface
    const uint32_t primary = cpu.read32(out);
    const uint32_t caps = mem(16);
    cpu.write32(caps, 0x4);                     // BACKBUFFER
    CHECK(call(primary, 12, {caps, out}) == 0); // GetAttachedSurface
    const uint32_t back = cpu.read32(out);
    // A 16-bit Z buffer, attached.
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4 | 0x40); // CAPS, HEIGHT, WIDTH, ZBUFFERBITDEPTH
    cpu.write32(desc + 8, 48);
    cpu.write32(desc + 12, 64);
    cpu.write32(desc + 104, 0x20000); // ZBUFFER
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    CHECK(call(back, 3, {cpu.read32(out)}) == 0); // AddAttachedSurface
    // Direct3D, from the DirectDraw object, and a device on the back buffer.
    CHECK(call(dd, 0, {guid("{f5049e77-4861-11d2-a407-00a0c90629a8}"), out}) == 0);
    const uint32_t d3d = cpu.read32(out);
    cpu.fpu.control = 0x027f; // 53 bits, as Windows starts a thread
    CHECK(call(d3d, 4, {guid("{84e63de0-46aa-11cf-816f-0000c020156e}"), back, out}) == 0); // CreateDevice
    const uint32_t device = cpu.read32(out);
    CHECK(cpu.fpu.control == 0x007f); // Direct3D leaves it in single precision

    // Clear to blue, then a red-green-blue triangle (screen coordinates).
    CHECK(call(device, 10, {0, 0, 3, 0xff0000ffu, bits(1.0f), 0}) == 0); // Clear target and Z
    CHECK(call(device, 20, {22, 1}) == 0);                               // CULLMODE: none
    const uint32_t verts = mem(4 * 28);
    auto vertex = [&](uint32_t i, float x, float y, float z, uint32_t color, float u = 0, float v = 0) {
        const uint32_t p = verts + i * 28; // XYZRHW, DIFFUSE, TEX1
        cpu.write32(p, bits(x));
        cpu.write32(p + 4, bits(y));
        cpu.write32(p + 8, bits(z));
        cpu.write32(p + 12, bits(1.0f));
        cpu.write32(p + 16, color);
        cpu.write32(p + 20, bits(u));
        cpu.write32(p + 24, bits(v));
    };
    const uint32_t fvf = 0x4 | 0x40 | 0x100;
    vertex(0, 0, 0, 0.5f, 0xffff0000u);
    vertex(1, 32, 0, 0.5f, 0xffff0000u);
    vertex(2, 0, 32, 0.5f, 0xffff0000u);
    CHECK(call(device, 37, {0, 1, 2}) == 0);             // stage 0 COLOROP: SELECTARG1
    CHECK(call(device, 37, {0, 2, 0}) == 0);             // COLORARG1: diffuse
    CHECK(call(device, 25, {4, fvf, verts, 3, 0}) == 0); // DrawPrimitive: a triangle list
    // A farther green triangle over it is hidden by the depth test.
    vertex(0, 0, 0, 0.9f, 0xff00ff00u);
    vertex(1, 32, 0, 0.9f, 0xff00ff00u);
    vertex(2, 0, 32, 0.9f, 0xff00ff00u);
    CHECK(call(device, 25, {4, fvf, verts, 3, 0}) == 0);
    // A textured quad (2x2 texels, point sampled) at the bottom right.
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4 | 0x1000);
    cpu.write32(desc + 8, 2);
    cpu.write32(desc + 12, 2);
    cpu.write32(desc + 72, 32);
    cpu.write32(desc + 76, 0x40); // RGB
    cpu.write32(desc + 84, 16);
    cpu.write32(desc + 88, 0xf800);
    cpu.write32(desc + 92, 0x07e0);
    cpu.write32(desc + 96, 0x001f);
    cpu.write32(desc + 104, 0x1000); // TEXTURE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t texture = cpu.read32(out);
    CHECK(call(texture, 25, {0, desc, 1, 0}) == 0); // Lock
    const uint32_t texels = cpu.read32(desc + 36);
    const uint32_t pitch = cpu.read32(desc + 16);
    cpu.write16(texels, 0xffff);             // white
    cpu.write16(texels + 2, 0xf800);         // red
    cpu.write16(texels + pitch, 0x07e0);     // green
    cpu.write16(texels + pitch + 2, 0x0000); // black
    CHECK(call(texture, 32, {0}) == 0);      // Unlock
    // A copy keyed by the source's colour key fails while it has none.
    CHECK(call(texture, 5, {0, texture, 0, 0x8000, 0}) == 0x887600d7u); // Blt, KEYSRC: DDERR_NOCOLORKEY
    const uint32_t key = mem(8);
    cpu.write32(key, 0), cpu.write32(key + 4, 0);
    CHECK(call(texture, 29, {0x8, key}) == 0); // SetColorKey: SRCBLT, black
    CHECK(call(texture, 5, {0, texture, 0, 0x8000, 0}) == 0);
    CHECK(call(device, 35, {0, texture}) == 0); // SetTexture
    CHECK(call(device, 37, {0, 1, 4}) == 0);    // COLOROP: MODULATE
    CHECK(call(device, 37, {0, 2, 2}) == 0);    // COLORARG1: texture
    CHECK(call(device, 37, {0, 3, 0}) == 0);    // COLORARG2: diffuse
    vertex(0, 40, 24, 0.5f, 0xffffffffu, 0, 0);
    vertex(1, 64, 24, 0.5f, 0xffffffffu, 1, 0);
    vertex(2, 40, 48, 0.5f, 0xffffffffu, 0, 1);
    vertex(3, 64, 48, 0.5f, 0xffffffffu, 1, 1);
    CHECK(call(device, 25, {5, fvf, verts, 4, 0}) == 0); // a strip
    CHECK(call(primary, 11, {0, 1}) == 0);               // Flip

    const x86::DirectX::Frame &f = dx.frame();
    CHECK(f.width == 64 && f.height == 48 && f.number == 1);
    auto at = [&](int x, int y) { return f.pixels[size_t(y * f.width + x)] & 0xffffff; };
    CHECK(at(5, 5) == 0xff0000);   // the near triangle
    CHECK(at(40, 5) == 0x0000ff);  // the clear colour
    CHECK(at(31, 31) == 0x0000ff); // past the hypotenuse
    CHECK(at(45, 28) == 0xffffff && at(58, 28) == 0xff0000 && at(45, 44) == 0x00ff00 && at(58, 44) == 0x000000);
    // The display's gamma ramp (IDirectDrawGammaControl, from the primary):
    // red turned upside down, shown from then on.
    CHECK(call(primary, 0, {guid("{69c11c3e-b46b-11d1-ad7a-00c04fc29b4e}"), out}) == 0);
    const uint32_t gamma = cpu.read32(out);
    const uint32_t ramp = mem(1536);
    for (uint32_t i = 0; i < 768; ++i)
        cpu.write16(ramp + 2 * i, uint16_t((i < 256 ? 255 - i : i % 256) * 257));
    CHECK(call(gamma, 4, {0, ramp}) == 0);                                   // SetGammaRamp
    CHECK(call(primary, 11, {0, 1}) == 0 && call(primary, 11, {0, 1}) == 0); // the drawing shown again
    CHECK(at(5, 5) == 0x000000 && at(40, 5) == 0xff00ff);
    const uint32_t read = mem(1536);
    CHECK(call(gamma, 3, {0, read}) == 0 && cpu.read16(read) == 0xffff && cpu.read16(read + 512) == 0); // GetGammaRamp
    for (uint32_t i = 0; i < 768; ++i)
        cpu.write16(ramp + 2 * i, uint16_t((i % 256) * 257));
    CHECK(call(gamma, 4, {0, ramp}) == 0);
    call(gamma, 2, {}); // Release: the primary keeps its other references
    // Shown a flip late: a flip shows the frame before, its drawing done;
    // the frame just flipped shows at the next.
    dx.presentLate = true;
    const uint64_t shown = f.number;
    CHECK(call(device, 10, {0, 0, 1, 0xff00ff00u, bits(1.0f), 0}) == 0); // Clear to green
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(f.number == shown); // the first late flip has nothing before it to show
    CHECK(call(device, 10, {0, 0, 1, 0xffffff00u, bits(1.0f), 0}) == 0); // yellow
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(f.number == shown + 1 && at(40, 5) == 0x00ff00); // the green one
    CHECK(call(primary, 11, {0, 1}) == 0);
    CHECK(f.number == shown + 2 && at(40, 5) == 0xffff00);
    dx.presentLate = false;
    // Drawing by rows on several threads draws the same as on one:
    // overlapping triangles, depth tested, bilinear, blended.
    CHECK(call(device, 37, {0, 16, 2}) == 0); // MAGFILTER: linear
    CHECK(call(device, 20, {27, 1}) == 0);    // ALPHABLENDENABLE
    CHECK(call(device, 20, {19, 5}) == 0);    // SRCBLEND: SRCALPHA
    CHECK(call(device, 20, {20, 6}) == 0);    // DESTBLEND: INVSRCALPHA
    auto scene = [&](x86::DirectX::DrawThreads threads) {
        dx.drawThreads = threads;
        CHECK(call(device, 10, {0, 0, 3, 0xff202020u, bits(1.0f), 0}) == 0);
        uint32_t seed = 7;
        auto next = [&] { return (seed = seed * 1103515245u + 12345u) >> 8; };
        for (int t = 0; t < 30; ++t) {
            for (uint32_t i = 0; i < 3; ++i)
                vertex(i, float(next() % 900) / 10 - 13, float(next() % 700) / 10 - 11, float(next() % 1000) / 1000,
                       next() | 0x40000000u, float(next() % 300) / 100, float(next() % 300) / 100);
            CHECK(call(device, 25, {4, fvf, verts, 3, 0}) == 0);
        }
        CHECK(call(back, 25, {0, desc, 1, 0}) == 0); // Lock
        const uint32_t pixels = cpu.read32(desc + 36);
        const uint32_t rowBytes = cpu.read32(desc + 16);
        std::vector<uint8_t> image(cpu.memory().begin() + pixels, cpu.memory().begin() + pixels + rowBytes * 48);
        CHECK(call(back, 32, {0}) == 0);
        return image;
    };
    const std::vector<uint8_t> one = scene(x86::DirectX::DrawThreads::kOne);
    CHECK(scene(x86::DirectX::DrawThreads::kAlways) == one);
    CHECK(std::adjacent_find(one.begin(), one.end(), std::not_equal_to<>()) != one.end()); // something was drawn
    // Release the device and the DirectDraw object's.
    CHECK(call(device, 2, {}) == 0);
}

// Input as the engine passes it on: window messages (with a double click
// for a class that asks for them), key states, the pointer kept within
// ClipCursor's rectangle; and DirectInput's mouse and keyboard, read as a
// state and as a buffer.
static void testInput() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    x86::DirectInput directInput(process);
    uint32_t now = 1000;
    process.milliseconds = [&] { return now; };
    auto fn = [&](const char *dll, const char *name) { return process.provided(dll, name); };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    const uint32_t proc = cpu.addNative("test!InputProc", [](x86::Cpu &c) {
        c.reg[x86::Cpu::EAX] = c.arg(1) == 0x81 ? 1 : 0;
        return 16u;
    });
    const uint32_t wc = process.memory().alloc(40);
    cpu.write32(wc, 0x8); // CS_DBLCLKS
    cpu.write32(wc + 4, proc);
    cpu.write32(wc + 36, text("Game"));
    CHECK(!cpu.call(fn("USER32.dll", "RegisterClassA"), {wc}, r) && r != 0);
    // A popup window (no frame) at 100,50.
    CHECK(!cpu.call(fn("USER32.dll", "CreateWindowExA"),
                    {0, text("Game"), 0, 0x80000000u, 100, 50, 320, 200, 0, 0, 0, 0}, r) &&
          r != 0);
    const uint32_t hwnd = r;
    const uint32_t msg = process.memory().alloc(28);
    auto next = [&]() -> std::array<uint32_t, 4> {
        if (cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) || !r)
            return {0, 0, 0, 0};
        return {cpu.read32(msg), cpu.read32(msg + 4), cpu.read32(msg + 8), cpu.read32(msg + 12)};
    };
    // Reluva's keys as Windows' virtual keys and scan codes: the keypad's
    // too, its / an extended key.
    uint32_t vk = 0;
    uint32_t scan = 0;
    CHECK(x86::windowsKey(key::kKpPlus, vk, scan) && vk == 0x6b && scan == 0x4e);
    CHECK(x86::windowsKey(key::kKpDivide, vk, scan) && vk == 0x6f && scan == 0x135);
    CHECK(x86::windowsKey(key::kKp0 + 5, vk, scan) && vk == 0x65 && scan == 0x4c);
    process.mouseMoveTo(130, 70);
    CHECK((next() == std::array<uint32_t, 4>{hwnd, 0x200, 0, 30 | 20u << 16}));
    process.mouseButton(0, true);
    CHECK((next() == std::array<uint32_t, 4>{hwnd, 0x201, 1, 30 | 20u << 16}));
    process.mouseButton(0, false);
    CHECK(next()[1] == 0x202);
    now += 200;
    process.mouseButton(0, true); // soon after, in the same place: a double click
    CHECK(next()[1] == 0x203);
    process.mouseButton(0, false);
    now += 1000;
    process.mouseButton(0, true); // too late for one
    CHECK(next()[1] == 0x202 && next()[1] == 0x201);
    process.mouseButton(0, false);
    next();
    process.key(0x41, 0x1e, true); // A
    auto k = next();
    CHECK(k[1] == 0x100 && k[2] == 0x41 && k[3] == (1 | 0x1eu << 16));
    CHECK(!cpu.call(fn("USER32.dll", "GetKeyState"), {0x41}, r) && (r & 0x8000));
    process.key(0x41, 0x1e, false);
    k = next();
    CHECK(k[1] == 0x101 && k[3] == (1 | 0x1eu << 16 | 3u << 30));
    CHECK(!cpu.call(fn("USER32.dll", "GetKeyState"), {0x41}, r) && !(r & 0x8000));
    // VkKeyScanA: a character's key on a US keyboard, Shift in the high byte.
    auto vkScan = [&](char ch) {
        CHECK(!cpu.call(fn("USER32.dll", "VkKeyScanA"), {uint32_t(uint8_t(ch))}, r));
        return r;
    };
    CHECK(vkScan('a') == 0x41 && vkScan('A') == 0x141 && vkScan('5') == 0x35 && vkScan('%') == 0x135);
    CHECK(vkScan(']') == 0xdd && vkScan('`') == 0xc0 && vkScan(';') == 0xba && vkScan('?') == 0x1bf);
    CHECK(vkScan(char(0xe9)) == 0xffff);
    // MapVirtualKeyA: scan codes both ways, and the character a key types.
    auto mapKey = [&](uint32_t code, uint32_t how) {
        CHECK(!cpu.call(fn("USER32.dll", "MapVirtualKeyA"), {code, how}, r));
        return r;
    };
    CHECK(mapKey(0x41, 0) == 0x1e && mapKey(0x1e, 1) == 0x41 && mapKey(0x2a, 1) == 0x10 && mapKey(0x2a, 3) == 0xa0);
    CHECK(mapKey(0xdd, 2) == ']' && mapKey(0xc0, 2) == '`' && mapKey(0x41, 2) == 'A' && mapKey(0x35, 2) == '5');
    CHECK(mapKey(0x70, 2) == 0); // F1 types nothing
    // ClipCursor keeps the pointer in.
    const uint32_t rc = process.memory().alloc(16);
    for (uint32_t i = 0; i < 4; ++i)
        cpu.write32(rc + 4 * i, i == 0 ? 100 : i == 1 ? 50 : i == 2 ? 420 : 250);
    CHECK(!cpu.call(fn("USER32.dll", "ClipCursor"), {rc}, r) && r == 1);
    process.mouseMoveTo(1000, 0);
    CHECK(process.cursorX == 419 && process.cursorY == 50);
    CHECK(!cpu.call(fn("USER32.dll", "ClipCursor"), {0}, r));
    process.mouseMoveTo(130, 70);
    while (next()[1]) {}

    // DirectInput.
    auto guid = [&](const char *g) {
        const uint32_t a = process.memory().alloc(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        const bool ok = !cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r);
        CHECK(ok);
        return r;
    };
    const uint32_t out = process.memory().alloc(4);
    CHECK(!cpu.call(fn("DINPUT.dll", "DirectInputCreateA"), {0, 0x700, out, 0}, r) && r == 0);
    const uint32_t di = cpu.read32(out);
    CHECK(call(di, 3, {guid("{6f1d2b60-d5a0-11cf-bfc7-444553540000}"), out, 0}) == 0); // CreateDevice: the mouse
    const uint32_t mouse = cpu.read32(out);
    // A mouse format as c_dfDIMouse is: X, Y, Z (LONGs), four buttons (BYTEs).
    const uint32_t objects = process.memory().alloc(7 * 16);
    const uint32_t format = process.memory().alloc(24);
    const uint32_t axisGuids[3] = {guid("{a36d02e0-c9f3-11cf-bfc7-444553540000}"),
                                   guid("{a36d02e1-c9f3-11cf-bfc7-444553540000}"),
                                   guid("{a36d02e2-c9f3-11cf-bfc7-444553540000}")};
    for (uint32_t i = 0; i < 7; ++i) {
        const uint32_t o = objects + 16 * i;
        cpu.write32(o, i < 3 ? axisGuids[i] : 0);
        cpu.write32(o + 4, i < 3 ? 4 * i : 12 + (i - 3));
        cpu.write32(o + 8, i < 3 ? 0x3 | 0xffff00u : 0xc | 0xffff00u | (i > 4 ? 0x80000000u : 0));
    }
    const uint32_t fmt[6] = {24, 16, 2 /* relative axes */, 16, 7, objects};
    for (uint32_t i = 0; i < 6; ++i)
        cpu.write32(format + 4 * i, fmt[i]);
    CHECK(call(mouse, 11, {format}) == 0); // SetDataFormat
    const uint32_t state = process.memory().alloc(256);
    CHECK(call(mouse, 9, {16, state}) == 0x8007000cu); // GetDeviceState before Acquire
    const uint32_t prop = process.memory().alloc(20);
    const uint32_t bufferSize[5] = {20, 16, 0, 0, 8};
    for (uint32_t i = 0; i < 5; ++i)
        cpu.write32(prop + 4 * i, bufferSize[i]);
    CHECK(call(mouse, 6, {1, prop}) == 0); // SetProperty: DIPROP_BUFFERSIZE
    CHECK(call(mouse, 7, {}) == 0);        // Acquire
    process.mouseMoveTo(140, 65);
    process.mouseButton(1, true);
    CHECK(call(mouse, 9, {16, state}) == 0);
    CHECK(int32_t(cpu.read32(state)) == 10 && int32_t(cpu.read32(state + 4)) == -5 && cpu.read32(state + 8) == 0 &&
          cpu.read8(state + 12) == 0 && cpu.read8(state + 13) == 0x80);
    CHECK(call(mouse, 9, {16, state}) == 0 && cpu.read32(state) == 0); // the movement was read
    // The buffer: X moved, Y moved, button 1 pressed.
    const uint32_t data = process.memory().alloc(8 * 16);
    const uint32_t count = process.memory().alloc(4);
    cpu.write32(count, 8);
    CHECK(call(mouse, 10, {16, data, count, 0}) == 0 && cpu.read32(count) == 3);
    CHECK(cpu.read32(data) == 0 && cpu.read32(data + 4) == 10 && cpu.read32(data + 16) == 4 &&
          int32_t(cpu.read32(data + 20)) == -5 && cpu.read32(data + 32) == 13 && cpu.read32(data + 36) == 0x80);
    process.mouseButton(1, false);

    // The keyboard, with a format of 256 keys by scan code.
    CHECK(call(di, 3, {guid("{6f1d2b61-d5a0-11cf-bfc7-444553540000}"), out, 0}) == 0);
    const uint32_t keyboard = cpu.read32(out);
    const uint32_t keys = process.memory().alloc(256 * 16);
    const uint32_t keyGuid = guid("{55728220-d33c-11cf-bfc7-444553540000}");
    for (uint32_t i = 0; i < 256; ++i) {
        cpu.write32(keys + 16 * i, keyGuid);
        cpu.write32(keys + 16 * i + 4, i);
        cpu.write32(keys + 16 * i + 8, 0x8000000cu | i << 8);
    }
    const uint32_t keyFmt[6] = {24, 16, 0, 256, 256, keys};
    for (uint32_t i = 0; i < 6; ++i)
        cpu.write32(format + 4 * i, keyFmt[i]);
    CHECK(call(keyboard, 11, {format}) == 0);
    CHECK(call(keyboard, 7, {}) == 0);
    process.key(0x41, 0x1e, true);
    CHECK(call(keyboard, 9, {256, state}) == 0 && cpu.read8(state + 0x1e) == 0x80 && cpu.read8(state + 0x1f) == 0);
    process.key(0x41, 0x1e, false);
    CHECK(call(keyboard, 9, {256, state}) == 0 && cpu.read8(state + 0x1e) == 0);
}

// A3D created as a program creates it, a source made, filled through Lock,
// played once and looped, as the clock moves.
// A DLL loaded with LoadLibraryA twice, freed twice (its DllMain told it's
// leaving at the second), and loaded again, started afresh: as Starfleet
// Command II loads its missions to list them, frees them, and loads them
// to play.
static void testDllUnload(const std::filesystem::path &tmp) {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    process.setProgram("Prog.exe", "C:\\Game\\");
    std::filesystem::create_directories(tmp / "Dll");
    process.mapPath = [&](const std::string &path, bool) -> std::optional<std::filesystem::path> {
        if (path == "C:\\Game\\Counter.dll")
            return tmp / "Dll" / "Counter.dll";
        return std::nullopt;
    };
    // DllMain counts its calls by reason, outside the DLL (counts[reason]).
    const uint32_t counts = process.memory().alloc(16);
    for (uint32_t i = 0; i < 16; i += 4)
        cpu.write32(counts + i, 0);
    std::vector<uint8_t> dll(0x400, 0);
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(dll.data() + at, &v, 2); };
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(dll.data() + at, &v, 4); };
    dll[0] = 'M', dll[1] = 'Z';
    put32(0x3c, 0x40);
    std::memcpy(dll.data() + 0x40, "PE\0\0", 4);
    put16(0x44, 0x14c);  // i386
    put16(0x46, 1);      // one section
    put16(0x54, 0xe0);   // the optional header's size
    put16(0x56, 0x2102); // a DLL, executable, 32-bit
    const size_t opt = 0x58;
    put16(opt, 0x10b);
    put32(opt + 16, 0x1000);     // its entry point
    put32(opt + 28, 0x10000000); // its base
    put32(opt + 32, 0x1000);     // section alignment
    put32(opt + 36, 0x200);      // file alignment
    put32(opt + 56, 0x2000);     // its size in memory
    put32(opt + 60, 0x200);      // its headers' size
    put32(opt + 92, 16);         // data directories
    const size_t sec = opt + 0xe0;
    std::memcpy(dll.data() + sec, ".text", 5);
    put32(sec + 8, 0x1000);
    put32(sec + 12, 0x1000);
    put32(sec + 16, 0x200);
    put32(sec + 20, 0x200);
    put32(sec + 36, 0x60000020);
    // mov eax, [esp+8]; inc dword [eax*4 + counts]; mov eax, 1; ret 12
    const uint8_t code[] = {0x8b, 0x44, 0x24, 0x08, 0xff, 0x04, 0x85, 0, 0, 0, 0, 0xb8, 1, 0, 0, 0, 0xc2, 0x0c, 0};
    std::memcpy(dll.data() + 0x200, code, sizeof code);
    put32(0x200 + 7, counts);
    {
        std::ofstream out(tmp / "Dll" / "Counter.dll", std::ios::binary);
        out.write(reinterpret_cast<const char *>(dll.data()), std::streamsize(dll.size()));
    }
    uint32_t r = 0;
    auto call = [&](const char *name, uint32_t arg) {
        CHECK(!cpu.call(process.provided("KERNEL32.dll", name), {arg}, r));
        return r;
    };
    const uint32_t name = process.memory().alloc(32);
    cpu.writeString(name, "Counter.dll", 32);
    const uint32_t first = call("LoadLibraryA", name);
    CHECK(first != 0 && cpu.read32(counts + 4) == 1); // DLL_PROCESS_ATTACH, once
    CHECK(call("LoadLibraryA", name) == first && cpu.read32(counts + 4) == 1);
    CHECK(call("FreeLibrary", first) == 1 && cpu.read32(counts) == 0); // still loaded once
    CHECK(call("FreeLibrary", first) == 1 && cpu.read32(counts) == 1); // DLL_PROCESS_DETACH
    CHECK(process.modules().size() == 0);
    const uint32_t again = call("LoadLibraryA", name);
    CHECK(again != 0 && cpu.read32(counts + 4) == 2); // started again
    CHECK(call("FreeLibrary", 0x12345678) == 1);      // not one of its modules: nothing done
    CHECK(process.modules().size() == 1);
}

// What running Starfleet Command II 2.0.3.6's patcher and the patched game
// asked of Windows: a file opened OPEN_ALWAYS starting as the install's,
// a seek before a file's start refused as Windows refuses it, a writable
// file mapping's view written back, MoveFileExA replacing a file, the
// time conversions, the wide command line, and wvsprintfA.
static void testPatcherCalls(const std::filesystem::path &tmp) {
    namespace stdfs = std::filesystem;
    stdfs::create_directories(tmp / "Install");
    stdfs::create_directories(tmp / "Save");
    std::ofstream(tmp / "Install" / "Old.txt", std::ios::binary) << "install";
    x86::Cpu cpu(32u << 20);
    {
        x86::Win32Process process(cpu);
        process.setProgram("Prog.exe", "C:\\Game\\");
        // The install read, the save folder written, as the game has them.
        process.mapPath = [&](const std::string &path, bool forWriting) -> std::optional<stdfs::path> {
            if (path.rfind("C:\\Game\\", 0) != 0)
                return std::nullopt;
            const std::string name = path.substr(8);
            if (forWriting)
                return tmp / "Save" / name;
            if (stdfs::exists(tmp / "Save" / name))
                return tmp / "Save" / name;
            return tmp / "Install" / name;
        };
        uint32_t r = 0;
        auto fn = [&](const char *dll, const char *name) { return process.provided(dll, name); };
        const uint32_t name = process.memory().alloc(32);
        cpu.writeString(name, "Old.txt", 32);
        CHECK(!cpu.call(fn("KERNEL32.dll", "CreateFileA"), {name, 0xc0000000u, 3, 0, 4, 0x80, 0}, r));
        const uint32_t h = r;
        CHECK(h != 0xffffffffu);
        const uint32_t buffer = process.memory().alloc(16);
        const uint32_t done = process.memory().alloc(4);
        CHECK(!cpu.call(fn("KERNEL32.dll", "ReadFile"), {h, buffer, 7, done, 0}, r) && r == 1 &&
              cpu.read32(done) == 7 && cpu.string(buffer, 7) == "install");
        CHECK(!cpu.call(fn("KERNEL32.dll", "SetFilePointer"), {h, uint32_t(-100), 0, 0}, r) && r == 0xffffffffu);
        CHECK(!cpu.call(fn("KERNEL32.dll", "GetLastError"), {}, r) && r == 131); // ERROR_NEGATIVE_SEEK
        CHECK(!cpu.call(fn("KERNEL32.dll", "CloseHandle"), {h}, r));
        // Time: SYSTEMTIME to FILETIME and back (28 October 2002, 12:34:56).
        const uint32_t st = process.memory().alloc(16);
        const uint16_t fields[8] = {2002, 10, 1, 28, 12, 34, 56, 0};
        for (int i = 0; i < 8; ++i)
            cpu.write16(st + 2 * uint32_t(i), fields[i]);
        const uint32_t ft = process.memory().alloc(8);
        CHECK(!cpu.call(fn("KERNEL32.dll", "SystemTimeToFileTime"), {st, ft}, r) && r == 1);
        CHECK((cpu.read32(ft) | uint64_t(cpu.read32(ft + 4)) << 32) ==
              uint64_t(1035808496) * 10000000 + 116444736000000000ull);
        const uint32_t back = process.memory().alloc(16);
        CHECK(!cpu.call(fn("KERNEL32.dll", "FileTimeToSystemTime"), {ft, back}, r) && r == 1);
        for (int i = 0; i < 7; ++i)
            CHECK(cpu.read16(back + 2 * uint32_t(i)) == fields[i]);
        // The command line, wide.
        process.setCommandLine("patch.exe C:\\");
        CHECK(!cpu.call(fn("KERNEL32.dll", "GetCommandLineW"), {}, r) && cpu.read16(r) == 'p' &&
              cpu.read16(r + 2 * 10) == 'C' && cpu.read16(r + 2 * 13) == 0);
        // wvsprintfA(out, format, arguments)
        const uint32_t format = process.memory().alloc(16);
        cpu.writeString(format, "%s=%d", 16);
        const uint32_t args = process.memory().alloc(8);
        cpu.write32(args, name);
        cpu.write32(args + 4, 42);
        CHECK(!cpu.call(fn("USER32.dll", "wvsprintfA"), {buffer, format, args}, r) && r == 10 &&
              cpu.string(buffer, 16) == "Old.txt=42");
    }
    // Drives: a writable mapping's view goes back to its file; MoveFileExA
    // replaces a file with MOVEFILE_REPLACE_EXISTING.
    x86::Win32Process process(cpu);
    x86::Win32Process::Drive c;
    c.letter = 'C';
    c.root = tmp / "Install";
    process.addDrive(c);
    uint32_t r = 0;
    auto fn = [&](const char *name) { return process.provided("KERNEL32.dll", name); };
    const uint32_t name = process.memory().alloc(32);
    cpu.writeString(name, "C:\\New.bin", 32);
    CHECK(!cpu.call(fn("CreateFileA"), {name, 0xc0000000u, 0, 0, 2, 0x80, 0}, r) && r != 0xffffffffu);
    const uint32_t file = r;
    CHECK(!cpu.call(fn("CreateFileMappingA"), {file, 0, 4, 0, 16, 0}, r) && r != 0);
    const uint32_t mapping = r;
    CHECK(!cpu.call(fn("MapViewOfFile"), {mapping, 2, 0, 0, 0}, r) && r != 0);
    const uint32_t view = r;
    for (uint32_t i = 0; i < 16; ++i)
        cpu.write8(view + i, uint8_t('a' + i));
    CHECK(!cpu.call(fn("UnmapViewOfFile"), {view}, r) && r == 1);
    CHECK(!cpu.call(fn("CloseHandle"), {mapping}, r));
    CHECK(!cpu.call(fn("CloseHandle"), {file}, r));
    {
        std::ifstream in(tmp / "Install" / "New.bin", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(bytes == "abcdefghijklmnop");
    }
    const uint32_t old = process.memory().alloc(32);
    cpu.writeString(old, "C:\\Old.txt", 32);
    CHECK(!cpu.call(fn("MoveFileExA"), {name, old, 0}, r) && r == 0); // there already
    CHECK(!cpu.call(fn("MoveFileExA"), {name, old, 1}, r) && r == 1);
    CHECK(!stdfs::exists(tmp / "Install" / "New.bin") && stdfs::file_size(tmp / "Install" / "Old.txt") == 16);
}

// The desktop's stand-ins (win32_desktop.cpp) and the INI files'
// (win32_files.cpp): what a game finds of Windows around it.
static void testDesktop(const std::filesystem::path &tmp) {
    namespace stdfs = std::filesystem;
    stdfs::create_directories(tmp);
    std::ofstream(tmp / "game.ini", std::ios::binary) << "; settings\r\n[Video]\r\n  Width = 640 \r\n"
                                                         "Name=\"Two words\"\r\nRescale=1\t; a comment\r\n[Other]\r\n"
                                                         "Width=800\r\n";
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    process.setProgram("Prog.exe", "C:\\Game\\");
    process.mapPath = [&](const std::string &path, bool) -> std::optional<stdfs::path> {
        if (path.rfind("C:\\Game\\", 0) != 0)
            return std::nullopt;
        return tmp / path.substr(8);
    };
    uint32_t r = 0;
    auto fn = [&](const char *dll, const char *name) { return process.provided(dll, name); };
    const auto text = [&](const std::string &s) {
        const uint32_t at = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(at, s, uint32_t(s.size() + 1));
        return at;
    };
    const uint32_t buffer = process.memory().alloc(64);

    // INI files: names and sections without case, the value trimmed and
    // unquoted, a comment after it kept (as Windows does), the default
    // where there's no key, and no file named without a path in the game's
    // folder (Windows looks in its own).
    const uint32_t ini = text("C:\\Game\\game.ini");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileIntA"), {text("video"), text("WIDTH"), 5, ini}, r) &&
          r == 640);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileIntA"), {text("Video"), text("Rescale"), 5, ini}, r) &&
          r == 1);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileStringA"),
                    {text("Video"), text("Name"), text("x"), buffer, 64, ini}, r) &&
          r == 9 && cpu.string(buffer) == "Two words");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileStringA"),
                    {text("Video"), text("Rescale"), text("x"), buffer, 64, ini}, r) &&
          cpu.string(buffer) == "1\t; a comment");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileStringA"),
                    {text("Video"), text("Depth"), text("16"), buffer, 3, ini}, r) &&
          r == 2 && cpu.string(buffer) == "16");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileStringA"),
                    {text("Video"), text("Name"), text("none"), buffer, 4, ini}, r) &&
          r == 3 && cpu.string(buffer) == "Two");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetPrivateProfileIntA"), {text("Video"), text("Width"), 5, text("game.ini")},
                    r) &&
          r == 5);

    // Global memory: a handle is its block, kept on a resize.
    CHECK(!cpu.call(fn("KERNEL32.dll", "GlobalAlloc"), {0, 8}, r) && r != 0);
    const uint32_t block = r;
    cpu.writeString(block, "global", 8);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GlobalLock"), {block}, r) && r == block);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GlobalReAlloc"), {block, 64, 0}, r) && r != 0 && cpu.string(r) == "global");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GlobalFree"), {r}, r) && r == 0);

    // Strings and the locale.
    const uint32_t word = text("Word");
    CHECK(!cpu.call(fn("KERNEL32.dll", "lstrlenA"), {word}, r) && r == 4);
    CHECK(!cpu.call(fn("KERNEL32.dll", "CompareStringA"), {0x409, 1, word, uint32_t(-1), text("WORD"), uint32_t(-1)},
                    r) &&
          r == 2);
    CHECK(!cpu.call(fn("KERNEL32.dll", "CompareStringA"), {0x409, 0, word, uint32_t(-1), text("Wore"), 3}, r) &&
          r == 3);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLocaleInfoA"), {0x409, 0x1004, buffer, 8}, r) && r == 5 &&
          cpu.string(buffer) == "1252");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLocaleInfoA"), {0x409, 0x1004, buffer, 2}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "InterlockedCompareExchange"), {block, 7, 0}, r));

    // The folders, the focus, and what isn't there: no program started,
    // the file dialogs cancelled, the clipboard closed.
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetWindowsDirectoryA"), {buffer, 64}, r) && r == 10 &&
          cpu.string(buffer) == "C:\\WINDOWS");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetSystemDirectoryW"), {buffer, 64}, r) && r == 17 &&
          cpu.read16(buffer + 2 * 16) == 'M' && cpu.read16(buffer + 2 * 17) == 0);
    CHECK(!cpu.call(fn("USER32.dll", "SetFocus"), {0x1234}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "GetActiveWindow"), {}, r) && r == 0x1234);
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateProcessA"), {0, text("notepad.exe"), 0, 0, 0, 0, 0, 0, 0, 0}, r) &&
          r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLastError"), {}, r) && r == 2);
    CHECK(!cpu.call(fn("comdlg32.dll", "GetOpenFileNameA"), {buffer}, r) && r == 0);
    CHECK(!cpu.call(fn("comdlg32.dll", "CommDlgExtendedError"), {}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "OpenClipboard"), {0}, r) && r == 0);
}

static void testDirectPlayLobby() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectPlay directPlay(process);
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    const uint32_t out = mem(4);
    const uint32_t create = process.provided("ole32.dll", "CoCreateInstance");
    // Other classes aren't its.
    CHECK(!cpu.call(create,
                    {guid("{11111111-2222-3333-4444-555555555555}"), 0, 1,
                     guid("{2db72491-652c-11d1-a7a8-0000f803abfc}"), out},
                    r) &&
          r != 0);
    CHECK(!cpu.call(create,
                    {guid("{2fe8f810-b2a5-11d0-a787-0000f803abfc}"), 0, 1,
                     guid("{2db72491-652c-11d1-a7a8-0000f803abfc}"), out},
                    r) &&
          r == 0);
    const uint32_t lobby = cpu.read32(out);
    CHECK(lobby != 0);
    // GetConnectionSettings(0, null, &size): not started by a lobby,
    // DPERR_NOTLOBBIED; the size left as it was.
    const uint32_t size = mem(4);
    cpu.write32(size, 0);
    CHECK(!cpu.call(cpu.read32(cpu.read32(lobby) + 4 * 8), {lobby, 0, 0, size}, r) && r == 0x8877042eu);
    CHECK(cpu.read32(size) == 0);
    CHECK(!cpu.call(cpu.read32(cpu.read32(lobby) + 4 * 2), {lobby}, r) && r == 0); // Release
}

// A DirectPlay session hosted by one process: its connection, players and
// messages, as Starfleet Command II uses them.
static void testDirectPlaySession() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectPlay directPlay(process);
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](uint32_t at, const char *g) {
        x86::ComHost::writeGuid(cpu, at, g);
        return at;
    };
    const uint32_t out = mem(4);
    CHECK(!cpu.call(process.provided("ole32.dll", "CoCreateInstance"),
                    {guid(mem(16), "{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}"), 0, 1,
                     guid(mem(16), "{0ab1c531-4745-11d1-a7a1-0000f803abfc}"), out},
                    r) &&
          r == 0);
    const uint32_t play = cpu.read32(out);
    // IDirectPlay4A's methods by their place in its vtable.
    auto call = [&](int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), play);
        CHECK(!cpu.call(cpu.read32(cpu.read32(play) + 4 * uint32_t(index)), args, r));
        return r;
    };
    enum {
        kCreatePlayer = 6,
        kDestroyPlayer = 9,
        kGetPlayerAddress = 18,
        kGetPlayerData = 20,
        kGetSessionDesc = 22,
        kOpen = 24,
        kReceive = 25,
        kSend = 26,
        kSetPlayerData = 29,
        kInitializeConnection = 38,
        kGetPlayerFlags = 46,
        kSendEx = 49
    };
    // The TCP/IP connection, as EnumConnections gives it: the address's
    // total size, then its provider.
    const uint32_t address = mem(60);
    guid(address, "{1318f560-912c-11d0-9daa-00a0c90a43cb}");
    cpu.write32(address + 16, 4);
    cpu.write32(address + 20, 60);
    guid(address + 24, "{07d916c0-e0af-11cf-9c4e-00a0c905425e}");
    cpu.write32(address + 40, 16);
    guid(address + 44, "{36e95ee0-8577-11cf-960c-0080c7534e82}");
    CHECK(call(kInitializeConnection, {address, 0}) == 0);
    const uint32_t size = mem(4);
    cpu.write32(size, 0);
    CHECK(call(kGetPlayerAddress, {0, 0, size}) == 0x8877001eu && cpu.read32(size) == 60);
    // Open(DPOPEN_CREATE): a session named "Duel" for four.
    const uint32_t desc = mem(80);
    cpu.write32(desc, 80);
    cpu.write32(desc + 40, 4);
    const uint32_t name = mem(8);
    cpu.writeString(name, "Duel", 8);
    cpu.write32(desc + 48, name);
    CHECK(call(kOpen, {desc, 0x82}) == 0);
    const uint32_t got = mem(200);
    cpu.write32(size, 200);
    CHECK(call(kGetSessionDesc, {got, size}) == 0 && cpu.read32(got + 40) == 4);
    CHECK(cpu.string(cpu.read32(got + 48)) == "Duel" && x86::ComHost::guid(cpu, got + 8) != "{}");
    // Two local players: the first told of the second, by name.
    const uint32_t ids = mem(8);
    const uint32_t dpName = mem(16 + 8);
    cpu.write32(dpName, 16);
    cpu.write32(dpName + 8, dpName + 16);
    cpu.writeString(dpName + 16, "Kirk", 8);
    CHECK(call(kCreatePlayer, {ids, dpName, 0, 0, 0, 0}) == 0);
    cpu.writeString(dpName + 16, "Plato", 8);
    const uint32_t platoData = mem(4);
    cpu.write32(platoData, 0xa1a1a1a1);
    CHECK(call(kCreatePlayer, {ids + 4, dpName, 0, platoData, 4, 0}) == 0);
    const uint32_t kirk = cpu.read32(ids);
    const uint32_t plato = cpu.read32(ids + 4);
    CHECK(kirk > 1 && plato > 1 && kirk != plato);
    CHECK(call(kGetPlayerFlags, {kirk, out}) == 0 && cpu.read32(out) == 8);
    const uint32_t from = mem(4);
    const uint32_t to = mem(4);
    const uint32_t buffer = mem(256);
    auto receive = [&](uint32_t room) {
        cpu.write32(size, room);
        return call(kReceive, {from, to, 1, room ? buffer : 0, size});
    };
    CHECK(receive(0) == 0x8877001eu); // DPERR_BUFFERTOOSMALL, and the size
    CHECK(receive(256) == 0 && cpu.read32(from) == 0 && cpu.read32(to) == kirk);
    CHECK(cpu.read32(buffer) == 3 && cpu.read32(buffer + 4) == 1 && cpu.read32(buffer + 8) == plato); // created
    CHECK(cpu.string(cpu.read32(buffer + 24 + 8)) == "Plato");
    CHECK(cpu.read32(buffer + 20) == 4 && cpu.read32(cpu.read32(buffer + 16)) == 0xa1a1a1a1);
    CHECK(receive(256) == 0x887700beu); // DPERR_NOMESSAGES
    // A message to every player but its sender; one to a player.
    const uint32_t data = mem(4);
    cpu.write32(data, 0x12345678);
    CHECK(call(kSend, {kirk, 0, 0, data, 4}) == 0);
    CHECK(receive(256) == 0 && cpu.read32(from) == kirk && cpu.read32(to) == plato && cpu.read32(size) == 4 &&
          cpu.read32(buffer) == 0x12345678);
    CHECK(receive(256) == 0x887700beu);
    // Sent asynchronously, it's under way (E_PENDING).
    CHECK(call(kSendEx, {plato, kirk, 0x601, data, 4, 0, 0, 0, 0}) == 0x8000000au);
    CHECK(receive(256) == 0 && cpu.read32(from) == plato && cpu.read32(to) == kirk);
    // Data every player sees: set, read back, the other player told.
    CHECK(call(kSetPlayerData, {kirk, data, 4, 2}) == 0);
    cpu.write32(size, 16);
    CHECK(call(kGetPlayerData, {kirk, buffer, size, 0}) == 0 && cpu.read32(size) == 4 &&
          cpu.read32(buffer) == 0x12345678);
    CHECK(receive(256) == 0 && cpu.read32(to) == plato && cpu.read32(buffer) == 0x102 &&
          cpu.read32(buffer + 8) == kirk && cpu.read32(cpu.read32(buffer + 12)) == 0x12345678);
    // A player destroyed: the other told; one not there refused.
    CHECK(call(kDestroyPlayer, {plato}) == 0);
    CHECK(receive(256) == 0 && cpu.read32(to) == kirk && cpu.read32(buffer) == 5 && cpu.read32(buffer + 8) == plato);
    CHECK(cpu.read32(buffer + 24) == 4 && cpu.read32(cpu.read32(buffer + 20)) == 0xa1a1a1a1); // its remote data
    CHECK(cpu.string(cpu.read32(buffer + 28 + 8)) == "Plato");
    CHECK(call(kDestroyPlayer, {plato}) == 0x88770096u);
}

static void testMiles() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 0;
    process.milliseconds = [&] { return clock; };
    x86::Miles miles(process);
    audio::Mixer mixer(22050);
    miles.mixer = &mixer;
    uint32_t r = 0;
    auto ail = [&](const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided("mss32.dll", name), args, r));
        return r;
    };
    const uint32_t out = process.memory().alloc(4);
    ail("_AIL_startup@0", {});
    CHECK(ail("_AIL_waveOutOpen@16", {out, 0, 0xffffffff, 0}) == 0 && cpu.read32(out) != 0);
    const uint32_t driver = cpu.read32(out);
    // A tenth of a second, in 256-byte pieces: 22,050 Hz 16-bit stereo, and IMA ADPCM mono.
    CHECK(ail("_AIL_minimum_sample_buffer_size@12", {driver, 22050, 3}) == 8960);
    CHECK(ail("_AIL_minimum_sample_buffer_size@12", {driver, 22050, 4}) == 1280);
    const uint32_t sample = ail("_AIL_allocate_sample_handle@4", {driver});
    CHECK(sample != 0 && miles.samplesAllocated() == 1);
    ail("_AIL_init_sample@4", {sample});
    CHECK(ail("_AIL_sample_status@4", {sample}) == 2); // done: nothing loaded
    // 1000 Hz 16-bit mono, two buffers of a quarter of a second each.
    ail("_AIL_set_sample_type@12", {sample, 1, 0});
    ail("_AIL_set_sample_playback_rate@8", {sample, 1000});
    const uint32_t buffers[2] = {process.memory().alloc(500), process.memory().alloc(500)};
    for (const uint32_t b : buffers)
        for (uint32_t i = 0; i < 250; ++i)
            cpu.write16(b + 2 * i, 0x2000);
    std::vector<int> loaded;
    miles.bufferLoaded = [&](uint32_t s, const std::vector<int16_t> &pcm, int rate, int channels) {
        CHECK(s == sample && rate == 1000 && channels == 1 && pcm.size() == 250 && pcm[0] == 0x2000);
        loaded.push_back(int(pcm.size()));
    };
    // The two buffers in turn, then neither until the first has been heard.
    CHECK(ail("_AIL_sample_buffer_ready@4", {sample}) == 0);
    ail("_AIL_load_sample_buffer@16", {sample, 0, buffers[0], 500});
    CHECK(ail("_AIL_sample_status@4", {sample}) == 4); // playing
    CHECK(ail("_AIL_sample_buffer_ready@4", {sample}) == 1);
    ail("_AIL_load_sample_buffer@16", {sample, 1, buffers[1], 500});
    CHECK(ail("_AIL_sample_buffer_ready@4", {sample}) == 0xffffffff);
    clock = 200;
    CHECK(ail("_AIL_sample_buffer_ready@4", {sample}) == 0xffffffff);
    clock = 260;
    CHECK(ail("_AIL_sample_buffer_ready@4", {sample}) == 0);
    CHECK(ail("_AIL_sample_buffer_ready@4", {sample}) == 0);
    CHECK(miles.samplesPlaying() == 1);
    // What was loaded is heard, at full volume: 0x2000.
    std::vector<int16_t> mixed(2 * 22050);
    mixer.mix(mixed.data(), 22050);
    CHECK(*std::max_element(mixed.begin(), mixed.end()) == 0x2000);
    // Both heard: done.
    clock = 600;
    CHECK(ail("_AIL_sample_status@4", {sample}) == 2 && miles.samplesPlaying() == 0);
    CHECK(loaded.size() == 2);
    // IMA ADPCM, a block of 4 header bytes and 4 of samples: 9 frames.
    ail("_AIL_set_sample_type@12", {sample, 4, 0});
    ail("_AIL_set_sample_adpcm_block_size@8", {sample, 8});
    miles.bufferLoaded = [&](uint32_t, const std::vector<int16_t> &pcm, int, int channels) {
        CHECK(channels == 1 && pcm.size() == 9 && pcm[0] == 0x100);
    };
    cpu.write16(buffers[0], 0x100);
    cpu.write16(buffers[0] + 2, 0);
    cpu.write32(buffers[0] + 4, 0);
    ail("_AIL_load_sample_buffer@16", {sample, 0, buffers[0], 8});
    CHECK(ail("_AIL_sample_status@4", {sample}) == 4);
    ail("_AIL_end_sample@4", {sample});
    CHECK(ail("_AIL_sample_status@4", {sample}) == 2);
    ail("_AIL_release_sample_handle@4", {sample});
    CHECK(miles.samplesAllocated() == 0);
    ail("_AIL_shutdown@0", {});
}

static void testA3d() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 0;
    process.milliseconds = [&] { return clock; };
    x86::A3d a3d(process);
    audio::Mixer mixer(22050);
    a3d.mixer = &mixer;
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto guid = [&](const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    };
    const uint32_t out = mem(4);
    const uint32_t out2 = mem(4);
    const uint32_t out3 = mem(4);
    const uint32_t out4 = mem(4);
    CHECK(!cpu.call(process.provided("ole32.dll", "CoCreateInstance"),
                    {guid("{92fa2c24-253c-11d2-90fb-006008a1f441}"), 0, 1,
                     guid("{9ae07221-8ac4-11d3-b6aa-00600879f3ee}"), out},
                    r) &&
          r == 0);
    const uint32_t api = cpu.read32(out);
    CHECK(call(api, 15, {0, 0, 0}) == 0);                                                               // Init
    CHECK(call(api, 0, {guid("{4730c6c2-b797-11d3-a33d-00104b67d10e}"), out}) == 0 && cpu.read32(out)); // the geometry
    CHECK(call(api, 0, {guid("{c398e563-d90b-11d1-90fb-006008a1f441}"), out}) == 0);                    // the listener
    CHECK(cpu.read32(out) != 0);
    CHECK(call(api, 17, {0x1, out}) == 0); // NewSource, native (not positioned)
    const uint32_t source = cpu.read32(out);
    CHECK(a3d.sourcesMade() == 1);
    // A second of 1000 Hz mono 8-bit: 1000 bytes.
    const uint32_t format = mem(18);
    std::memset(cpu.memory().data() + format, 0, 18);
    cpu.write16(format, 1);
    cpu.write16(format + 2, 1);
    cpu.write32(format + 4, 1000);
    cpu.write32(format + 8, 1000);
    cpu.write16(format + 12, 1);
    cpu.write16(format + 14, 8);
    CHECK(call(source, 7, {format}) == 0);                              // SetWaveFormat
    CHECK(call(source, 5, {1000}) == 0);                                // AllocateWaveData
    CHECK(call(source, 9, {}) == 1000);                                 // GetWaveSize
    CHECK(call(source, 11, {900, 200, out, out2, out3, out4, 0}) == 0); // Lock, wrapping round
    CHECK(cpu.read32(out2) == 100 && cpu.read32(out4) == 100 && cpu.read32(out3) + 900 == cpu.read32(out));
    for (uint32_t i = 0; i < 100; ++i)
        cpu.write8(cpu.read32(out) + i, 0xc0), cpu.write8(cpu.read32(out3) + i, 0xc0);
    CHECK(call(source, 12, {cpu.read32(out), 100, cpu.read32(out3), 100}) == 0); // Unlock
    // Played once: it moves with the clock, and stops at its end.
    CHECK(call(source, 13, {0}) == 0 && a3d.sourcesPlaying() == 1);
    clock = 250;
    CHECK(call(source, 19, {out}) == 0 && cpu.read32(out) == 250); // GetWavePosition
    CHECK(call(source, 56, {out}) == 0 && cpu.read32(out) == 0x1); // GetStatus: playing
    std::vector<int16_t> mixed(2 * 300);
    mixer.mix(mixed.data(), 300); // 250 bytes at 1000 Hz: about 5500 frames, past its prebuffer
    clock = 1500;
    CHECK(call(source, 56, {out}) == 0 && cpu.read32(out) == 0 && a3d.sourcesPlaying() == 0);
    // The first 100 bytes, heard: 0xc0 is +0x40 << 8.
    mixed.assign(2 * 22050, 0);
    mixer.mix(mixed.data(), 22050);
    CHECK(*std::max_element(mixed.begin(), mixed.end()) == 0x4000);
    // A wave event at the middle: not set until play passes it, then set
    // once (auto-reset).
    uint32_t event = 0;
    const auto wait = process.provided("KERNEL32.dll", "WaitForSingleObject");
    CHECK(!cpu.call(process.provided("KERNEL32.dll", "CreateEventA"), {0, 0, 0, 0}, event) && event);
    CHECK(!cpu.call(wait, {event, 0}, r) && r == 0); // untracked: reads as set
    CHECK(call(source, 59, {500, event}) == 0);      // SetWaveEvent
    CHECK(!cpu.call(wait, {event, 0}, r) && r == 0x102);
    // Looped: it wraps round, and stops where it's stopped.
    CHECK(call(source, 13, {1}) == 0);
    clock = 2000;
    CHECK(call(source, 56, {out}) == 0 && !cpu.call(wait, {event, 0}, r) && r == 0x102);
    clock = 2600;
    CHECK(call(source, 56, {out}) == 0 && !cpu.call(wait, {event, 0}, r) && r == 0);
    CHECK(!cpu.call(wait, {event, 0}, r) && r == 0x102);
    CHECK(call(source, 60, {}) == 0); // ClearWaveEvents
    clock = 3750;
    CHECK(call(source, 19, {out}) == 0 && cpu.read32(out) == 250);
    CHECK(call(source, 56, {out}) == 0 && cpu.read32(out) == (0x1 | 0x4));
    CHECK(call(source, 14, {}) == 0);
    clock = 5000;
    CHECK(call(source, 19, {out}) == 0 && cpu.read32(out) == 250 && a3d.sourcesPlaying() == 0);
    CHECK(call(source, 2, {}) == 0); // Release

    // A 3D source: no format until it has a wave (NiA3dSound asks, to know
    // whether to load its file), then heard from where it is. The listener
    // stays where A3D starts it: at the origin, facing -z, y up, so +x is
    // to its right.
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    CHECK(call(api, 17, {0x0, out}) == 0); // NewSource, 3D
    const uint32_t positioned = cpu.read32(out);
    CHECK(call(positioned, 8, {mem(18)}) != 0); // GetWaveFormat: none yet
    CHECK(call(positioned, 7, {format}) == 0);  // SetWaveFormat
    CHECK(call(positioned, 5, {1000}) == 0);    // AllocateWaveData
    CHECK(call(positioned, 11, {0, 1000, out, out2, out3, out4, 0}) == 0);
    std::memset(cpu.memory().data() + cpu.read32(out), 0xc0, 1000);
    CHECK(call(positioned, 12, {cpu.read32(out), 1000, 0, 0}) == 0);
    CHECK(call(positioned, 38, {bits(1), bits(100), 1}) == 0); // SetMinMaxDistance: muted beyond 100
    auto heard = [&](float x) {                                // the loudest left and right while it plays at (x, 0, 0)
        CHECK(call(positioned, 20, {bits(x), 0, 0}) == 0);     // SetPosition3f
        CHECK(call(positioned, 15, {}) == 0);                  // Rewind
        CHECK(call(positioned, 13, {0}) == 0);                 // Play
        clock += 600;
        CHECK(call(positioned, 56, {out}) == 0);
        std::vector<int16_t> both(2 * 22050);
        mixer.mix(both.data(), 22050);
        int left = 0;
        int right = 0;
        for (size_t i = 0; i < both.size(); i += 2)
            left = std::max(left, std::abs(int(both[i]))), right = std::max(right, std::abs(int(both[i + 1])));
        clock += 600;
        CHECK(call(positioned, 56, {out}) == 0 && cpu.read32(out) == 0); // played to its end
        return std::pair{left, right};
    };
    const auto beyond = heard(200);
    CHECK(beyond.first == 0 && beyond.second == 0);
    const auto onTheRight = heard(10);
    CHECK(onTheRight.second > onTheRight.first && onTheRight.second > 0);
    CHECK(onTheRight.second < 0x4000); // fainter for its distance
    CHECK(call(positioned, 2, {}) == 0);
}

// Pixel conversion: the quick paths (a table per 16-bit format, the
// value itself for 32-bit) give what PixelFormat::toArgb does.
static void testArgbConverter() {
    using reluva::x86::dx::ArgbConverter;
    using reluva::x86::dx::PixelFormat;
    PixelFormat x888{reluva::x86::dx::kPfRgb, 0, 32, 0xff0000, 0xff00, 0xff, 0};
    PixelFormat a888{
        reluva::x86::dx::kPfRgb | reluva::x86::dx::kPfAlphaPixels, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000u};
    PixelFormat r565{reluva::x86::dx::kPfRgb, 0, 16, 0xf800, 0x07e0, 0x001f, 0};
    PixelFormat a1555{reluva::x86::dx::kPfRgb | reluva::x86::dx::kPfAlphaPixels, 0, 16, 0x7c00, 0x03e0, 0x001f, 0x8000};
    uint32_t seed = 12345;
    for (const PixelFormat &f : {x888, a888, r565, a1555}) {
        const ArgbConverter convert(f, nullptr);
        std::vector<uint8_t> bytes(64 * 4);
        for (uint8_t &b : bytes)
            b = uint8_t((seed = seed * 1103515245u + 12345u) >> 16);
        const int n = int(bytes.size() / f.bytes());
        std::vector<uint32_t> out(size_t(n), 0);
        convert.row(bytes.data(), n, out.data());
        bool same = true;
        for (int i = 0; i < n; ++i) {
            uint32_t v = 0;
            std::memcpy(&v, bytes.data() + size_t(i) * f.bytes(), f.bytes());
            same = same && out[size_t(i)] == f.toArgb(v, nullptr) && convert(v) == out[size_t(i)];
        }
        CHECK(same);
    }
}

static const test::Suite
    kSuite("common.unit.windows",
           {
               {"sound streams", [](const std::filesystem::path &) { testMixerStream(); }},
               {"threads", [](const std::filesystem::path &) { testThreads(); }},
               {"exceptions", [](const std::filesystem::path &) { testExceptions(); }},
               {"msvcrt and files", [](const std::filesystem::path &tmp) { testMsvcrtAndFiles(tmp / "msvcrt"); }},
               {"msvcp60", [](const std::filesystem::path &tmp) { testMsvcp60(tmp / "msvcp60"); }},
               {"windows and registry", [](const std::filesystem::path &) { testWindowsAndRegistry(); }},
               {"directx", [](const std::filesystem::path &) { testDirectX(); }},
               {"direct3d 6", [](const std::filesystem::path &) { testDirect3D6(); }},
               {"input", [](const std::filesystem::path &) { testInput(); }},
               {"a3d", [](const std::filesystem::path &) { testA3d(); }},
               {"miles", [](const std::filesystem::path &) { testMiles(); }},
               {"directplay lobby", [](const std::filesystem::path &) { testDirectPlayLobby(); }},
               {"dll unload", [](const std::filesystem::path &tmp) { testDllUnload(tmp); }},
               {"patcher calls", [](const std::filesystem::path &tmp) { testPatcherCalls(tmp); }},
               {"desktop", [](const std::filesystem::path &tmp) { testDesktop(tmp / "desktop"); }},
               {"directplay session", [](const std::filesystem::path &) { testDirectPlaySession(); }},
               {"pixel conversion", [](const std::filesystem::path &) { testArgbConverter(); }},
           });
