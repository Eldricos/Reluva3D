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
#include "emulation/x86/com.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/directx/dinput.h"
#include "emulation/x86/directx/d3drm.h"
#include "emulation/x86/directx/directx.h"
#include "emulation/x86/directx/dsound.h"
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
            return path == "C:\\Game\\Out.dat" ? std::optional(tmp / "Game" / "Out.dat") : std::nullopt;
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
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateFileMappingA"), {handle, 0, 2, 0, 0, 0}, r) && r != 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "MapViewOfFile"), {r, 4, 0, 6, 0}, r) && cpu.string(r, 5) == "world");
    const uint32_t find = process.memory().alloc(320);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindFirstFileA"), {text("C:\\Game\\*.txt"), find}, r) && r != 0xffffffffu &&
          cpu.string(find + 44) == "Data.txt" && cpu.read32(find + 32) == 11);
    CHECK(!cpu.call(fn("KERNEL32.dll", "FindNextFileA"), {r, find}, r) && r == 0);
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
// More of USER32 and KERNEL32: window timers, a window's longs, the work
// area, WIN.INI's defaults; with host threads, semaphores and named
// mutexes.
static void testUserAndKernelMore(const std::filesystem::path &tmp) {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 1000;
    process.milliseconds = [&] { return clock; };
    process.useHostThreads();
    auto fn = [&](const char *dll, const char *name) { return process.provided(dll, name); };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    uint32_t r = 0;
    const uint32_t proc = cpu.addNative("test!WndProc2", [&](x86::Cpu &c) {
        c.reg[x86::Cpu::EAX] = c.arg(1) == 0x81 ? 1 : 0;
        return 16u;
    });
    const uint32_t wc = process.memory().alloc(40);
    cpu.write32(wc + 4, proc);
    cpu.write32(wc + 36, text("More"));
    CHECK(!cpu.call(fn("USER32.dll", "RegisterClassA"), {wc}, r) && r != 0);
    CHECK(!cpu.call(fn("USER32.dll", "CreateWindowExA"),
                    {0, text("More"), text("T"), 0x00cf0000, 0, 0, 100, 100, 0, 0, 0, 0}, r) &&
          r != 0);
    const uint32_t hwnd = r;

    // A timer: WM_TIMER once it's due, one waiting at a time.
    const uint32_t msg = process.memory().alloc(28);
    CHECK(!cpu.call(fn("USER32.dll", "SetTimer"), {hwnd, 7, 100, 0}, r) && r == 7);
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 0);
    clock = 1350;
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 && cpu.read32(msg + 4) == 0x113 &&
          cpu.read32(msg + 8) == 7);
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "KillTimer"), {hwnd, 7}, r) && r == 1);
    clock = 2000;
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 0);

    // Its style and user data; a rectangle; the work area.
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {hwnd, uint32_t(-16)}, r) && r == 0x00cf0000);
    CHECK(!cpu.call(fn("USER32.dll", "SetWindowLongA"), {hwnd, uint32_t(-21), 42}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {hwnd, uint32_t(-21)}, r) && r == 42);
    const uint32_t rc = process.memory().alloc(16);
    CHECK(!cpu.call(fn("USER32.dll", "SetRect"), {rc, 1, 2, 3, 4}, r) && r == 1 && cpu.read32(rc + 12) == 4);
    CHECK(!cpu.call(fn("USER32.dll", "SystemParametersInfoA"), {0x30, 0, rc, 0}, r) && r == 1 &&
          cpu.read32(rc + 8) == uint32_t(process.screenWidth));
    CHECK(!cpu.call(fn("USER32.dll", "MoveWindow"), {hwnd, 5, 6, 70, 80, 1}, r) && r == 1 &&
          process.window(hwnd)->x == 5 && process.window(hwnd)->height == 80);

    // WIN.INI holds nothing: a key reads as its default, trailing spaces off.
    const uint32_t buf = process.memory().alloc(16);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetProfileStringA"), {text("windows"), text("device"), text("none  "), buf, 16},
                    r) &&
          r == 4 && cpu.string(buf) == "none");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetProfileStringA"), {text("a"), text("b"), text("abcdefgh"), buf, 4}, r) &&
          r == 3 && cpu.string(buf) == "abc");

    // A semaphore of 1 of 2: taken once, then not; released by 2, too many.
    const uint32_t wait = fn("KERNEL32.dll", "WaitForSingleObject");
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateSemaphoreA"), {0, 1, 2, 0}, r) && r != 0);
    const uint32_t sem = r;
    CHECK(!cpu.call(wait, {sem, 0}, r) && r == 0);
    CHECK(!cpu.call(wait, {sem, 0}, r) && r == 0x102);
    const uint32_t prev = process.memory().alloc(4);
    CHECK(!cpu.call(fn("KERNEL32.dll", "ReleaseSemaphore"), {sem, 2, prev}, r) && r == 1 && cpu.read32(prev) == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "ReleaseSemaphore"), {sem, 1, prev}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateSemaphoreA"), {0, 3, 2, 0}, r) && r == 0);

    // A named mutex: made once, found again, opened; an unknown one isn't.
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateMutexA"), {0, 0, text("Island")}, r) && r != 0);
    const uint32_t mutex = r;
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateMutexA"), {0, 0, text("ISLAND")}, r) && r == mutex);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLastError"), {}, r) && r == 183);
    CHECK(!cpu.call(fn("KERNEL32.dll", "OpenMutexA"), {0x1f0001, 0, text("island")}, r) && r == mutex);
    CHECK(!cpu.call(fn("KERNEL32.dll", "OpenMutexA"), {0x1f0001, 0, text("other")}, r) && r == 0);

    // A file through OpenFile, read and moved through with the 16-bit
    // functions; written with _hwrite.
    { std::ofstream(tmp / "data.bin", std::ios::binary) << "0123456789"; }
    process.mapPath = [&](const std::string &path, bool) -> std::optional<std::filesystem::path> {
        return tmp / path.substr(path.find_last_of('\\') + 1);
    };
    const uint32_t ofs = process.memory().alloc(136);
    CHECK(!cpu.call(fn("KERNEL32.dll", "OpenFile"), {text("C:\\data.bin"), ofs, 2}, r) && r != 0xffffffffu);
    const uint32_t file = r;
    // On a disk: a C runtime closes a handle of no known type at once.
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetFileType"), {file}, r) && r == 1);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetFileType"), {0x7777}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "_llseek"), {file, 4, 0}, r) && r == 4);
    CHECK(!cpu.call(fn("KERNEL32.dll", "_hread"), {file, buf, 3}, r) && r == 3 && cpu.read8(buf) == '4');
    CHECK(!cpu.call(fn("KERNEL32.dll", "_llseek"), {file, uint32_t(-2), 2}, r) && r == 8);
    CHECK(!cpu.call(fn("KERNEL32.dll", "_hwrite"), {file, text("xy"), 2}, r) && r == 2);
    CHECK(!cpu.call(fn("KERNEL32.dll", "_llseek"), {file, 5, 3}, r) && r == 0xffffffffu); // no such origin
    CHECK(!cpu.call(fn("KERNEL32.dll", "_lclose"), {file}, r) && r == 0);
    std::string written;
    std::getline(std::ifstream(tmp / "data.bin", std::ios::binary), written);
    CHECK(written == "01234567xy");
    // Moved, as a game shifts its saves along: not onto a file that's
    // there, nor from one that isn't.
    CHECK(!cpu.call(fn("KERNEL32.dll", "MoveFileA"), {text("C:\\data.bin"), text("C:\\moved.bin")}, r) && r == 1);
    CHECK(!std::filesystem::exists(tmp / "data.bin") && std::filesystem::exists(tmp / "moved.bin"));
    CHECK(!cpu.call(fn("KERNEL32.dll", "MoveFileA"), {text("C:\\data.bin"), text("C:\\other.bin")}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLastError"), {}, r) && r == 2); // ERROR_FILE_NOT_FOUND
    { std::ofstream(tmp / "data.bin") << "new"; }
    CHECK(!cpu.call(fn("KERNEL32.dll", "MoveFileA"), {text("C:\\data.bin"), text("C:\\moved.bin")}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLastError"), {}, r) && r == 183); // ERROR_ALREADY_EXISTS

    // The locale (US English), as the C runtime asks for it: a length of 0
    // asks for the room.
    const uint32_t info = process.memory().alloc(64);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLocaleInfoA"), {0x409, 0x1004, info, 0}, r) && r == 5);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLocaleInfoA"), {0x409, 0x1004, info, 16}, r) && r == 5 &&
          cpu.string(info) == "1252");
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLocaleInfoW"), {0x409, 0x2a, info, 16}, r) && r == 7 &&
          cpu.read16(info) == 'M' && cpu.read16(info + 12) == 0); // "Monday"
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLocaleInfoA"), {0x409, 0x7777, info, 16}, r) && r == 0);
    // No mapping shared by name; no view where one is asked for; the own
    // process only; no other program run; no MIDI device.
    CHECK(!cpu.call(fn("KERNEL32.dll", "OpenFileMappingA"), {4, 0, text("SmartHeap")}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "MapViewOfFileEx"), {1, 4, 0, 0, 0, 0x10000000}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetLastError"), {}, r) && r == 487);
    CHECK(!cpu.call(fn("KERNEL32.dll", "OpenProcess"), {0x1f0fff, 0, 0x100}, r) && r == 0xffffffffu);
    CHECK(!cpu.call(fn("KERNEL32.dll", "OpenProcess"), {0x1f0fff, 0, 0x200}, r) && r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "CreateProcessA"), {text("CONFIG.EXE"), 0, 0, 0, 0, 0, 0, 0, info, info}, r) &&
          r == 0);
    CHECK(!cpu.call(fn("KERNEL32.dll", "GetExitCodeProcess"), {0xffffffffu, info}, r) && r == 1 &&
          cpu.read32(info) == 259);
    CHECK(!cpu.call(fn("WINMM.dll", "midiOutGetNumDevs"), {}, r) && r == 0);
    CHECK(!cpu.call(fn("WINMM.dll", "midiStreamOpen"), {info, info, 1, 0, 0, 0}, r) && r == 2);

    // VirtualQuery: inside a block VirtualAlloc gave, and beyond memory.
    CHECK(!cpu.call(fn("KERNEL32.dll", "VirtualAlloc"), {0, 0x3000, 0x3000, 4}, r) && r != 0);
    const uint32_t block = r;
    const uint32_t mbi = process.memory().alloc(28);
    CHECK(!cpu.call(fn("KERNEL32.dll", "VirtualQuery"), {block + 0x1234, mbi, 28}, r) && r == 28);
    CHECK(cpu.read32(mbi) == block + 0x1000 && cpu.read32(mbi + 4) == block && cpu.read32(mbi + 12) == 0x2000 &&
          cpu.read32(mbi + 16) == 0x1000); // its page, the block, what's left of it, committed
    CHECK(!cpu.call(fn("KERNEL32.dll", "VirtualQuery"), {0xfff00000u, mbi, 28}, r) && r == 28 &&
          cpu.read32(mbi + 16) == 0x10000); // free
}

// DirectX 5's Direct3D, as LEGO Island reaches it: IDirect3D2 from the
// DirectDraw object, its devices listed and found, and a device made on a
// surface, with its DirectX 5 descriptions.
static void testDirect3D5() {
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
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    };
    const uint32_t out = mem(4);
    CHECK(!cpu.call(process.provided("DDRAW.dll", "DirectDrawCreate"), {0, out, 0}, r) && r == 0);
    const uint32_t dd = cpu.read32(out);
    CHECK(call(dd, 20, {0, 0x11}) == 0);    // SetCooperativeLevel: exclusive, fullscreen
    CHECK(call(dd, 21, {64, 48, 16}) == 0); // SetDisplayMode
    CHECK(call(dd, 0, {guid("{6aae1ec1-662a-11d0-889d-00aa00bbb76a}"), out}) == 0);
    const uint32_t d3d = cpu.read32(out);

    // EnumDevices: emulation, then hardware; each has its flags on its side.
    std::vector<std::pair<std::string, bool>> devices;
    const uint32_t callback = cpu.addNative("test!EnumDevices5", [&](x86::Cpu &c) {
        const bool hardware = c.read32(c.arg(3)) == 204 && c.read32(c.arg(3) + 4) != 0;
        CHECK(c.read32(c.arg(4)) == 204 && (c.read32(c.arg(4) + 4) != 0) == !hardware);
        devices.emplace_back(c.string(c.arg(2)), hardware);
        c.reg[x86::Cpu::EAX] = 1;
        return 24u;
    });
    CHECK(call(d3d, 3, {callback, 0}) == 0);
    CHECK((devices == std::vector<std::pair<std::string, bool>>{{"RGB Emulation", false}, {"Direct3D HAL", true}}));

    // FindDevice: the hardware one.
    const uint32_t search = mem(92);
    std::memset(cpu.memory().data() + search, 0, 92);
    cpu.write32(search, 92);
    cpu.write32(search + 4, 0x4); // D3DFDS_HARDWARE
    cpu.write32(search + 8, 1);
    const uint32_t found = mem(428);
    CHECK(call(d3d, 7, {search, found}) == 0 && cpu.read32(found) == 428);
    CHECK(x86::ComHost::guid(cpu, found + 4) == "{84e63de0-46aa-11cf-816f-0000c020156e}");
    cpu.write32(search + 4, 0x2); // D3DFDS_GUID: one there isn't
    x86::ComHost::writeGuid(cpu, search + 16, "{00000000-0000-0000-0000-000000000001}");
    CHECK(call(d3d, 7, {search, found}) != 0);

    // A device on an offscreen 3D surface.
    const uint32_t desc = mem(108);
    std::memset(cpu.memory().data() + desc, 0, 108);
    cpu.write32(desc, 108);
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4); // CAPS, HEIGHT, WIDTH
    cpu.write32(desc + 8, 48);
    cpu.write32(desc + 12, 64);
    cpu.write32(desc + 104, 0x40 | 0x2000); // OFFSCREENPLAIN, 3DDEVICE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t surface = cpu.read32(out);
    CHECK(call(d3d, 8, {guid("{84e63de0-46aa-11cf-816f-0000c020156e}"), surface, out}) == 0); // CreateDevice
    const uint32_t device = cpu.read32(out);
    const uint32_t hal = mem(204);
    const uint32_t hel = mem(204);
    CHECK(call(device, 3, {hal, hel}) == 0 && cpu.read32(hal) == 204 && cpu.read32(hal + 4) != 0 &&
          cpu.read32(hel + 4) == 0); // GetCaps
    int formats = 0;
    const uint32_t onFormat = cpu.addNative("test!EnumTextureFormats5", [&](x86::Cpu &c) {
        CHECK(c.read32(c.arg(0)) == 108 && c.read32(c.arg(0) + 72) == 32); // a DDSURFACEDESC, its DDPIXELFORMAT
        ++formats;
        c.reg[x86::Cpu::EAX] = 1;
        return 8u;
    });
    CHECK(call(device, 9, {onFormat, 0}) == 0 && formats > 0);
    CHECK(call(device, 12, {out}) == 0 && cpu.read32(out) == d3d);                                   // GetDirect3D
    CHECK(call(device, 23, {7, 1}) == 0 && call(device, 22, {7, out}) == 0 && cpu.read32(out) == 1); // ZENABLE
    CHECK(call(device, 10, {}) == 0 && call(device, 11, {}) == 0); // BeginScene, EndScene
}

// Retained Mode on a DirectX 5 device, as LEGO Island builds its scene:
// frames placed and turned relative to each other, a device and a viewport
// that clears to the scene's background, a light, a material, names; the
// floats its getters give are on the x87 stack.
static void testRetainedMode() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectX dx(process);
    dx.modes.push_back({64, 48, 16});
    x86::Direct3DRM rm(process, dx);
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
    auto value = [&](uint32_t p) {
        float f;
        const uint32_t v = cpu.read32(p);
        std::memcpy(&f, &v, 4);
        return f;
    };
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    auto writeVec = [&](uint32_t p, float x, float y, float z) {
        cpu.write32(p, bits(x));
        cpu.write32(p + 4, bits(y));
        cpu.write32(p + 8, bits(z));
    };
    const uint32_t out = mem(4);
    const uint32_t vec = mem(12);
    const uint32_t vec2 = mem(12);

    // The Direct3D device it's made on.
    CHECK(!cpu.call(process.provided("DDRAW.dll", "DirectDrawCreate"), {0, out, 0}, r) && r == 0);
    const uint32_t dd = cpu.read32(out);
    CHECK(call(dd, 20, {0, 0x11}) == 0 && call(dd, 21, {64, 48, 16}) == 0);
    CHECK(call(dd, 0, {guid("{6aae1ec1-662a-11d0-889d-00aa00bbb76a}"), out}) == 0);
    const uint32_t d3d = cpu.read32(out);
    const uint32_t desc = mem(108);
    std::memset(cpu.memory().data() + desc, 0, 108);
    cpu.write32(desc, 108);
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4);
    cpu.write32(desc + 8, 48);
    cpu.write32(desc + 12, 64);
    cpu.write32(desc + 104, 0x40 | 0x2000);
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t surface = cpu.read32(out);
    CHECK(call(d3d, 8, {guid("{84e63de0-46aa-11cf-816f-0000c020156e}"), surface, out}) == 0);
    const uint32_t d3dDevice = cpu.read32(out);

    // Direct3DRMCreate, as IDirect3DRM2.
    CHECK(!cpu.call(process.provided("d3drm.dll", "Direct3DRMCreate"), {out}, r) && r == 0);
    const uint32_t rm1 = cpu.read32(out);
    CHECK(call(rm1, 0, {guid("{4516ecc8-8f20-11d0-9b6d-0000c0781bc3}"), out}) == 0);
    const uint32_t rm2 = cpu.read32(out);

    // A scene and a child: the child put at (1, 2, 3) in the scene's
    // terms, then the scene moved by (10, 0, 0) and turned a quarter about
    // y. Row vectors: (1, 0, 0) turned a quarter about y is (0, 0, -1).
    CHECK(call(rm2, 4, {0, out}) == 0);
    const uint32_t scene = cpu.read32(out);
    CHECK(call(rm2, 4, {scene, out}) == 0);
    const uint32_t child = cpu.read32(out);
    CHECK(call(child, 61, {scene, bits(1), bits(2), bits(3)}) == 0); // SetPosition
    CHECK(call(child, 24, {scene, vec}) == 0 && near(value(vec), 1) && near(value(vec + 8), 3));
    CHECK(call(scene, 17, {0, 0, bits(1), 0, bits(1.5707964f)}) == 0); // AddRotation, replacing
    CHECK(call(scene, 15, {2, bits(10), 0, 0}) == 0);                  // AddTranslation, after
    // Its origin in the world (Transform), and its position from its parent
    // when no reference is given.
    std::memset(cpu.memory().data() + vec2, 0, 12);
    CHECK(call(child, 68, {vec, vec2}) == 0);
    CHECK(near(value(vec), 13) && near(value(vec + 4), 2) && near(value(vec + 8), -1));
    CHECK(call(child, 24, {0, vec}) == 0 && near(value(vec), 1) && near(value(vec + 8), 3));
    CHECK(call(child, 24, {scene, vec}) == 0 && near(value(vec), 1) && near(value(vec + 4), 2));
    // The child faces +x in the scene's terms: its own z then.
    CHECK(call(child, 60, {scene, bits(1), 0, 0, 0, bits(1), 0}) == 0); // SetOrientation
    CHECK(call(child, 31, {scene, vec, vec2}) == 0 && near(value(vec), 1) && near(value(vec2 + 4), 1));
    // A point to the world and back.
    for (uint32_t i = 0; i < 3; ++i)
        cpu.write32(vec2 + 4 * i, bits(float(i)));
    CHECK(call(child, 68, {vec, vec2}) == 0 && call(child, 34, {vec2, vec}) == 0); // Transform, InverseTransform
    CHECK(near(value(vec2), 0) && near(value(vec2 + 4), 1) && near(value(vec2 + 8), 2));
    CHECK(call(child, 23, {out}) == 0 && cpu.read32(out) != 0); // GetParent
    // Names and the class's.
    const uint32_t name = mem(16);
    cpu.writeString(name, "camera", 16);
    CHECK(call(child, 8, {name}) == 0);
    cpu.write32(out, 16);
    CHECK(call(child, 9, {out, name + 8}) == 0 && cpu.read32(out) == 7 && cpu.string(name + 8) == "camera");
    cpu.write32(out, 16);
    CHECK(call(child, 10, {out, name}) == 0 && cpu.string(name) == "Frame");

    // A device on the Direct3D one, a viewport of it seen from the child,
    // cleared to the scene's background: red.
    CHECK(call(rm2, 16, {d3d, d3dDevice, out}) == 0); // CreateDeviceFromD3D
    const uint32_t device = cpu.read32(out);
    CHECK(call(device, 27, {}) == 64 && call(device, 26, {}) == 48); // GetWidth, GetHeight
    CHECK(call(rm2, 20, {device, child, 0, 0, 64, 48, out}) == 0);   // CreateViewport
    const uint32_t viewport = cpu.read32(out);
    CHECK(call(viewport, 16, {bits(0.25f)}) == 0); // SetField
    cpu.fpu.top = 0;
    call(viewport, 34, {}); // GetField, on the x87 stack
    CHECK(near(float(cpu.fpuPop()), 0.25f));
    const uint32_t planes = mem(16);
    CHECK(call(viewport, 27, {planes, planes + 4, planes + 8, planes + 12}) == 0); // GetPlane: wider than high
    CHECK(near(value(planes + 4), 0.25f * 64 / 48) && near(value(planes + 12), 0.25f));
    CHECK(call(scene, 49, {bits(1), 0, 0}) == 0); // SetSceneBackgroundRGB
    CHECK(call(viewport, 12, {}) == 0);           // Clear
    const uint32_t lock = mem(108);
    std::memset(cpu.memory().data() + lock, 0, 108);
    cpu.write32(lock, 108);
    CHECK(call(surface, 25, {0, lock, 1, 0}) == 0);     // Lock
    CHECK(cpu.read16(cpu.read32(lock + 36)) == 0xf800); // red in 565
    CHECK(call(surface, 32, {0}) == 0);                 // Unlock

    // A light and a material.
    CHECK(call(rm2, 12, {3, bits(1), bits(0.5f), 0, out}) == 0); // CreateLightRGB, directional
    const uint32_t light = cpu.read32(out);
    CHECK(call(light, 27, {}) == 3 && call(light, 26, {}) == 0xffff8000u); // GetType, GetColor
    cpu.fpu.top = 0;
    call(light, 20, {}); // GetRange: 256 unless set
    CHECK(near(float(cpu.fpuPop()), 256));
    CHECK(call(child, 12, {light}) == 0);      // AddLight
    CHECK(call(rm2, 13, {bits(8), out}) == 0); // CreateMaterial
    cpu.fpu.top = 0;
    call(cpu.read32(out), 14, {}); // GetPower
    CHECK(near(float(cpu.fpuPop()), 8));

    // A texture on the surface, and a mesh: a group of two triangles, its
    // vertices, its box, the group read back; a clone, apart from it.
    CHECK(call(rm2, 18, {surface, out}) == 0); // CreateTextureFromSurface
    const uint32_t texture = cpu.read32(out);
    CHECK(call(texture, 6, {77}) == 0 && call(texture, 7, {}) == 77); // SetAppData, GetAppData
    CHECK(call(rm2, 5, {out}) == 0);                                  // CreateMesh
    const uint32_t mesh = cpu.read32(out);
    const uint32_t faces = mem(4 * 8);
    const uint32_t faceData[8] = {3, 0, 1, 2, 3, 1, 3, 2}; // each face's count, then its vertices
    for (uint32_t i = 0; i < 8; ++i)
        cpu.write32(faces + 4 * i, faceData[i]);
    CHECK(call(mesh, 14, {4, 2, 0, faces, out}) == 0 && cpu.read32(out) == 0); // AddGroup
    const uint32_t vertices = mem(36 * 4);
    std::memset(cpu.memory().data() + vertices, 0, 36 * 4);
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.write32(vertices + 36 * i, bits(float(i)));       // x
        cpu.write32(vertices + 36 * i + 8, bits(-float(i)));  // z
        cpu.write32(vertices + 36 * i + 32, 0xff000000u | i); // colour
    }
    CHECK(call(mesh, 15, {0, 0, 4, vertices}) == 0);                                    // SetVertices
    CHECK(call(mesh, 15, {0, 3, 2, vertices}) != 0);                                    // past its end
    CHECK(call(mesh, 21, {0, texture}) == 0);                                           // SetGroupTexture
    CHECK(call(mesh, 16, {0, 0xff00ff00u}) == 0 && call(mesh, 25, {0}) == 0xff00ff00u); // its colour
    const uint32_t box = mem(24);
    CHECK(call(mesh, 13, {box}) == 0 && near(value(box + 8), -3) && near(value(box + 12), 3)); // GetBox
    const uint32_t counts = mem(16);
    const uint32_t data = mem(32);
    CHECK(call(mesh, 23, {0, counts, counts + 4, counts + 8, counts + 12, data}) == 0); // GetGroup
    CHECK(cpu.read32(counts) == 4 && cpu.read32(counts + 4) == 2 && cpu.read32(counts + 12) == 8 &&
          cpu.read32(data + 16) == 3 && cpu.read32(data + 28) == 2);
    CHECK(call(mesh, 3, {0, guid("{a3a80d01-6e12-11cf-ac4a-0000c03825a1}"), out}) == 0); // Clone
    const uint32_t copy = cpu.read32(out);
    CHECK(copy != mesh && call(copy, 22, {}) == 1); // GetGroupCount
    CHECK(call(copy, 13, {box}) == 0 && near(value(box + 12), 3));
    CHECK(call(mesh, 11, {bits(2), bits(1), bits(1)}) == 0); // Scale: the mesh, not its copy
    CHECK(call(copy, 13, {box}) == 0 && near(value(box + 12), 3));
    CHECK(call(child, 18, {mesh}) == 0); // AddVisual
    CHECK(rm.objectsMade() == 9);

    // A scene drawn and picked: a camera at its origin, looking down its z,
    // and an unlit green square 5 in front of it, its faces clockwise as
    // the camera sees them. The square fills the viewport's middle, not its
    // corners.
    CHECK(call(rm2, 4, {0, out}) == 0);
    const uint32_t scene2 = cpu.read32(out);
    CHECK(call(rm2, 4, {scene2, out}) == 0);
    const uint32_t camera = cpu.read32(out);
    CHECK(call(rm2, 4, {scene2, out}) == 0);
    const uint32_t holder = cpu.read32(out);
    CHECK(call(holder, 61, {scene2, 0, 0, bits(5)}) == 0); // SetPosition
    CHECK(call(rm2, 5, {out}) == 0);                       // CreateMesh
    const uint32_t square = cpu.read32(out);
    const uint32_t quadFaces = mem(4 * 8);
    const uint32_t quadData[8] = {3, 3, 2, 1, 3, 3, 1, 0};
    for (uint32_t i = 0; i < 8; ++i)
        cpu.write32(quadFaces + 4 * i, quadData[i]);
    CHECK(call(square, 14, {4, 2, 0, quadFaces, out}) == 0); // AddGroup
    const uint32_t corners = mem(36 * 4);
    std::memset(cpu.memory().data() + corners, 0, 36 * 4);
    const float xy[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.write32(corners + 36 * i, bits(xy[i][0]));
        cpu.write32(corners + 36 * i + 4, bits(xy[i][1]));
    }
    CHECK(call(square, 15, {0, 0, 4, corners}) == 0);               // SetVertices
    CHECK(call(square, 16, {0, 0xff00ff00u}) == 0);                 // SetGroupColor
    CHECK(call(square, 19, {0, 0x80}) == 0);                        // SetGroupQuality: flat, unlit, solid
    CHECK(call(holder, 18, {square}) == 0);                         // AddVisual
    CHECK(call(rm2, 20, {device, camera, 0, 0, 64, 48, out}) == 0); // CreateViewport
    const uint32_t view = cpu.read32(out);
    CHECK(call(scene2, 49, {0, 0, 0}) == 0); // SetSceneBackgroundRGB: black
    CHECK(call(view, 12, {}) == 0);          // Clear
    CHECK(call(view, 13, {scene2}) == 0);    // Render
    std::memset(cpu.memory().data() + lock, 0, 108);
    cpu.write32(lock, 108);
    CHECK(call(surface, 25, {0, lock, 1, 0}) == 0); // Lock
    const uint32_t pixels = cpu.read32(lock + 36);
    const uint32_t pitch = cpu.read32(lock + 16);
    CHECK(cpu.read16(pixels + 24 * pitch + 32 * 2) == 0x07e0); // the middle: green in 565
    CHECK(cpu.read16(pixels) == 0);                            // a corner: the background
    CHECK(call(surface, 32, {0}) == 0);                        // Unlock
    // Picked in the middle: the square, its frames from the scene down, its
    // group; nothing at a corner.
    CHECK(call(view, 28, {32, 24, out}) == 0); // Pick
    const uint32_t picked = cpu.read32(out);
    CHECK(call(picked, 3, {}) == 1); // GetSize
    const uint32_t pickDesc = mem(20);
    const uint32_t frameArray = mem(4);
    CHECK(call(picked, 4, {0, out, frameArray, pickDesc}) == 0); // GetPick
    CHECK(cpu.read32(out) == square && cpu.read32(pickDesc + 4) == 0);
    const uint32_t path = cpu.read32(frameArray);
    CHECK(call(path, 3, {}) == 2);                                             // GetSize
    CHECK(call(holder, 6, {55}) == 0);                                         // SetAppData
    CHECK(call(path, 4, {1, out}) == 0 && call(cpu.read32(out), 7, {}) == 55); // GetElement, its app data
    CHECK(call(view, 28, {0, 0, out}) == 0 && call(cpu.read32(out), 3, {}) == 0);
    // Points to the screen and back: the square's middle is the viewport's,
    // its right edge to the right (homogeneous: x and y over w).
    const uint32_t h4 = mem(16);
    const uint32_t world = mem(12);
    writeVec(world, 0, 0, 5);
    CHECK(call(view, 20, {h4, world}) == 0); // Transform
    CHECK(near(value(h4) / value(h4 + 12), 32) && near(value(h4 + 4) / value(h4 + 12), 24));
    const float depth = value(h4 + 8) / value(h4 + 12);
    CHECK(depth > 0 && depth < 1);
    writeVec(world, 0, 0, 0);
    CHECK(call(view, 21, {world, h4}) == 0); // InverseTransform
    CHECK(std::fabs(value(world)) < 1e-3f && std::fabs(value(world + 8) - 5) < 1e-3f);
    writeVec(world, 1, 0, 5);
    CHECK(call(view, 20, {h4, world}) == 0 && value(h4) / value(h4 + 12) > 40);

    // Lit by two white lights shining straight at it, and textured mid-grey:
    // the lit colour saturates at white, and the texture stays grey.
    for (uint32_t i = 0; i < 4; ++i) {
        cpu.write32(corners + 36 * i + 20, bits(-1));           // normal: towards the camera
        cpu.write32(corners + 36 * i + 24, bits(xy[i][0] > 0)); // u
        cpu.write32(corners + 36 * i + 28, bits(xy[i][1] > 0)); // v
    }
    CHECK(call(square, 15, {0, 0, 4, corners}) == 0); // SetVertices
    std::memset(cpu.memory().data() + desc, 0, 108);
    cpu.write32(desc, 108);
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
    const uint32_t greySurface = cpu.read32(out);
    std::memset(cpu.memory().data() + lock, 0, 108);
    cpu.write32(lock, 108);
    CHECK(call(greySurface, 25, {0, lock, 1, 0}) == 0); // Lock
    for (uint32_t y = 0; y < 2; ++y)
        for (uint32_t x = 0; x < 2; ++x)
            cpu.write16(cpu.read32(lock + 36) + y * cpu.read32(lock + 16) + 2 * x, 0x8410);
    CHECK(call(greySurface, 32, {0}) == 0);             // Unlock
    CHECK(call(rm2, 18, {greySurface, out}) == 0);      // CreateTextureFromSurface
    CHECK(call(square, 21, {0, cpu.read32(out)}) == 0); // SetGroupTexture
    CHECK(call(square, 16, {0, 0xffffffffu}) == 0);     // SetGroupColor: white
    CHECK(call(square, 19, {0, 0x80 | 0x8}) == 0);      // flat, lit, solid
    for (int i = 0; i < 2; ++i) {
        CHECK(call(rm2, 12, {3, bits(1), bits(1), bits(1), out}) == 0); // CreateLightRGB, directional
        CHECK(call(camera, 12, {cpu.read32(out)}) == 0);                // AddLight
    }
    CHECK(call(view, 12, {}) == 0 && call(view, 13, {scene2}) == 0); // Clear, Render
    std::memset(cpu.memory().data() + lock, 0, 108);
    cpu.write32(lock, 108);
    CHECK(call(surface, 25, {0, lock, 1, 0}) == 0); // Lock
    const uint16_t lit = cpu.read16(cpu.read32(lock + 36) + 24 * cpu.read32(lock + 16) + 32 * 2);
    CHECK(call(surface, 32, {0}) == 0);            // Unlock
    CHECK((lit >> 11) >= 14 && (lit >> 11) <= 18); // grey, not white

    // Partly out of view, across its right edge (at 3.33 at that depth),
    // it's still drawn; wholly out of it, not.
    auto pixelAt = [&](int x, int y) {
        std::memset(cpu.memory().data() + lock, 0, 108);
        cpu.write32(lock, 108);
        CHECK(call(surface, 25, {0, lock, 1, 0}) == 0); // Lock
        const uint16_t v = cpu.read16(cpu.read32(lock + 36) + uint32_t(y) * cpu.read32(lock + 16) + uint32_t(x) * 2);
        CHECK(call(surface, 32, {0}) == 0); // Unlock
        return v;
    };
    CHECK(call(holder, 61, {scene2, bits(3), 0, bits(5)}) == 0);     // SetPosition: 2 to 4 across
    CHECK(call(view, 12, {}) == 0 && call(view, 13, {scene2}) == 0); // Clear, Render
    CHECK(pixelAt(56, 24) != 0 && pixelAt(32, 24) == 0);
    CHECK(call(holder, 61, {scene2, bits(30), 0, bits(5)}) == 0);
    CHECK(call(view, 12, {}) == 0 && call(view, 13, {scene2}) == 0);
    CHECK(pixelAt(63, 24) == 0);

    // The scene's fog: linear from 1 to 6, the square at 5 a fifth its own
    // green and four fifths the fog's red.
    CHECK(call(holder, 61, {scene2, 0, 0, bits(5)}) == 0);     // SetPosition
    CHECK(call(square, 21, {0, 0}) == 0);                      // SetGroupTexture: none
    CHECK(call(square, 16, {0, 0xff00ff00u}) == 0);            // SetGroupColor: green
    CHECK(call(square, 19, {0, 0x80}) == 0);                   // unlit
    CHECK(call(scene2, 52, {1}) == 0);                         // SetSceneFogEnable
    CHECK(call(scene2, 53, {0xffff0000u}) == 0);               // SetSceneFogColor: red
    CHECK(call(scene2, 54, {0}) == 0);                         // SetSceneFogMode: D3DRMFOG_LINEAR
    CHECK(call(scene2, 55, {bits(1), bits(6), bits(1)}) == 0); // SetSceneFogParams
    CHECK(call(view, 12, {}) == 0 && call(view, 13, {scene2}) == 0);
    const uint16_t fogged = pixelAt(32, 24);
    CHECK((fogged >> 11) >= 23 && (fogged >> 11) <= 27);             // red: 0.8 of 31
    CHECK(((fogged >> 5) & 63) >= 10 && ((fogged >> 5) & 63) <= 15); // green: 0.2 of 63
    CHECK(call(scene2, 52, {0}) == 0);

    // Two half-transparent squares, the near red one added first, the far
    // blue one after (no Z buffer): drawn as they come, the far blends over
    // the near, more blue than red; sorted
    // (D3DRMRENDERMODE_SORTEDTRANSPARENCY), the far is drawn first, the
    // near over it, more red than blue.
    CHECK(call(holder, 61, {scene2, 0, 0, bits(4)}) == 0);
    CHECK(call(square, 16, {0, 0x80ff0000u}) == 0);                                        // red, half
    CHECK(call(square, 3, {0, guid("{a3a80d01-6e12-11cf-ac4a-0000c03825a1}"), out}) == 0); // Clone
    const uint32_t farSquare = cpu.read32(out);
    CHECK(call(farSquare, 16, {0, 0x800000ffu}) == 0); // blue, half
    CHECK(call(rm2, 4, {scene2, out}) == 0);           // CreateFrame
    const uint32_t farHolder = cpu.read32(out);
    CHECK(call(farHolder, 61, {scene2, 0, 0, bits(6)}) == 0);
    CHECK(call(farHolder, 18, {farSquare}) == 0); // AddVisual
    CHECK(call(device, 36, {1}) == 0);            // SetRenderMode: blended
    CHECK(call(view, 12, {}) == 0 && call(view, 13, {scene2}) == 0);
    const uint16_t asAdded = pixelAt(32, 24);
    CHECK((asAdded & 0x1f) >= 14 && (asAdded >> 11) >= 6 && (asAdded >> 11) <= 9);
    CHECK(call(device, 36, {1 | 2}) == 0); // blended and sorted
    CHECK(call(view, 12, {}) == 0 && call(view, 13, {scene2}) == 0);
    const uint16_t both = pixelAt(32, 24);
    CHECK((both & 0x1f) >= 6 && (both & 0x1f) <= 9 && (both >> 11) >= 14 && (both >> 11) <= 17);

    // Lighting as Retained Mode's: a grey square lit by one light at a time
    // in a scene of its own, its front facing the camera.
    CHECK(call(device, 36, {1}) == 0); // blended, not sorted
    CHECK(call(rm2, 4, {0, out}) == 0);
    const uint32_t scene3 = cpu.read32(out);
    CHECK(call(rm2, 4, {scene3, out}) == 0);
    const uint32_t camera3 = cpu.read32(out);
    CHECK(call(rm2, 4, {scene3, out}) == 0);
    const uint32_t litHolder = cpu.read32(out);
    CHECK(call(litHolder, 61, {scene3, 0, 0, bits(5)}) == 0);
    CHECK(call(square, 3, {0, guid("{a3a80d01-6e12-11cf-ac4a-0000c03825a1}"), out}) == 0); // Clone
    const uint32_t litSquare = cpu.read32(out);
    CHECK(call(litSquare, 16, {0, 0xff404040u}) == 0); // a quarter grey
    CHECK(call(litSquare, 19, {0, 0x88}) == 0);        // flat, lit, solid
    CHECK(call(litHolder, 18, {litSquare}) == 0);
    CHECK(call(rm2, 20, {device, camera3, 0, 0, 64, 48, out}) == 0); // CreateViewport
    const uint32_t view3 = cpu.read32(out);
    CHECK(call(scene3, 49, {0, 0, 0}) == 0); // black behind
    auto litMiddle = [&](uint32_t lightFrame, uint32_t type, auto setUp) {
        CHECK(call(rm2, 12, {type, bits(1), bits(1), bits(1), out}) == 0); // CreateLightRGB
        const uint32_t light = cpu.read32(out);
        setUp(light);
        CHECK(call(lightFrame, 12, {light}) == 0); // AddLight
        CHECK(call(view3, 12, {}) == 0 && call(view3, 13, {scene3}) == 0);
        CHECK(call(lightFrame, 39, {light}) == 0); // DeleteLight
        return pixelAt(32, 24) >> 11;              // red, of 31
    };
    // A point light at the camera, 5 away: its range (1) doesn't cut it
    // off, a spotlight's alone; a constant attenuation of a quarter doesn't
    // make it four times as bright.
    CHECK(litMiddle(camera3, 1, [&](uint32_t l) { CHECK(call(l, 14, {bits(1)}) == 0); }) >= 7); // SetRange
    const uint32_t attenuated = litMiddle(camera3, 1, [&](uint32_t l) {
        CHECK(call(l, 17, {bits(0.25f)}) == 0); // SetConstantAttenuation
    });
    CHECK(attenuated >= 7 && attenuated <= 9);
    // Behind the square (10 away), facing the way it does: a directional
    // light shines on its front; a parallel point light, from where it is,
    // on its back.
    CHECK(call(rm2, 4, {scene3, out}) == 0);
    const uint32_t behind = cpu.read32(out);
    CHECK(call(behind, 61, {scene3, 0, 0, bits(10)}) == 0);
    CHECK(litMiddle(behind, 3, [](uint32_t) {}) >= 7);
    CHECK(litMiddle(behind, 4, [](uint32_t) {}) == 0);
    // Just in front of its middle, Gouraud shaded: a point light reaches its
    // corners at a slant, dark; a parallel point light one way across it,
    // straight on, bright.
    CHECK(call(litSquare, 19, {0, 0x89}) == 0);
    CHECK(call(behind, 61, {scene3, 0, 0, bits(4.9f)}) == 0);
    CHECK(litMiddle(behind, 1, [](uint32_t) {}) <= 2);
    CHECK(litMiddle(behind, 4, [](uint32_t) {}) >= 7);
}

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
    cpu.write16(texels, 0xffff);                // white
    cpu.write16(texels + 2, 0xf800);            // red
    cpu.write16(texels + pitch, 0x07e0);        // green
    cpu.write16(texels + pitch + 2, 0x0000);    // black
    CHECK(call(texture, 32, {0}) == 0);         // Unlock
    CHECK(call(device, 35, {0, texture}) == 0); // SetTexture
    CHECK(call(device, 37, {0, 1, 4}) == 0);    // COLOROP: MODULATE
    CHECK(call(device, 37, {0, 2, 2}) == 0);    // COLORARG1: texture
    CHECK(call(device, 37, {0, 3, 0}) == 0);    // COLORARG2: diffuse
    vertex(0, 40, 24, 0.5f, 0xffffffffu, 0, 0);
    vertex(1, 64, 24, 0.5f, 0xffffffffu, 1, 0);
    vertex(2, 40, 48, 0.5f, 0xffffffffu, 0, 1);
    vertex(3, 64, 48, 0.5f, 0xffffffffu, 1, 1);
    CHECK(call(device, 25, {5, fvf, verts, 4, 0}) == 0); // a strip
    // Flat shaded, a triangle whose first (red) corner is off the screen's
    // left is still red where it shows, however it's clipped. Not
    // transformed (clip space, the transforms being the identity), unlit.
    const uint32_t flatVerts = mem(3 * 16);
    const float flatXy[3][2] = {{-3.0f, -0.5f}, {-0.5f, -0.3f}, {-0.5f, -0.9f}};
    const uint32_t flatColors[3] = {0xffff0000u, 0xff00ff00u, 0xff0000ffu};
    for (uint32_t i = 0; i < 3; ++i) {
        cpu.write32(flatVerts + 16 * i, bits(flatXy[i][0]));
        cpu.write32(flatVerts + 16 * i + 4, bits(flatXy[i][1]));
        cpu.write32(flatVerts + 16 * i + 8, bits(0.5f));
        cpu.write32(flatVerts + 16 * i + 12, flatColors[i]);
    }
    CHECK(call(device, 35, {0, 0}) == 0);                           // SetTexture: none
    CHECK(call(device, 37, {0, 1, 2}) == 0);                        // COLOROP: SELECTARG1
    CHECK(call(device, 37, {0, 2, 0}) == 0);                        // COLORARG1: diffuse
    CHECK(call(device, 20, {137, 0}) == 0);                         // LIGHTING: off
    CHECK(call(device, 20, {9, 1}) == 0);                           // SHADEMODE: flat
    CHECK(call(device, 25, {4, 0x2 | 0x40, flatVerts, 3, 0}) == 0); // XYZ | DIFFUSE
    CHECK(call(device, 20, {9, 2}) == 0);                           // SHADEMODE: Gouraud again
    // GDI on a surface's device context (GetDC): a red "I" in a font 9 high,
    // transparent behind, its size measured; a 2x2 bitmap of 24 bits,
    // bottom-up, stretched twice its size. In 565 once let go (ReleaseDC).
    clearDesc();
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4 | 0x1000);
    cpu.write32(desc + 8, 16);
    cpu.write32(desc + 12, 32);
    cpu.write32(desc + 72, 32);
    cpu.write32(desc + 76, 0x40); // RGB
    cpu.write32(desc + 84, 16);
    cpu.write32(desc + 88, 0xf800);
    cpu.write32(desc + 92, 0x07e0);
    cpu.write32(desc + 96, 0x001f);
    cpu.write32(desc + 104, 0x40); // OFFSCREENPLAIN
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t canvas = cpu.read32(out);
    CHECK(call(canvas, 17, {out}) == 0); // GetDC
    const uint32_t hdc = cpu.read32(out);
    auto gdi = [&](const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided("GDI32.dll", name), args, r));
        return r;
    };
    const uint32_t font = gdi("CreateFontA", {uint32_t(-9), 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(font != 0 && gdi("SelectObject", {hdc, font}) == 0);
    CHECK(gdi("SetTextColor", {hdc, 0x0000ff}) == 0); // was black; red as COLORREF
    CHECK(gdi("SetBkMode", {hdc, 1}) == 2);           // TRANSPARENT; was OPAQUE
    const uint32_t textAt = mem(4);
    const uint32_t size = mem(8);
    cpu.writeString(textAt, "I", 4);
    CHECK(gdi("GetTextExtentPoint32A", {hdc, textAt, 1, size}) == 1 && cpu.read32(size) == 6 &&
          cpu.read32(size + 4) == 11);
    CHECK(gdi("ExtTextOutA", {hdc, 0, 0, 0, 0, textAt, 1, 0}) == 1);
    const uint32_t dib = mem(16);
    const uint32_t info = mem(40);
    std::memset(cpu.memory().data() + info, 0, 40);
    cpu.write32(info, 40);
    cpu.write32(info + 4, 2);
    cpu.write32(info + 8, 2); // bottom-up
    cpu.write16(info + 12, 1);
    cpu.write16(info + 14, 24);
    const uint8_t rows[16] = {0, 255, 0,   255, 0,   0,   0, 0,  // bottom: green, blue (B, G, R), padding
                              0, 0,   255, 255, 255, 255, 0, 0}; // top: red, white
    for (uint32_t i = 0; i < 16; ++i)
        cpu.write8(dib + i, rows[i]);
    CHECK(gdi("StretchDIBits", {hdc, 20, 0, 4, 4, 0, 0, 2, 2, dib, info, 0, 0xcc0020}) == 2);
    CHECK(call(canvas, 26, {hdc}) == 0); // ReleaseDC
    std::memset(cpu.memory().data() + desc, 0, 124);
    cpu.write32(desc, 124);
    CHECK(call(canvas, 25, {0, desc, 1, 0}) == 0); // Lock
    const uint32_t canvasPixels = cpu.read32(desc + 36);
    const uint32_t canvasPitch = cpu.read32(desc + 16);
    auto canvasAt = [&](int x, int y) {
        return cpu.read16(canvasPixels + uint32_t(y) * canvasPitch + uint32_t(x) * 2);
    };
    bool red = false;
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 6; ++x)
            red = red || canvasAt(x, y) == 0xf800;
    CHECK(red && canvasAt(5, 10) == 0); // the "I" drawn; nothing behind it
    CHECK(canvasAt(20, 0) == 0xf800 && canvasAt(23, 1) == 0xffff && canvasAt(21, 3) == 0x07e0 &&
          canvasAt(23, 3) == 0x001f);
    CHECK(call(canvas, 32, {0}) == 0);     // Unlock
    CHECK(call(primary, 11, {0, 1}) == 0); // Flip

    const x86::DirectX::Frame &f = dx.frame();
    CHECK(f.width == 64 && f.height == 48 && f.number == 1);
    auto at = [&](int x, int y) { return f.pixels[size_t(y * f.width + x)] & 0xffffff; };
    CHECK(at(5, 5) == 0xff0000);   // the near triangle
    CHECK(at(40, 5) == 0x0000ff);  // the clear colour
    CHECK(at(31, 31) == 0x0000ff); // past the hypotenuse
    CHECK(at(45, 28) == 0xffffff && at(58, 28) == 0xff0000 && at(45, 44) == 0x00ff00 && at(58, 44) == 0x000000);
    CHECK(at(10, 38) == 0xff0000 && at(2, 38) == 0xff0000); // the flat triangle, clipped
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
    // WINMM's joystick: none until there is one; then its capabilities,
    // and an axis, a button and a diagonal on its hat read back.
    const uint32_t caps = process.memory().alloc(404);
    const uint32_t pos = process.memory().alloc(52);
    CHECK(!cpu.call(fn("WINMM.dll", "joyGetDevCapsA"), {0, caps, 404}, r) && r == 167); // JOYERR_UNPLUGGED
    process.joystick.present = true;
    CHECK(!cpu.call(fn("WINMM.dll", "joyGetDevCapsA"), {0, caps, 404}, r) && r == 0);
    CHECK(cpu.read32(caps + 40) == 65535 && cpu.read32(caps + 60) == 32 && (cpu.read32(caps + 96) & 0x10));
    CHECK(!cpu.call(fn("WINMM.dll", "joyGetDevCapsA"), {1, caps, 404}, r) && r == 165); // JOYERR_PARMS
    process.joystickAxis(0, 32767);
    process.joystickButton(2, true);
    process.joystickHat(1 | 2); // up and right
    cpu.write32(pos, 52);
    CHECK(!cpu.call(fn("WINMM.dll", "joyGetPosEx"), {0, pos}, r) && r == 0);
    CHECK(cpu.read32(pos + 8) == 65535 && cpu.read32(pos + 12) == 32768); // x right, y centred
    CHECK(cpu.read32(pos + 32) == 4 && cpu.read32(pos + 36) == 1 && cpu.read32(pos + 40) == 4500);
    // An extended key: the up arrow, scan code 0x48 extended, is DIK_UP (0xc8).
    process.key(0x26, 0x148, true);
    CHECK(call(keyboard, 9, {256, state}) == 0 && cpu.read8(state + 0xc8) == 0x80 && cpu.read8(state + 0x48) == 0);
    process.key(0x26, 0x148, false);
}

// A3D created as a program creates it, a source made, filled through Lock,
// played once and looped, as the clock moves.
// DirectSound: a buffer written through Lock, played by the process's
// clock to its end, looped, duplicated; the primary buffer's listener and a
// buffer's 3D position kept.
static void testDirectSound() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 0;
    process.milliseconds = [&] { return clock; };
    x86::DirectSound dsound(process);
    audio::Mixer mixer(22050);
    dsound.mixer = &mixer;
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    };
    auto guid = [&](const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    const uint32_t out = mem(4);
    const uint32_t out2 = mem(4);
    const uint32_t out3 = mem(4);
    const uint32_t out4 = mem(4);
    CHECK(!cpu.call(process.provided("DSOUND.dll", "DirectSoundCreate"), {0, out, 0}, r) && r == 0);
    const uint32_t ds = cpu.read32(out);
    CHECK(ds != 0);
    const uint32_t caps = mem(96);
    CHECK(call(ds, 4, {caps}) == 0 && cpu.read32(caps) == 96); // GetCaps
    CHECK(call(ds, 6, {0, 2}) == 0);                           // SetCooperativeLevel
    CHECK(call(ds, 8, {out}) == 0 && cpu.read32(out) == 4);    // GetSpeakerConfig: stereo

    // The primary buffer, 3D: its listener.
    const uint32_t desc = mem(20);
    std::memset(cpu.memory().data() + desc, 0, 20);
    cpu.write32(desc, 20);
    cpu.write32(desc + 4, 0x1 | 0x10);
    CHECK(call(ds, 3, {desc, out, 0}) == 0); // CreateSoundBuffer
    const uint32_t primary = cpu.read32(out);
    CHECK(call(primary, 0, {guid("{279afa84-4981-11ce-a521-0020af0be560}"), out}) == 0);
    const uint32_t listener = cpu.read32(out);
    CHECK(call(primary, 0, {guid("{279afa86-4981-11ce-a521-0020af0be560}"), out}) != 0); // no 3D buffer
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    CHECK(call(listener, 14, {bits(1), bits(2), bits(3), 0}) == 0); // SetPosition
    CHECK(call(listener, 7, {out}) == 0 && cpu.read32(out) == bits(1));

    // A second of 1000 Hz mono 8-bit, 3D, with volume: 1000 bytes.
    const uint32_t format = mem(18);
    std::memset(cpu.memory().data() + format, 0, 18);
    cpu.write16(format, 1);
    cpu.write16(format + 2, 1);
    cpu.write32(format + 4, 1000);
    cpu.write32(format + 8, 1000);
    cpu.write16(format + 12, 1);
    cpu.write16(format + 14, 8);
    cpu.write32(desc + 4, 0x10 | 0x80);
    cpu.write32(desc + 16, format);
    CHECK(call(ds, 3, {desc, out, 0}) != 0); // no size
    cpu.write32(desc + 8, 1000);
    CHECK(call(ds, 3, {desc, out, 0}) == 0);
    const uint32_t buffer = cpu.read32(out);
    CHECK(dsound.buffersMade() == 2);
    CHECK(call(buffer, 11, {900, 200, out, out2, out3, out4, 0}) == 0); // Lock, wrapping round
    CHECK(cpu.read32(out2) == 100 && cpu.read32(out4) == 100 && cpu.read32(out3) + 900 == cpu.read32(out));
    for (uint32_t i = 0; i < 100; ++i)
        cpu.write8(cpu.read32(out) + i, 0xc0), cpu.write8(cpu.read32(out3) + i, 0xc0);
    CHECK(call(buffer, 19, {cpu.read32(out), 100, cpu.read32(out3), 100}) == 0); // Unlock
    CHECK(call(buffer, 11, {0, 0, out, out2, out3, out4, 0x2}) == 0 && cpu.read32(out2) == 1000 &&
          cpu.read32(out4) == 0); // Lock, all of it
    const uint32_t sound = cpu.read32(out);

    // Played once: it moves with the clock, the write position a little
    // ahead, and it stops at its end.
    CHECK(call(buffer, 12, {0, 0, 0}) == 0 && dsound.buffersPlaying() == 1);
    clock = 250;
    CHECK(call(buffer, 4, {out, out2}) == 0 && cpu.read32(out) == 250 && cpu.read32(out2) == 265);
    CHECK(call(buffer, 9, {out}) == 0 && cpu.read32(out) == 0x1); // GetStatus: playing
    std::vector<int16_t> mixed(2 * 6000);
    mixer.mix(mixed.data(), 6000);
    clock = 1500;
    CHECK(call(buffer, 9, {out}) == 0 && cpu.read32(out) == 0 && dsound.buffersPlaying() == 0);
    // The first 100 bytes, heard (0xc0 is +0x40 << 8), but further off:
    // the listener is 3.7 from the buffer, three times its minimum
    // distance, so a little over a quarter as loud.
    mixed.assign(2 * 22050, 0);
    mixer.mix(mixed.data(), 22050);
    const int16_t loudest = *std::max_element(mixed.begin(), mixed.end());
    CHECK(loudest > 0x400 && loudest < 0x2000);

    // Looped from 100: it wraps round, and stays where it's stopped.
    CHECK(call(buffer, 13, {100}) == 0);     // SetCurrentPosition
    CHECK(call(buffer, 12, {0, 0, 1}) == 0); // Play, looping
    clock = 2600;
    CHECK(call(buffer, 4, {out, 0}) == 0 && cpu.read32(out) == 200);
    CHECK(call(buffer, 9, {out}) == 0 && cpu.read32(out) == (0x1 | 0x4));
    CHECK(call(buffer, 18, {}) == 0); // Stop
    clock = 4000;
    CHECK(call(buffer, 4, {out, 0}) == 0 && cpu.read32(out) == 200);

    // Volume, frequency; a duplicate shares its sound.
    CHECK(call(buffer, 15, {uint32_t(-600)}) == 0 && call(buffer, 6, {out}) == 0 && cpu.read32(out) == uint32_t(-600));
    CHECK(call(buffer, 8, {out}) == 0 && cpu.read32(out) == 1000);
    CHECK(call(ds, 5, {buffer, out}) == 0); // DuplicateSoundBuffer
    const uint32_t copy = cpu.read32(out);
    CHECK(call(copy, 11, {0, 0, out3, out4, 0, 0, 0x2}) == 0 && cpu.read32(out3) == sound);

    // Its 3D position, kept.
    CHECK(call(buffer, 0, {guid("{279afa86-4981-11ce-a521-0020af0be560}"), out}) == 0);
    const uint32_t buffer3D = cpu.read32(out);
    CHECK(call(buffer3D, 19, {bits(5), 0, 0, 0}) == 0); // SetPosition
    CHECK(call(buffer3D, 10, {out}) == 0 && cpu.read32(out) == bits(5));
    CHECK(call(buffer3D, 18, {1, 0}) == 0 && call(buffer3D, 9, {out}) == 0 && cpu.read32(out) == 1); // the mode
    for (uint32_t p : {buffer3D, copy, buffer, listener, primary, ds})
        call(p, 2, {}); // Release
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
               {"msvcrt and files", [](const std::filesystem::path &tmp) { testMsvcrtAndFiles(tmp / "msvcrt"); }},
               {"msvcp60", [](const std::filesystem::path &tmp) { testMsvcp60(tmp / "msvcp60"); }},
               {"windows and registry", [](const std::filesystem::path &) { testWindowsAndRegistry(); }},
               {"directx", [](const std::filesystem::path &) { testDirectX(); }},
               {"input", [](const std::filesystem::path &) { testInput(); }},
               {"a3d", [](const std::filesystem::path &) { testA3d(); }},
               {"directsound", [](const std::filesystem::path &) { testDirectSound(); }},
               {"direct3d 5", [](const std::filesystem::path &) { testDirect3D5(); }},
               {"retained mode", [](const std::filesystem::path &) { testRetainedMode(); }},
               {"user and kernel, more", [](const std::filesystem::path &tmp) { testUserAndKernelMore(tmp); }},
               {"pixel conversion", [](const std::filesystem::path &) { testArgbConverter(); }},
           });
