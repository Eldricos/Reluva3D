// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Windows a program sees in the x86 emulator: threads, the C and C++
// runtimes, files, windows and the registry, DirectDraw and Direct3D 7,
// DirectInput, Aureal A3D (and the mixer's SoundStream it plays through).
//
//   reluva-tests common.unit.windows

#include "audio/mixer.h"
#include "audio/wav.h"
#include "common/fs.h"
#include "common/net.h"
#include "common/stream.h"
#include "common/types.h"
#include "emulation/x86/a3d.h"
#include "emulation/x86/cdaudio.h"
#include "emulation/x86/directx/dsound.h"
#include "emulation/x86/directx/sound3d.h"
#include "emulation/x86/dplay.h"
#include "emulation/x86/joystick.h"
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
    // A mark and the keypad's digits type their characters too.
    for (const auto &[vk, ch] : std::vector<std::pair<uint32_t, uint32_t>>{{0xbe, '.'}, {0xbd, '-'}, {0x67, '7'}}) {
        process.postMessage(hwnd, 0x100, vk, 0);
        CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1);
        CHECK(!cpu.call(fn("USER32.dll", "TranslateMessage"), {msg}, r) && r == 1);
        CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r == 1 &&
              cpu.read32(msg + 4) == 0x102 && cpu.read32(msg + 8) == ch);
    }
    // GetMessageA takes what's queued, and says 0 for WM_QUIT.
    process.postMessage(hwnd, 0x400, 7, 0); // WM_USER
    CHECK(!cpu.call(fn("USER32.dll", "GetMessageA"), {msg, 0, 0, 0}, r) && r == 1 && cpu.read32(msg + 4) == 0x400 &&
          cpu.read32(msg + 8) == 7);
    process.postMessage(hwnd, 0x12, 5, 0); // WM_QUIT
    CHECK(!cpu.call(fn("USER32.dll", "GetMessageA"), {msg, 0, 0, 0}, r) && r == 0 && cpu.read32(msg + 8) == 5);
    CHECK(!cpu.call(fn("USER32.dll", "PostQuitMessage"), {3}, r));
    CHECK(!cpu.call(fn("USER32.dll", "PeekMessageA"), {msg, hwnd, 0, 0, 1}, r) && r == 1 &&
          cpu.read32(msg + 4) == 0x12 && cpu.read32(msg + 8) == 3);
    // Closing through DefWindowProcA destroys the window.
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "DefWindowProcA"), {hwnd, 0x10, 0, 0}, r));
    CHECK((seen == std::vector<uint32_t>{0x2, 0x82}) && process.window(hwnd) == nullptr);
    // Made visible (WS_VISIBLE), a window is shown and activated as by ShowWindow.
    seen.clear();
    CHECK(!cpu.call(fn("USER32.dll", "CreateWindowExA"),
                    {0, text("testclass"), text("Title"), 0x10cf0000, 0, 0, 640, 480, 0, 0, 0, 0}, r) &&
          r != 0);
    const uint32_t shown = r;
    CHECK((seen == std::vector<uint32_t>{0x81, 0x1, 0x18, 0x1c, 0x6, 0x7}));
    CHECK(!cpu.call(fn("USER32.dll", "GetActiveWindow"), {}, r) && r == shown);
    CHECK(!cpu.call(fn("USER32.dll", "SetFocus"), {0}, r) && r == shown);
    CHECK(!cpu.call(fn("USER32.dll", "GetFocus"), {}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "SetWindowLongA"), {shown, uint32_t(-21), 1234}, r) && r == 0);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {shown, uint32_t(-21)}, r) && r == 1234);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {shown, uint32_t(-4)}, r) && r == proc);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowLongA"), {shown, uint32_t(-16)}, r) && r == 0x10cf0000);
    CHECK(!cpu.call(fn("USER32.dll", "IsWindow"), {shown}, r) && r == 1);
    CHECK(!cpu.call(fn("USER32.dll", "GetWindowThreadProcessId"), {shown, 0}, r) && r != 0);
    CHECK(!cpu.call(fn("USER32.dll", "GetDesktopWindow"), {}, r) && r != 0 && r != shown);
    const uint32_t paint = process.memory().alloc(64);
    CHECK(!cpu.call(fn("USER32.dll", "BeginPaint"), {shown, paint}, r) && r != 0 && cpu.read32(paint) == r);
    // FILETIME to SYSTEMTIME: 2000-02-29 12:34:56.789 UTC, a Tuesday.
    const uint32_t fileTime = process.memory().alloc(8);
    const uint32_t systemTime = process.memory().alloc(16);
    const uint64_t ticks = 125963012967890000ull;
    cpu.write32(fileTime, uint32_t(ticks));
    cpu.write32(fileTime + 4, uint32_t(ticks >> 32));
    CHECK(!cpu.call(fn("KERNEL32.dll", "FileTimeToSystemTime"), {fileTime, systemTime}, r) && r == 1);
    const uint16_t expected[8] = {2000, 2, 2, 29, 12, 34, 56, 789};
    for (uint32_t i = 0; i < 8; ++i)
        CHECK(cpu.read16(systemTime + 2 * i) == expected[i]);
    const uint32_t copy = process.memory().alloc(16);
    CHECK(!cpu.call(fn("KERNEL32.dll", "lstrcpyA"), {copy, text("copied")}, r) && r == copy &&
          cpu.string(copy) == "copied");

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
    // Direct3D 1's devices, as DirectX 5 programs list them: software, then
    // hardware, a description each for hardware and for software.
    CHECK(call(dd, 0, {guid("{3bba0080-2421-11cf-a31a-00aa00b93356}"), out}) == 0);
    const uint32_t d3d1 = cpu.read32(out);
    std::vector<uint32_t> colorModels; // the hardware description's, for each device
    const uint32_t listed = cpu.addNative("test!EnumDevices", [&](x86::Cpu &c) {
        CHECK(c.read32(c.arg(3)) == 204 && c.read32(c.arg(4) + 8) == 2); // sizes; software always RGB
        colorModels.push_back(c.read32(c.arg(3) + 8));
        c.reg[x86::Cpu::EAX] = 1; // D3DENUMRET_OK
        return 24u;
    });
    CHECK(call(d3d1, 4, {listed, 0}) == 0);
    CHECK((colorModels == std::vector<uint32_t>{0, 2}));
    call(d3d1, 2, {});                            // Release: the DirectDraw object stays
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
    // GDI text through the back buffer's device context: red on what was
    // there (black), left alone around it.
    CHECK(call(back, 17, {out}) == 0); // GetDC
    const uint32_t dc = cpu.read32(out);
    CHECK(call(back, 17, {out}) != 0); // one at a time
    auto gdi = [&](const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided("GDI32.dll", name), args, r));
        return r;
    };
    const uint32_t text = mem(4);
    cpu.writeString(text, "Hi", 4);
    const uint32_t font = gdi("CreateFontA", {uint32_t(-9), 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(gdi("SelectObject", {dc, font}) != 0);
    CHECK(gdi("SetBkMode", {dc, 1}) == 2); // TRANSPARENT, from OPAQUE
    CHECK(gdi("SetTextColor", {dc, 0x0000ff}) == 0);
    CHECK(gdi("TextOutA", {dc, 2, 3, text, 2}) == 1);
    const uint32_t extent = mem(8);
    CHECK(gdi("GetTextExtentPoint32A", {dc, text, 2, extent}) == 1 && cpu.read32(extent) == 12 &&
          cpu.read32(extent + 4) == 11);
    CHECK(call(back, 26, {dc}) == 0); // ReleaseDC
    const uint32_t lockDesc = mem(124);
    cpu.write32(lockDesc, 124);
    CHECK(call(back, 25, {0, lockDesc, 1, 0}) == 0); // Lock
    {
        const uint32_t pixels = cpu.read32(lockDesc + 36);
        const uint32_t pitch = cpu.read32(lockDesc + 16);
        int red = 0;
        int other = 0;
        for (uint32_t y = 0; y < 20; ++y)
            for (uint32_t x = 0; x < 20; ++x) {
                const uint16_t v = cpu.read16(pixels + y * pitch + 2 * x);
                const bool inside = x >= 2 && x < 14 && y >= 3 && y < 14;
                red += v == 0xf800 && inside;
                other += v != 0 && (v != 0xf800 || !inside);
            }
        CHECK(red > 10 && other == 0);
    }
    CHECK(call(back, 32, {0}) == 0); // Unlock
    gdi("DeleteObject", {font});
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
    // Held by the program once it's acquired exclusively (DISCL_EXCLUSIVE).
    CHECK(!directInput.mouseHeld());
    CHECK(call(mouse, 13, {0, 5}) == 0); // SetCooperativeLevel: exclusive, foreground
    CHECK(directInput.mouseHeld());
    CHECK(call(mouse, 8, {}) == 0 && !directInput.mouseHeld()); // Unacquire
    CHECK(call(mouse, 7, {}) == 0 && directInput.mouseHeld());
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
    // An extended key: the up arrow's scan code 0x48 is DIK_UP, 0xc8.
    process.key(0x26, 0x148, true);
    CHECK(call(keyboard, 9, {256, state}) == 0 && cpu.read8(state + 0xc8) == 0x80 && cpu.read8(state + 0x48) == 0);
    process.key(0x26, 0x148, false);
}

// DirectSound 3D's model (directx/sound3d.h): distance, pan, cone and Doppler.
static void testSound3d() {
    using namespace x86;
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-4; };
    Ds3dListener listener;
    Ds3dSource s;
    // Ahead, within the minimum distance: as it is.
    s.position[2] = 0.5f;
    Ds3dResult r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1) && near(r.doppler, 1));
    // Ahead at 2 and 4 times the minimum distance: -6 and -12 dB or so;
    // twice the rolloff, faster; past the maximum distance, no quieter.
    s.position[2] = 2;
    CHECK(near(spatialize(listener, s).left, 0.5));
    s.position[2] = 4;
    CHECK(near(spatialize(listener, s).left, 0.25));
    listener.rolloffFactor = 2;
    CHECK(near(spatialize(listener, s).left, 1.0 / 7));
    listener.rolloffFactor = 1;
    s.maxDistance = 2;
    CHECK(near(spatialize(listener, s).left, 0.5));
    s.maxDistance = 1e9f;
    // Straight to the right: the left speaker 20 dB down; to the left, the
    // other way round; 30 degrees right, the left down by 10 dB.
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, 0.1));
    s.position[0] = -1;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 0.1));
    s.position[0] = 0.5f;
    s.position[2] = std::sqrt(0.75f);
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, std::pow(10.0, -0.5)));
    // The listener turned to face +x: a source at +x is ahead; one at +z is
    // now to its left.
    listener.front[0] = 1;
    listener.front[2] = 0;
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1));
    s.position[0] = 0;
    s.position[2] = 1;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 0.1));
    // Straight behind (-x for this listener): both speakers 3 dB down and
    // muffled to 4 kHz; beside, neither.
    s.position[0] = -1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.behind, 1) && near(r.left, std::pow(10.0, -0.15)) && near(r.right, r.left));
    CHECK(near(rearCutoff(r.behind), 4000) && near(rearCutoff(0), 20000) && near(rearGain(0), 1));
    CHECK(near(lowPassCoefficient(4000, 44100), 1 - std::exp(-2 * 3.14159265358979 * 4000 / 44100)));
    // Head-relative: the position is the listener's own, whichever way it
    // faces; disabled: as it is, wherever.
    s.mode = 1;
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, 0.1));
    s.mode = 2;
    s.position[0] = 100;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1));
    s = Ds3dSource();
    listener = Ds3dListener();
    // A cone pointing away (-z) from a listener at the origin, the source
    // ahead of it at z 1: outside, so -20 dB.
    s.position[2] = 1;
    s.insideCone = 90;
    s.outsideCone = 180;
    s.coneOrientation[2] = 1;
    s.coneOutsideVolume = -2000;
    CHECK(near(spatialize(listener, s).left, 0.1));
    s.coneOrientation[2] = -1; // pointing at the listener: inside
    CHECK(near(spatialize(listener, s).left, 1));
    s = Ds3dSource();
    // Doppler: a source 10 units ahead coming at the listener at a tenth of
    // the speed of sound sounds higher, 1 / 0.9; going away, lower.
    s.position[2] = 10;
    s.velocity[2] = -34.33f;
    CHECK(near(spatialize(listener, s).doppler, 1 / 0.9));
    s.velocity[2] = 34.33f;
    CHECK(near(spatialize(listener, s).doppler, 1 / 1.1));
    listener.dopplerFactor = 0;
    CHECK(near(spatialize(listener, s).doppler, 1));
}

// DirectSound called as a program calls it: a buffer streamed into as it
// plays, as SMACKW32 does, its play position by the clock, and the card's
// output; a 3D buffer placed off to one side.
static void testDirectSound() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 1000;
    process.milliseconds = [&] { return clock; };
    x86::DirectSound sound(process);
    uint32_t r = 0;
    auto mem = [&](uint32_t size) { return process.memory().alloc(size); };
    auto call = [&](uint32_t self, int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), self);
        CHECK(!cpu.call(cpu.read32(cpu.read32(self) + 4 * uint32_t(index)), args, r));
        return r;
    };
    const uint32_t out = mem(4);
    CHECK(!cpu.call(process.provided("DSOUND.dll", "DirectSoundCreate"), {0, out, 0}, r) && r == 0);
    const uint32_t ds = cpu.read32(out);
    CHECK(call(ds, 6, {0, 2}) == 0); // SetCooperativeLevel: priority
    // A quarter of a second of 16-bit stereo at 22050 Hz, as SMACKW32 makes it.
    const uint32_t format = mem(18);
    const uint16_t wave[9] = {1, 2, 22050 & 0xffff, 0, 0x5888, 1, 4, 16, 0};
    for (uint32_t i = 0; i < 9; ++i)
        cpu.write16(format + 2 * i, wave[i]);
    const uint32_t desc = mem(20);
    const uint32_t descFields[5] = {20, 0x8000 | 0x80, 22048, 0, format}; // GLOBALFOCUS, CTRLVOLUME
    for (uint32_t i = 0; i < 5; ++i)
        cpu.write32(desc + 4 * i, descFields[i]);
    CHECK(call(ds, 3, {desc, out, 0}) == 0); // CreateSoundBuffer
    const uint32_t buffer = cpu.read32(out);
    // Each frame's samples say which frame of the stream it is: left n,
    // right -n (n counting from 1, wrapping at 32000).
    int written = 0;
    const uint32_t p1 = mem(4);
    const uint32_t n1 = mem(4);
    const uint32_t p2 = mem(4);
    const uint32_t n2 = mem(4);
    auto fill = [&](uint32_t offset, uint32_t bytes) {
        CHECK(call(buffer, 11, {offset, bytes, p1, n1, p2, n2, 0}) == 0); // Lock
        CHECK(cpu.read32(n1) + cpu.read32(n2) == bytes);
        for (int part = 0; part < 2; ++part) {
            const uint32_t at = cpu.read32(part ? p2 : p1);
            for (uint32_t i = 0; i < cpu.read32(part ? n2 : n1); i += 4) {
                const int16_t v = int16_t(written++ % 32000 + 1);
                cpu.write16(at + i, uint16_t(v));
                cpu.write16(at + i + 2, uint16_t(-v));
            }
        }
        CHECK(call(buffer, 19, {cpu.read32(p1), cpu.read32(n1), cpu.read32(p2), cpu.read32(n2)}) == 0); // Unlock
    };
    fill(0, 22048);
    CHECK(call(buffer, 12, {0, 0, 1}) == 0); // Play, looping
    const uint32_t status = mem(4);
    CHECK(call(buffer, 9, {status}) == 0 && cpu.read32(status) == 5); // PLAYING | LOOPING
    // Streamed for two seconds: each 62 ms a quarter of the buffer behind
    // the play position written again with what comes next, the card's
    // output read a millisecond at a time, as an audio device takes it.
    const std::shared_ptr<audio::OutputStream> card = sound.output();
    std::vector<int16_t> heard;
    uint32_t nextQuarter = 0;
    const uint32_t play = mem(4);
    const uint32_t write = mem(4);
    for (int ms = 0; ms < 2000; ++ms) {
        clock += 1;
        CHECK(call(buffer, 4, {play, write}) == 0); // GetCurrentPosition
        const uint32_t at = cpu.read32(play);
        // The quarter just played is written again.
        if ((at / 5512 + 4 - nextQuarter) % 4 >= 2) {
            fill(nextQuarter * 5512, 5512);
            nextQuarter = (nextQuarter + 1) % 4;
        }
        int16_t frames[2 * 23];
        const int n = 22 + (ms % 20 == 0 ? 1 : 0); // 22.05 frames a millisecond
        card->read(frames, n, 22050);
        heard.insert(heard.end(), frames, frames + 2 * n);
    }
    // Heard in order, every frame once: n then n + 1, right the negative of
    // left, after the first quarter of a second.
    int outOfOrder = 0;
    for (size_t i = 2 * 5512; i + 3 < heard.size(); i += 2) {
        outOfOrder += heard[i + 1] != -heard[i];
        const int next = heard[i] % 32000 + 1;
        outOfOrder += heard[i + 2] != next;
    }
    CHECK(outOfOrder == 0);
    CHECK(sound.buffersPlaying() == 1);
    CHECK(call(buffer, 18, {}) == 0 && sound.buffersPlaying() == 0); // Stop

    // A 3D buffer, mono, a constant 1000, put a unit to the listener's
    // right: the left speaker 20 dB down (sound3d.h), the right as it is.
    // Its 3D interface comes from QueryInterface; the listener from the
    // primary buffer's.
    const uint16_t mono[9] = {1, 1, 22050 & 0xffff, 0, 0xac44, 0, 2, 16, 0};
    for (uint32_t i = 0; i < 9; ++i)
        cpu.write16(format + 2 * i, mono[i]);
    cpu.write32(desc + 4, 0x10 | 0x80); // CTRL3D, CTRLVOLUME
    cpu.write32(desc + 8, 4410);
    CHECK(call(ds, 3, {desc, out, 0}) == 0);
    const uint32_t placed = cpu.read32(out);
    CHECK(call(placed, 11, {0, 4410, p1, n1, p2, n2, 0}) == 0);
    for (uint32_t i = 0; i < 4410; i += 2)
        cpu.write16(cpu.read32(p1) + i, 1000);
    CHECK(call(placed, 19, {cpu.read32(p1), 4410, 0, 0}) == 0);
    auto iid = [&](const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    };
    CHECK(call(placed, 0, {iid("{279afa86-4981-11ce-a521-0020af0be560}"), out}) == 0);
    const uint32_t placed3d = cpu.read32(out);
    auto bits = [](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        return v;
    };
    CHECK(call(placed3d, 19, {bits(1), bits(0), bits(0), 0}) == 0); // SetPosition, at once
    // The primary buffer's listener, deferred settings applied on Commit.
    cpu.write32(desc + 4, 0x1 | 0x10); // PRIMARYBUFFER, CTRL3D
    cpu.write32(desc + 8, 0);
    cpu.write32(desc + 16, 0);
    CHECK(call(ds, 3, {desc, out, 0}) == 0);
    const uint32_t primary = cpu.read32(out);
    CHECK(call(primary, 0, {iid("{279afa84-4981-11ce-a521-0020af0be560}"), out}) == 0);
    const uint32_t listener = cpu.read32(out);
    CHECK(call(listener, 14, {bits(0), bits(0), bits(0), 1}) == 0); // SetPosition, deferred
    CHECK(call(listener, 17, {}) == 0);                             // CommitDeferredSettings
    CHECK(call(placed, 12, {0, 0, 1}) == 0);                        // Play, looping
    int16_t frames[2 * 100];
    card->read(frames, 100, 22050);
    CHECK(frames[198] >= 95 && frames[198] <= 105 && frames[199] >= 995 && frames[199] <= 1005);
    // Behind a deferred SetPosition, uncommitted, it stays where it was.
    CHECK(call(placed3d, 19, {bits(-1), bits(0), bits(0), 1}) == 0);
    card->read(frames, 100, 22050);
    CHECK(frames[199] >= 995 && frames[199] <= 1005);
    CHECK(call(listener, 17, {}) == 0); // now it's on the left
    card->read(frames, 100, 22050);
    CHECK(frames[198] >= 995 && frames[198] <= 1005 && frames[199] >= 95 && frames[199] <= 105);
}

// MCI's cdaudio device driven as Jedi Knight drives it, on a disc of
// ripped tracks: opened, set to TMSF, a track played from it to the next,
// its mode as the clock moves, a track's length, a range off the disc's end;
// the CD volume through the auxiliary device.
static void testCdAudio(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    // Tracks 2 to 4, half a second each, left n * 100 and right -n * 100.
    for (int n = 2; n <= 4; ++n) {
        audio::Sound track;
        track.rate = 44100;
        track.channels = 2;
        for (int i = 0; i < 22050; ++i)
            track.samples.insert(track.samples.end(), {int16_t(n * 100), int16_t(-n * 100)});
        char name[16];
        std::snprintf(name, sizeof name, "track%02d.wav", n);
        CHECK(writeFile(tmp / name, audio::encodePcmWav(track)));
    }
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    uint32_t clock = 1000;
    process.milliseconds = [&] { return clock; };
    x86::CdAudio cd(process);
    bool second = false; // the other disc in the drive: track 2 alone
    cd.disc = [&] { return x86::CdAudio::discIn(second ? tmp / "second" : tmp); };
    audio::Mixer mixer(44100);
    cd.mixer = &mixer;
    uint32_t r = 0;
    auto mci = [&](uint32_t device, uint32_t message, uint32_t flags, uint32_t parms) {
        CHECK(!cpu.call(process.provided("WINMM.dll", "mciSendCommandA"), {device, message, flags, parms}, r));
        return r;
    };
    const uint32_t parms = process.memory().alloc(32);
    const uint32_t type = process.memory().alloc(16);
    cpu.writeString(type, "CDAudio", 16);
    cpu.write32(parms + 8, type);
    CHECK(mci(0, 0x803, 0x2000, parms) == 0 && cpu.read32(parms + 4) == 1); // MCI_OPEN, by its type
    cpu.write32(parms + 4, 10);
    CHECK(mci(1, 0x80d, 0x400, parms) == 0); // MCI_SET: TMSF
    auto status = [&](uint32_t item, uint32_t flags = 0x100, uint32_t track = 0) {
        cpu.write32(parms + 8, item);
        cpu.write32(parms + 12, track);
        CHECK(mci(1, 0x814, flags, parms) == 0);
        return cpu.read32(parms + 4);
    };
    CHECK(status(3) == 4);                     // tracks, the data track among them
    CHECK(status(1, 0x110, 3) == (37u << 16)); // track 3's length: 0:00 and 37 frames
    CHECK(status(0x4001, 0x110, 1) == 0x441);  // track 1 isn't audio
    // Track 2 to the start of 3: its sound, then stopped.
    cpu.write32(parms + 4, 2);
    cpu.write32(parms + 8, 3);
    CHECK(mci(1, 0x806, 0xc, parms) == 0 && cd.trackPlaying() == 2);
    CHECK(status(4) == 526); // MCI_MODE_PLAY
    int16_t frames[2 * 64];
    mixer.mix(frames, 64);
    CHECK(frames[126] > 190 && frames[126] < 210 && frames[127] < -190 && frames[127] > -210);
    clock += 600;
    CHECK(status(4) == 525 && cd.trackPlaying() == 0); // MCI_MODE_STOP, at its end
    // The volume through the CD's auxiliary device: 0, and nothing's heard.
    CHECK(!cpu.call(process.provided("WINMM.dll", "auxGetNumDevs"), {}, r) && r == 1);
    const uint32_t caps = process.memory().alloc(48);
    CHECK(!cpu.call(process.provided("WINMM.dll", "auxGetDevCapsA"), {0, caps, 48}, r) && r == 0 &&
          cpu.read16(caps + 40) == 1); // AUXCAPS_CDAUDIO
    CHECK(!cpu.call(process.provided("WINMM.dll", "auxSetVolume"), {0, 0}, r) && r == 0);
    cpu.write32(parms + 4, 3);
    cpu.write32(parms + 8, 5); // the end of the disc
    CHECK(mci(1, 0x806, 0xc, parms) == 0 && cd.trackPlaying() == 3);
    mixer.mix(frames, 64);
    CHECK(frames[126] == 0 && frames[127] == 0);
    // Past the end, as the game asks after the last track: out of range.
    cpu.write32(parms + 8, 6);
    CHECK(mci(1, 0x806, 0xc, parms) == 282);
    // Another disc put in: what played stops, and the device has the new one.
    std::filesystem::create_directories(tmp / "second");
    std::filesystem::copy_file(tmp / "track02.wav", tmp / "second" / "track02.wav");
    second = true;
    cd.discChanged();
    CHECK(cd.trackPlaying() == 0 && status(3) == 2 && status(4) == 525);
    CHECK(mci(1, 0x804, 0, parms) == 0);       // MCI_CLOSE
    CHECK(mci(1, 0x814, 0x100, parms) == 257); // closed: no such device
    cpu.writeString(type, "waveaudio", 16);
    CHECK(mci(0, 0x803, 0x2000, parms) == 263);            // only the CD
    CHECK(x86::CdAudio::discIn(tmp / "none").tracks == 0); // no disc
    std::filesystem::remove_all(tmp);
}

// Direct3D 1 driven as a DirectX 5 program drives it: the device asked of
// a 16-bit back buffer by its IID, a viewport, an execute buffer of
// pre-transformed vertices copied in, a render state and two triangles,
// executed; a texture's handle and Load; materials' handles for a clear.
static void testDirect3D1() {
    x86::Cpu cpu(32u << 20);
    x86::Win32Process process(cpu);
    x86::DirectX dx(process);
    dx.modes.push_back({64, 48, 16});
    dx.drawThreads = x86::DirectX::DrawThreads::kOne;
    uint32_t r = 0;
    auto mem = [&](uint32_t size) {
        const uint32_t a = process.memory().alloc(size);
        std::memset(cpu.memory().data() + a, 0, size);
        return a;
    };
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
    const uint32_t dd = cpu.read32(out);    // IDirectDraw
    CHECK(call(dd, 20, {0, 0x11}) == 0);    // SetCooperativeLevel
    CHECK(call(dd, 21, {64, 48, 16}) == 0); // SetDisplayMode (IDirectDraw's: no refresh rate)
    const uint32_t desc = mem(108);
    cpu.write32(desc, 108);
    cpu.write32(desc + 4, 0x1 | 0x20); // CAPS, BACKBUFFERCOUNT
    cpu.write32(desc + 20, 1);
    cpu.write32(desc + 104, 0x200 | 0x10 | 0x8 | 0x2000); // PRIMARY, FLIP, COMPLEX, 3DDEVICE
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t primary = cpu.read32(out);
    const uint32_t caps = mem(4);
    cpu.write32(caps, 0x4);
    CHECK(call(primary, 12, {caps, out}) == 0); // GetAttachedSurface: the back buffer
    const uint32_t back = cpu.read32(out);
    // The device, by the HAL's IID; and Direct3D, for a viewport and materials.
    CHECK(call(back, 0, {guid("{84e63de0-46aa-11cf-816f-0000c020156e}"), out}) == 0);
    const uint32_t device = cpu.read32(out);
    CHECK(call(dd, 0, {guid("{3bba0080-2421-11cf-a31a-00aa00b93356}"), out}) == 0);
    const uint32_t d3d = cpu.read32(out);
    CHECK(call(d3d, 7, {out, 0}) == 0); // CreateViewport
    const uint32_t viewport = cpu.read32(out);
    CHECK(call(device, 9, {viewport}) == 0); // AddViewport
    const uint32_t vp = mem(44);
    const uint32_t vpFields[11] = {44, 0, 0, 64, 48, bits(32), bits(24), bits(1), bits(1), bits(0), bits(1)};
    for (uint32_t i = 0; i < 11; ++i)
        cpu.write32(vp + 4 * i, vpFields[i]);
    CHECK(call(viewport, 5, {vp}) == 0); // SetViewport
    // A blue material as the background, cleared to.
    CHECK(call(d3d, 6, {out, 0}) == 0); // CreateMaterial
    const uint32_t material = cpu.read32(out);
    const uint32_t mat = mem(80);
    cpu.write32(mat, 80);
    cpu.write32(mat + 12, bits(1));                                  // diffuse blue
    cpu.write32(mat + 16, bits(1));                                  // and alpha
    CHECK(call(material, 4, {mat}) == 0);                            // SetMaterial
    CHECK(call(material, 6, {device, out}) == 0 && cpu.read32(out)); // GetHandle
    CHECK(call(viewport, 8, {cpu.read32(out)}) == 0);                // SetBackground
    const uint32_t rect = mem(16);
    const uint32_t rectFields[4] = {0, 0, 64, 48};
    for (uint32_t i = 0; i < 4; ++i)
        cpu.write32(rect + 4 * i, rectFields[i]);
    CHECK(call(viewport, 12, {1, rect, 1}) == 0); // Clear: the target
    // An execute buffer: four D3DTLVERTEXs (a red square from 16,12 to
    // 48,36), then PROCESSVERTICES (copy), STATERENDER (no texture),
    // TRIANGLE x 2, EXIT.
    const uint32_t ebDesc = mem(20);
    cpu.write32(ebDesc, 20);
    cpu.write32(ebDesc + 4, 0x2);
    cpu.write32(ebDesc + 12, 1024);
    CHECK(call(device, 6, {ebDesc, out, 0}) == 0); // CreateExecuteBuffer
    const uint32_t eb = cpu.read32(out);
    CHECK(call(eb, 4, {ebDesc}) == 0); // Lock
    const uint32_t data = cpu.read32(ebDesc + 16);
    const float corners[4][2] = {{16, 12}, {48, 12}, {48, 36}, {16, 36}};
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t v = data + 32 * i;
        cpu.write32(v, bits(corners[i][0]));
        cpu.write32(v + 4, bits(corners[i][1]));
        cpu.write32(v + 8, bits(0.5f));
        cpu.write32(v + 12, bits(1));
        cpu.write32(v + 16, 0xffff0000u); // red
    }
    uint32_t at = data + 128;
    auto instruction = [&](uint8_t op, uint8_t size, uint16_t count) {
        cpu.write8(at, op);
        cpu.write8(at + 1, size);
        cpu.write16(at + 2, count);
        at += 4;
    };
    instruction(9, 16, 1); // PROCESSVERTICES: copy, from 0 to 0, 4 of them
    cpu.write32(at, 2);
    cpu.write16(at + 4, 0);
    cpu.write16(at + 6, 0);
    cpu.write32(at + 8, 4);
    at += 16;
    instruction(8, 8, 1); // STATERENDER: TEXTUREHANDLE 0
    cpu.write32(at, 1);
    cpu.write32(at + 4, 0);
    at += 8;
    instruction(3, 8, 2); // TRIANGLE x 2
    const uint16_t tris[2][4] = {{0, 1, 2, 0}, {0, 2, 3, 0}};
    for (const auto &t : tris) {
        for (uint32_t k = 0; k < 4; ++k)
            cpu.write16(at + 2 * k, t[k]);
        at += 8;
    }
    instruction(11, 0, 0);       // EXIT
    CHECK(call(eb, 5, {}) == 0); // Unlock
    const uint32_t exec = mem(48);
    const uint32_t execFields[6] = {48, 0, 4, 128, at - data - 128, 0};
    for (uint32_t i = 0; i < 6; ++i)
        cpu.write32(exec + 4 * i, execFields[i]);
    CHECK(call(eb, 6, {exec}) == 0);                // SetExecuteData
    CHECK(call(device, 19, {}) == 0);               // BeginScene
    CHECK(call(device, 8, {eb, viewport, 1}) == 0); // Execute
    CHECK(call(device, 20, {}) == 0);               // EndScene
    // Red inside, the blue background outside (565).
    const uint32_t lock = mem(108);
    cpu.write32(lock, 108);
    CHECK(call(back, 25, {0, lock, 1, 0}) == 0); // Lock
    const uint32_t pixels = cpu.read32(lock + 36);
    const uint32_t pitch = cpu.read32(lock + 16);
    CHECK(cpu.read16(pixels + 24 * pitch + 2 * 32) == 0xf800);
    CHECK(cpu.read16(pixels + 2 * pitch + 2 * 2) == 0x001f);
    CHECK(call(back, 32, {0}) == 0); // Unlock
    // A texture: its IDirect3DTexture from the surface, a handle, and Load.
    std::memset(cpu.memory().data() + desc, 0, 108);
    cpu.write32(desc, 108);
    cpu.write32(desc + 4, 0x1 | 0x2 | 0x4 | 0x1000); // CAPS, HEIGHT, WIDTH, PIXELFORMAT
    cpu.write32(desc + 8, 2);
    cpu.write32(desc + 12, 2);
    cpu.write32(desc + 72, 32);
    cpu.write32(desc + 76, 0x40); // RGB
    cpu.write32(desc + 84, 16);
    cpu.write32(desc + 88, 0xf800);
    cpu.write32(desc + 92, 0x07e0);
    cpu.write32(desc + 96, 0x001f);
    cpu.write32(desc + 104, 0x1000 | 0x800); // TEXTURE, SYSTEMMEMORY
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t source = cpu.read32(out);
    cpu.write32(desc + 104, 0x1000 | 0x4000); // TEXTURE, VIDEOMEMORY
    CHECK(call(dd, 6, {desc, out, 0}) == 0);
    const uint32_t texSurface = cpu.read32(out);
    CHECK(call(source, 25, {0, lock, 1, 0}) == 0);
    cpu.write16(cpu.read32(lock + 36), 0x07e0); // green
    CHECK(call(source, 32, {0}) == 0);
    const char *textureIid = "{2cdcd9e0-25a0-11cf-a31a-00aa00b93356}";
    CHECK(call(source, 0, {guid(textureIid), out}) == 0);
    const uint32_t from = cpu.read32(out);
    CHECK(call(texSurface, 0, {guid(textureIid), out}) == 0);
    const uint32_t texture = cpu.read32(out);
    CHECK(call(texture, 6, {from}) == 0);                                // Load
    CHECK(call(texture, 4, {device, out}) == 0 && cpu.read32(out) != 0); // GetHandle
    CHECK(call(texSurface, 25, {0, lock, 1, 0}) == 0 && cpu.read16(cpu.read32(lock + 36)) == 0x07e0);
    CHECK(call(texSurface, 32, {0}) == 0);
}

// DirectPlay 3 created through COM, with no connections to offer.
// One DirectPlay object in a process of its own, on the loopback network,
// called as a program calls it.
struct PlaySide {
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    x86::DirectPlay directPlay{process};
    uint32_t dp = 0;

    explicit PlaySide(const std::shared_ptr<net::LoopbackNetwork> &network) {
        directPlay.network.open = [network](uint16_t port) { return network->open(port); };
        directPlay.network.search = {net::Address::broadcast(x86::kDirectPlayPort)};
        const uint32_t clsid = guid("{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}");
        const uint32_t iid = guid("{133efe41-32dc-11d0-9cfb-00a0c90a43cb}"); // IDirectPlay3A
        const uint32_t out = mem(4);
        uint32_t r = 1;
        CHECK(!cpu.call(process.provided("ole32.dll", "CoCreateInstance"), {clsid, 0, 1, iid, out}, r) && r == 0);
        dp = cpu.read32(out);
    }
    uint32_t mem(uint32_t size) { return process.memory().alloc(size); }
    uint32_t guid(const char *g) {
        const uint32_t a = mem(16);
        x86::ComHost::writeGuid(cpu, a, g);
        return a;
    }
    uint32_t text(const std::string &t) {
        const uint32_t a = mem(uint32_t(t.size()) + 1);
        cpu.writeString(a, t, uint32_t(t.size()) + 1);
        return a;
    }
    // IDirectPlay3A's method `index`.
    uint32_t call(int index, std::vector<uint32_t> args) {
        args.insert(args.begin(), dp);
        uint32_t r = 0xffffffffu;
        CHECK(!cpu.call(cpu.read32(cpu.read32(dp) + 4 * uint32_t(index)), args, r));
        return r;
    }
    // A DPSESSIONDESC2 for the application "{11111111-...}".
    uint32_t sessionDesc(const std::string &name) {
        const uint32_t d = mem(0x50);
        for (uint32_t i = 0; i < 0x50; i += 4)
            cpu.write32(d + i, 0);
        cpu.write32(d, 0x50);
        x86::ComHost::writeGuid(cpu, d + 0x18, "{11111111-2222-3333-4444-555555555555}");
        cpu.write32(d + 0x28, 4);
        cpu.write32(d + 0x30, text(name));
        return d;
    }
    // Lists the connections and initializes the first: how many there were.
    int connect() {
        int listed = 0;
        uint32_t copy = mem(256);
        const uint32_t callback = cpu.addNative("test!EnumConnections", [&](x86::Cpu &c) {
            if (!listed++)
                for (uint32_t i = 0; i < std::min(c.arg(2), 256u); ++i)
                    c.write8(copy + i, c.read8(c.arg(1) + i));
            c.reg[x86::Cpu::EAX] = 1;
            return 24u;
        });
        CHECK(call(35, {0, callback, 0, 0}) == 0); // EnumConnections
        if (listed)
            CHECK(call(38, {copy, 0}) == 0); // InitializeConnection
        return listed;
    }
    uint32_t createPlayer(const std::string &name) {
        const uint32_t dpName = mem(16);
        cpu.write32(dpName, 16);
        cpu.write32(dpName + 4, 0);
        cpu.write32(dpName + 8, text(name));
        cpu.write32(dpName + 12, 0);
        const uint32_t id = mem(4);
        CHECK(call(6, {id, dpName, 0, 0, 0, 0}) == 0); // CreatePlayer
        return cpu.read32(id);
    }
    // Receive: the message's bytes, its sender and receiver; false if there's none.
    bool receive(std::vector<uint8_t> &data, uint32_t &from, uint32_t &to) {
        const uint32_t fromAt = mem(4);
        const uint32_t toAt = mem(4);
        const uint32_t buffer = mem(512);
        const uint32_t size = mem(4);
        cpu.write32(size, 512);
        const uint32_t r = call(25, {fromAt, toAt, 1, buffer, size});
        if (r != 0)
            return false;
        from = cpu.read32(fromAt);
        to = cpu.read32(toAt);
        data.resize(cpu.read32(size));
        for (size_t i = 0; i < data.size(); ++i)
            data[i] = cpu.read8(buffer + uint32_t(i));
        return true;
    }
    uint32_t send(uint32_t from, uint32_t to, const std::string &bytes, uint32_t flags = 0) {
        return call(26, {from, to, flags, text(bytes), uint32_t(bytes.size())});
    }
};

// DirectPlay without a network offers no connection; with the loopback
// network, one process hosts a session, another finds it, joins and makes
// a player; messages go both ways, to a player, to everyone and to a
// group; the session's description follows the host's; and the host sees
// the player come and go, as system messages.
static void testDirectPlay() {
    {
        x86::Cpu cpu(16u << 20);
        x86::Win32Process process(cpu);
        x86::DirectPlay directPlay(process);
        uint32_t r = 0;
        const uint32_t out = process.memory().alloc(4);
        const uint32_t clsid = process.memory().alloc(16);
        const uint32_t iid = process.memory().alloc(16);
        x86::ComHost::writeGuid(cpu, clsid, "{d1eb6d20-8923-11d0-9d97-00a0c90a43cb}");
        x86::ComHost::writeGuid(cpu, iid, "{133efe40-32dc-11d0-9cfb-00a0c90a43cb}");
        CHECK(!cpu.call(process.provided("ole32.dll", "CoCreateInstance"), {clsid, 0, 1, iid, out}, r) && r == 0);
        const uint32_t dp = cpu.read32(out);
        int offered = 0;
        const uint32_t callback = cpu.addNative("test!EnumConnections", [&](x86::Cpu &c) {
            ++offered;
            c.reg[x86::Cpu::EAX] = 1;
            return 24u;
        });
        CHECK(!cpu.call(cpu.read32(cpu.read32(dp) + 4 * 35), {dp, 0, callback, 0, 0}, r) && r == 0 && offered == 0);
        CHECK(!cpu.call(cpu.read32(cpu.read32(dp) + 4 * 2), {dp}, r) && r == 0); // Release
    }

    auto network = net::LoopbackNetwork::create();
    PlaySide host(network);
    PlaySide joiner(network);
    joiner.directPlay.waiting = [&] { host.directPlay.update(); };
    CHECK(host.connect() == 1 && joiner.connect() == 1);
    CHECK(host.call(24, {host.sessionDesc("Bespin"), 2}) == 0); // Open: create
    const uint32_t hostPlayer = host.createPlayer("Kyle");
    CHECK(hostPlayer != 0);

    // EnumSessions, asynchronously: asked, then answered.
    std::string found;
    uint32_t foundAt = joiner.mem(0x50);
    const uint32_t sessions = joiner.cpu.addNative("test!EnumSessions", [&](x86::Cpu &c) {
        if (c.arg(0) && !(c.arg(2) & 1)) {
            found = c.string(c.read32(c.arg(0) + 0x30));
            for (uint32_t i = 0; i < 0x50; ++i)
                c.write8(foundAt + i, c.read8(c.arg(0) + i));
            CHECK(c.read32(c.arg(0) + 0x2c) == 1); // its players
        }
        c.reg[x86::Cpu::EAX] = 0;
        return 16u;
    });
    const uint32_t wanted = joiner.sessionDesc("");
    CHECK(joiner.call(13, {wanted, 0, sessions, 0, 0x52}) == 0);
    host.directPlay.update();
    CHECK(joiner.call(13, {wanted, 0, sessions, 0, 0x52}) == 0);
    CHECK(found == "Bespin");

    // Its players, before joining.
    std::vector<std::string> names;
    const uint32_t players = joiner.cpu.addNative("test!EnumPlayers", [&](x86::Cpu &c) {
        names.push_back(c.string(c.read32(c.arg(2) + 8)));
        c.reg[x86::Cpu::EAX] = 1;
        return 20u;
    });
    CHECK(joiner.call(12, {foundAt + 8, players, 0, 0x80}) == 0 && names == std::vector<std::string>{"Kyle"});

    CHECK(joiner.call(24, {foundAt, 1}) == 0); // Open: join
    names.clear();
    CHECK(joiner.call(12, {0, players, 0, 0}) == 0 && names == std::vector<std::string>{"Kyle"});
    const uint32_t joinerPlayer = joiner.createPlayer("Jerec");
    CHECK(joinerPlayer != 0 && joinerPlayer != hostPlayer);
    host.directPlay.update();

    std::vector<uint8_t> data;
    uint32_t from = 0;
    uint32_t to = 0;
    CHECK(host.receive(data, from, to) && from == 0 && data.size() >= 48);
    CHECK(data[0] == 3 && data[4] == 1); // DPSYS_CREATEPLAYERORGROUP, a player
    CHECK(uint32_t(data[8] | data[9] << 8 | data[10] << 16 | data[11] << 24) == joinerPlayer);

    CHECK(joiner.send(joinerPlayer, hostPlayer, "hello") == 0);
    host.directPlay.update();
    CHECK(host.receive(data, from, to) && from == joinerPlayer && to == hostPlayer &&
          std::string(data.begin(), data.end()) == "hello");
    CHECK(host.send(hostPlayer, 0, "everyone", 1) == 0); // guaranteed
    joiner.directPlay.update();
    CHECK(joiner.receive(data, from, to) && from == hostPlayer && to == joinerPlayer &&
          std::string(data.begin(), data.end()) == "everyone");
    CHECK(!host.receive(data, from, to)); // not back to the sender

    // A group of the host's with the joiner's player in it, and a message to it.
    const uint32_t groupAt = host.mem(4);
    CHECK(host.call(5, {groupAt, 0, 0, 0, 0}) == 0); // CreateGroup
    const uint32_t group = host.cpu.read32(groupAt);
    CHECK(host.call(3, {group, joinerPlayer}) == 0); // AddPlayerToGroup
    CHECK(host.send(hostPlayer, group, "team") == 0);
    joiner.directPlay.update();
    CHECK(joiner.receive(data, from, to) && from == 0 && data[0] == 3 && data[4] == 0); // the group made
    CHECK(joiner.receive(data, from, to) && from == 0 && data[0] == 7);                 // DPSYS_ADDPLAYERTOGROUP
    CHECK(joiner.receive(data, from, to) && to == joinerPlayer && std::string(data.begin(), data.end()) == "team");

    // The host renames the session; the joiner's description follows.
    CHECK(host.call(31, {host.sessionDesc("Bespin 2"), 0}) == 0); // SetSessionDesc
    joiner.directPlay.update();
    const uint32_t descAt = joiner.mem(0x100);
    const uint32_t descSize = joiner.mem(4);
    joiner.cpu.write32(descSize, 0x100);
    CHECK(joiner.call(22, {descAt, descSize}) == 0); // GetSessionDesc
    CHECK(joiner.cpu.string(joiner.cpu.read32(descAt + 0x30)) == "Bespin 2");
    CHECK(joiner.cpu.read32(descAt + 0x2c) == 2);
    CHECK(joiner.call(31, {joiner.sessionDesc("mine"), 0}) != 0); // only the host may

    CHECK(joiner.call(4, {}) == 0); // Close
    host.directPlay.update();
    CHECK(host.receive(data, from, to) && from == 0 && data[0] == 5); // DPSYS_DESTROYPLAYERORGROUP
    CHECK(!host.receive(data, from, to));
}

// Dialog boxes from templates in memory: their controls, the procedure's
// WM_INITDIALOG and WM_COMMAND, a modal one answered by its default
// button or by the engine, a modeless one through IsDialogMessageA.
static void testDialogs() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    uint32_t r = 0;
    auto user = [&](const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided("USER32.dll", name), args, r));
        return r;
    };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    // A DLGTEMPLATE, or a DLGTEMPLATEEX, of words.
    auto build = [&](bool extended) {
        std::vector<uint16_t> w;
        auto dword = [&](uint32_t v) {
            w.push_back(uint16_t(v));
            w.push_back(uint16_t(v >> 16));
        };
        auto string = [&](const std::string &s) {
            for (char ch : s)
                w.push_back(uint8_t(ch));
            w.push_back(0);
        };
        auto align = [&] {
            if (w.size() % 2)
                w.push_back(0);
        };
        struct Item {
            uint32_t style;
            uint16_t id;
            uint16_t atom;
            const char *text;
        };
        const Item items[] = {{0x50000000, 100, 0x82, "Pick one"}, {0x50010000, 101, 0x81, "5"},
                              {0x50010003, 102, 0x80, "Again"},    {0x50010000, 103, 0x83, ""},
                              {0x50010001, 1, 0x80, "OK"},         {0x50010000, 2, 0x80, "Cancel"}};
        if (extended) {
            w = {1, 0xffff};
            dword(0);
            dword(0);
            dword(0x80c80000);
        } else {
            dword(0x80c80000); // WS_POPUP, WS_CAPTION, WS_SYSMENU
            dword(0);
        }
        w.push_back(uint16_t(std::size(items)));
        for (uint16_t v : {10, 10, 200, 100})
            w.push_back(v);
        w.push_back(0); // no menu
        w.push_back(0); // the dialogs' class
        string("Choose");
        for (const Item &it : items) {
            align();
            if (extended) {
                dword(0);
                dword(0);
                dword(it.style);
            } else {
                dword(it.style);
                dword(0);
            }
            for (uint16_t v : {5, 5, 50, 14})
                w.push_back(v);
            if (extended)
                dword(it.id);
            else
                w.push_back(it.id);
            w.push_back(0xffff);
            w.push_back(it.atom);
            string(it.text);
            w.push_back(0); // no creation data
        }
        const uint32_t a = process.memory().alloc(uint32_t(w.size() * 2));
        for (size_t k = 0; k < w.size(); ++k)
            cpu.write16(a + 2 * uint32_t(k), w[k]);
        return a;
    };
    // The procedure: fills the controls when made; OK ends with the edit's
    // number, Cancel with 0.
    uint32_t initParam = 0;
    const uint32_t proc = cpu.addNative("test!DlgProc", [&](x86::Cpu &c) {
        const uint32_t dlg = c.arg(0);
        uint32_t v = 0;
        c.reg[x86::Cpu::EAX] = 0;
        if (c.arg(1) == 0x110) { // WM_INITDIALOG
            initParam = c.arg(3);
            CHECK(!c.call(process.provided("USER32.dll", "SetDlgItemTextA"), {dlg, 101, text("42")}, v) && v);
            CHECK(!c.call(process.provided("USER32.dll", "CheckDlgButton"), {dlg, 102, 1}, v));
            for (const char *s : {"red", "blue"})
                CHECK(!c.call(process.provided("USER32.dll", "SendDlgItemMessageA"), {dlg, 103, 0x180, 0, text(s)}, v));
            CHECK(!c.call(process.provided("USER32.dll", "SendDlgItemMessageA"), {dlg, 103, 0x186, 1, 0}, v));
            c.reg[x86::Cpu::EAX] = 1;
        } else if (c.arg(1) == 0x111) { // WM_COMMAND
            const uint32_t id = c.arg(2) & 0xffff;
            if (id == 1 || id == 2) {
                uint32_t n = 0;
                if (id == 1)
                    CHECK(!c.call(process.provided("USER32.dll", "GetDlgItemInt"), {dlg, 101, 0, 0}, n));
                CHECK(!c.call(process.provided("USER32.dll", "EndDialog"), {dlg, n}, v) && v);
                c.reg[x86::Cpu::EAX] = 1;
            }
        }
        return 16u;
    });

    // Modal, answered by its default button: OK, the edit's 42.
    std::vector<x86::Win32Process::DialogShown> shown;
    for (const bool extended : {false, true}) {
        process.dialogBox = [&](const x86::Win32Process::DialogShown &d) {
            shown.push_back(d);
            return 0u;
        };
        CHECK(user("DialogBoxIndirectParamA", {0, build(extended), 0, proc, 77}) == 42);
        CHECK(initParam == 77);
    }
    CHECK(shown.size() == 2 && shown[0].modal && shown[0].caption == "Choose" && shown[0].defaultButton == 1);
    CHECK(shown.size() == 2 && shown[1].controls.size() == 6 && shown[1].controls[1].text == "42" &&
          shown[1].controls[3].className == "ListBox");
    // The engine pressing Cancel.
    process.dialogBox = [](const x86::Win32Process::DialogShown &) { return 2u; };
    CHECK(user("DialogBoxIndirectParamA", {0, build(false), 0, proc, 0}) == 0);

    // Modeless: its controls read back; Enter, through IsDialogMessageA,
    // presses OK.
    const uint32_t dlg = user("CreateDialogIndirectParamA", {0, build(false), 0, proc, 5});
    CHECK(dlg != 0);
    const uint32_t list = user("GetDlgItem", {dlg, 103});
    CHECK(list != 0 && user("GetDlgCtrlID", {list}) == 103);
    CHECK(user("SendMessageA", {list, 0x18b, 0, 0}) == 2 && user("SendMessageA", {list, 0x188, 0, 0}) == 1);
    const uint32_t buffer = process.memory().alloc(16);
    CHECK(user("SendMessageA", {list, 0x189, 1, buffer}) == 4 && cpu.string(buffer) == "blue");
    CHECK(user("IsDlgButtonChecked", {dlg, 102}) == 1);
    CHECK(user("GetDlgItemTextA", {dlg, 101, buffer, 16}) == 2 && cpu.string(buffer) == "42");
    CHECK(user("GetDlgItem", {dlg, 999}) == 0);
    const uint32_t msg = process.memory().alloc(28);
    cpu.write32(msg, list);
    cpu.write32(msg + 4, 0x100); // WM_KEYDOWN
    cpu.write32(msg + 8, 0x0d);  // VK_RETURN
    CHECK(user("IsDialogMessageA", {dlg, msg}) == 1);
    CHECK(process.window(dlg) && !process.window(dlg)->visible); // ended: hidden
    CHECK(user("IsDialogMessageA", {0x1234, msg}) == 0);
    CHECK(user("DestroyWindow", {dlg}) == 1 && process.window(list) == nullptr);

    // No such resource.
    CHECK(user("DialogBoxParamA", {0, 7, 0, proc, 0}) == 0xffffffffu);
}

// WINMM's joysticks: none plugged in, then a game controller as the first,
// its caps, axes, buttons and hat as JK.EXE reads them, and a second; the
// Unicode caps, the registry's settings, thresholds and capture.
static void testJoysticks() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    x86::Joysticks joysticks(process);
    uint32_t r = 0;
    auto call = [&](const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided("WINMM.dll", name), args, r));
        return r;
    };
    const uint32_t info = process.memory().alloc(52);
    const uint32_t caps = process.memory().alloc(404);
    CHECK(call("joyGetNumDevs", {}) == 16);
    CHECK(call("joyGetPos", {0, info}) == 167);  // JOYERR_UNPLUGGED
    CHECK(call("joyGetPos", {16, info}) == 165); // JOYERR_PARMS
    CHECK(call("joyGetDevCapsA", {0, caps, 404}) == 167);

    GamepadState pad;
    pad.name = "Test pad";
    const x86::Joysticks::Device device = x86::Joysticks::fromGamepad(pad);
    joysticks.connect(0, &device);
    CHECK(call("joyGetDevCapsA", {0, caps, 404}) == 0);
    CHECK(cpu.string(caps + 4) == "Test pad" && cpu.read32(caps + 40) == 65535 && cpu.read32(caps + 76) == 65535);
    CHECK(cpu.read32(caps + 96) == (0x1 | 0x2 | 0x4 | 0x10 | 0x40)); // Z, R, U and a hat by degrees
    CHECK(cpu.read32(caps + 104) == 5 && cpu.read32(caps + 108) == 10);
    CHECK(call("joyGetDevCapsA", {1, caps, 404}) == 167);

    // The left stick right and up, the right trigger, B held, the D-pad
    // down and left.
    pad.axes[0] = 1;
    pad.axes[1] = -1;
    pad.axes[5] = 1;
    pad.buttons = 1u << kPadEast | 1u << kPadDown | 1u << kPadLeft;
    joysticks.set(0, x86::Joysticks::stateOf(pad));
    cpu.write32(info, 52);
    CHECK(call("joyGetPosEx", {0, info}) == 0);
    CHECK(cpu.read32(info + 8) == 65535 && cpu.read32(info + 12) == 0 && cpu.read32(info + 16) == 65535);
    CHECK(cpu.read32(info + 20) == x86::Joysticks::kCentre);
    CHECK(cpu.read32(info + 32) == 2 && cpu.read32(info + 36) == 1);
    CHECK(cpu.read32(info + 40) == 22500); // down and left: 225 degrees clockwise from ahead
    CHECK(call("joyGetPos", {0, info}) == 0 && cpu.read32(info) == 65535 && cpu.read32(info + 12) == 2);
    cpu.write32(info, 20);
    CHECK(call("joyGetPosEx", {0, info}) == 165); // too small a structure

    // A second, on its own: the first's state isn't its.
    joysticks.connect(1, &device);
    GamepadState second;
    second.axes[0] = -1;
    joysticks.set(1, x86::Joysticks::stateOf(second));
    cpu.write32(info, 52);
    CHECK(call("joyGetPosEx", {1, info}) == 0 && cpu.read32(info + 8) == 0 && cpu.read32(info + 32) == 0);
    CHECK(call("joyGetPosEx", {0, info}) == 0 && cpu.read32(info + 8) == 65535);
    CHECK(call("joyGetPos", {2, info}) == 167); // a third isn't offered

    // The Unicode caps: the name in 16-bit characters, the numbers 32 bytes on.
    const uint32_t wcaps = process.memory().alloc(728);
    CHECK(call("joyGetDevCapsW", {0, wcaps, 404}) == 165);
    CHECK(call("joyGetDevCapsW", {0, wcaps, 728}) == 0);
    CHECK(cpu.read16(wcaps + 4) == 'T' && cpu.read16(wcaps + 6) == 'e' && cpu.read32(wcaps + 72) == 65535);
    CHECK(cpu.read32(wcaps + 140) == 10 && cpu.read16(wcaps + 144) == 'R'); // wMaxButtons, szRegKey

    // Its settings in the registry once update() has run, as Windows 95 keeps them.
    joysticks.update(0);
    const std::string current = "HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\"
                                "Reluva\\CurrentJoystickSettings";
    const auto oemName = process.registryValue(current, "Joystick1OEMName");
    CHECK(oemName && std::string(oemName->begin(), oemName->end()) == std::string("Test pad", 9));
    uint32_t type = 0;
    const auto config = process.registryValue(current, "Joystick1Configuration", &type);
    CHECK(config && type == 3 && config->size() == 112 && (*config)[36] == 0xff && (*config)[37] == 0xff);
    CHECK(process.registryValue("HKEY_LOCAL_MACHINE\\System\\CurrentControlSet\\Control\\MediaProperties\\"
                                "PrivateProperties\\Joystick\\OEM\\Test pad",
                                "OEMName"));

    // Thresholds.
    const uint32_t value = process.memory().alloc(4);
    CHECK(call("joySetThreshold", {0, 1000}) == 0);
    CHECK(call("joyGetThreshold", {0, value}) == 0 && cpu.read32(value) == 1000);
    CHECK(call("joySetThreshold", {5, 1000}) == 165);

    // Captured by a window: its messages each period, the moves only past
    // the threshold.
    const uint32_t proc = cpu.addNative("test!JoyProc", [](x86::Cpu &c) {
        c.reg[x86::Cpu::EAX] = c.arg(1) == 0x81 ? 1 : 0;
        return 16u;
    });
    const uint32_t wc = process.memory().alloc(40);
    const uint32_t name = process.memory().alloc(8);
    cpu.writeString(name, "Joy", 8);
    cpu.write32(wc + 4, proc);
    cpu.write32(wc + 36, name);
    CHECK(!cpu.call(process.provided("USER32.dll", "RegisterClassA"), {wc}, r) && r != 0);
    CHECK(
        !cpu.call(process.provided("USER32.dll", "CreateWindowExA"), {0, name, name, 0, 0, 0, 64, 64, 0, 0, 0, 0}, r) &&
        r != 0);
    const uint32_t hwnd = r;
    const uint32_t msg = process.memory().alloc(28);
    auto messages = [&] {
        std::vector<std::array<uint32_t, 3>> got;
        while (!cpu.call(process.provided("USER32.dll", "PeekMessageA"), {msg, 0, 0, 0, 1}, r) && r)
            if (cpu.read32(msg + 4) >= 0x3a0 && cpu.read32(msg + 4) <= 0x3b8)
                got.push_back({cpu.read32(msg + 4), cpu.read32(msg + 8), cpu.read32(msg + 12)});
        return got;
    };
    messages();
    joysticks.set(0, x86::Joysticks::State());
    CHECK(call("joySetCapture", {0x1234, 0, 50, 1}) == 165); // no such window
    CHECK(call("joySetCapture", {hwnd, 0, 50, 1}) == 0);
    CHECK(call("joySetCapture", {hwnd, 0, 50, 1}) == 166); // captured already
    GamepadState now;
    now.buttons = 1u << kPadSouth;
    now.axes[0] = 0.01f; // less than the threshold
    joysticks.set(0, x86::Joysticks::stateOf(now));
    joysticks.update(100);
    auto got = messages();
    CHECK(got.size() == 1 && got[0][0] == 0x3b5 && got[0][1] == (0x1 | 0x100)); // button 1 down, and changed
    joysticks.update(120);                                                      // within the period: nothing
    now.axes[0] = 1;
    joysticks.set(0, x86::Joysticks::stateOf(now));
    joysticks.update(130);
    CHECK(messages().empty());
    joysticks.update(150);
    got = messages();
    CHECK(got.size() == 1 && got[0][0] == 0x3a0 && got[0][1] == 1 && (got[0][2] & 0xffff) == 65535);
    CHECK(call("joyReleaseCapture", {0}) == 0);
    now.buttons = 0;
    joysticks.set(0, x86::Joysticks::stateOf(now));
    joysticks.update(300);
    CHECK(messages().empty());

    joysticks.connect(0, nullptr);
    CHECK(call("joyGetPos", {0, info}) == 167);
    CHECK(call("joyGetPos", {1, info}) == 0);
}

// GDI's bitmaps as a program uses them: an 8-bit DIB section blitted, mirrored
// and stretched into a 32-bit one, read back with GetDIBits; palettes.
static void testGdi() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    uint32_t r = 0;
    auto call = [&](const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided("GDI32.dll", name), args, r));
        return r;
    };
    const uint32_t srccopy = 0x00cc0020;
    // A 4 by 2 bottom-up DIB of 8 bits: black, red, green and blue.
    const uint32_t info = process.memory().alloc(40 + 256 * 4);
    cpu.write32(info, 40);
    cpu.write32(info + 4, 4);
    cpu.write32(info + 8, 2);
    cpu.write16(info + 12, 1);
    cpu.write16(info + 14, 8);
    cpu.write32(info + 32, 4);
    const uint32_t quads[4] = {0, 0x00ff0000, 0x0000ff00, 0x000000ff}; // RGBQUADs: blue, green, red
    for (uint32_t i = 0; i < 4; ++i)
        cpu.write32(info + 40 + 4 * i, quads[i]);
    const uint32_t bitsOut = process.memory().alloc(4);
    const uint32_t dc8 = call("CreateCompatibleDC", {0});
    const uint32_t dib8 = call("CreateDIBSection", {dc8, info, 0, bitsOut, 0, 0});
    CHECK(dib8 != 0 && cpu.read32(bitsOut) != 0);
    const uint32_t pixels8 = cpu.read32(bitsOut);
    cpu.write32(pixels8, 0x00000000);     // the bottom row
    cpu.write32(pixels8 + 4, 0x00030201); // the top row: red, green, blue, black
    const uint32_t first = call("SelectObject", {dc8, dib8});
    CHECK(first != 0 && first != dib8);
    // A 4 by 2 top-down one of 32 bits.
    const uint32_t info32 = process.memory().alloc(40);
    cpu.write32(info32, 40);
    cpu.write32(info32 + 4, 4);
    cpu.write32(info32 + 8, uint32_t(-2));
    cpu.write16(info32 + 12, 1);
    cpu.write16(info32 + 14, 32);
    const uint32_t dc32 = call("CreateCompatibleDC", {dc8});
    const uint32_t dib32 = call("CreateDIBSection", {dc32, info32, 0, bitsOut, 0, 0});
    const uint32_t pixels32 = cpu.read32(bitsOut);
    CHECK(dib32 != 0 && call("SelectObject", {dc32, dib32}) != 0);
    auto row32 = [&](int y) {
        std::vector<uint32_t> row;
        for (uint32_t x = 0; x < 4; ++x)
            row.push_back(cpu.read32(pixels32 + uint32_t(y) * 16 + 4 * x));
        return row;
    };
    CHECK(call("BitBlt", {dc32, 0, 0, 4, 2, dc8, 0, 0, srccopy}) == 1);
    CHECK((row32(0) == std::vector<uint32_t>{0xff0000, 0xff00, 0xff, 0}) &&
          (row32(1) == std::vector<uint32_t>{0, 0, 0, 0}));
    // Mirrored by a negative source width.
    CHECK(call("StretchBlt", {dc32, 0, 0, 4, 2, dc8, 4, 0, uint32_t(-4), 2, srccopy}) == 1);
    CHECK((row32(0) == std::vector<uint32_t>{0, 0xff, 0xff00, 0xff0000}));
    // A colour of the DIB section's table changed.
    const uint32_t yellow = process.memory().alloc(4);
    cpu.write32(yellow, 0x00ffff00);
    CHECK(call("SetDIBColorTable", {dc8, 1, 1, yellow}) == 1);
    CHECK(call("BitBlt", {dc32, 0, 0, 1, 1, dc8, 0, 0, srccopy}) == 1 && cpu.read32(pixels32) == 0xffff00);
    // Raster operations without a pattern; one with fails.
    CHECK(call("BitBlt", {dc32, 0, 1, 4, 1, 0, 0, 0, 0x00ff0062}) == 1 && row32(1)[3] == 0xffffff); // WHITENESS
    CHECK(call("BitBlt", {dc32, 0, 1, 4, 1, dc8, 0, 0, 0x008800c6}) == 1 &&                         // SRCAND
          (row32(1) == std::vector<uint32_t>{0xffff00, 0xff00, 0xff, 0}));
    CHECK(call("BitBlt", {dc32, 0, 0, 4, 2, 0, 0, 0, 0x00f00021}) == 0); // PATCOPY
    // A 2 by 1 DIB of 24 bits, white and blue, stretched over the whole.
    const uint32_t info24 = process.memory().alloc(40);
    cpu.write32(info24, 40);
    cpu.write32(info24 + 4, 2);
    cpu.write32(info24 + 8, 1);
    cpu.write16(info24 + 12, 1);
    cpu.write16(info24 + 14, 24);
    const uint32_t pixels24 = process.memory().alloc(8);
    for (uint32_t i = 0; i < 3; ++i)
        cpu.write8(pixels24 + i, 0xff);
    cpu.write8(pixels24 + 3, 0xff); // blue, green, red: blue
    CHECK(call("StretchDIBits", {dc32, 0, 0, 4, 2, 0, 0, 2, 1, pixels24, info24, 0, srccopy}) == 1);
    CHECK((row32(0) == std::vector<uint32_t>{0xffffff, 0xffffff, 0xff, 0xff}) && row32(1) == row32(0));
    CHECK(call("SetStretchBltMode", {dc32, 3}) == 1 && call("SetStretchBltMode", {dc32, 4}) == 3);
    CHECK(call("SetStretchBltMode", {dc32, 9}) == 0);
    // GetDIBits: the format, then the rows as 24 bits, bottom up.
    const uint32_t asked = process.memory().alloc(52);
    cpu.write32(asked, 40);
    CHECK(call("GetDIBits", {dc32, dib32, 0, 2, 0, asked, 0}) == 1);
    CHECK(cpu.read32(asked + 4) == 4 && cpu.read32(asked + 8) == 2 && cpu.read16(asked + 14) == 32 &&
          cpu.read32(asked + 16) == 0);
    cpu.write16(asked + 14, 24);
    const uint32_t lines = process.memory().alloc(24);
    cpu.write32(pixels32 + 16, 0x00123456); // the bottom row's first
    CHECK(call("GetDIBits", {dc32, dib32, 0, 2, lines, asked, 0}) == 2);
    CHECK(cpu.read8(lines) == 0x56 && cpu.read8(lines + 1) == 0x34 && cpu.read8(lines + 2) == 0x12 &&
          cpu.read8(lines + 12) == 0xff && cpu.read8(lines + 15) == 0xff);
    // A compatible bitmap of the context's depth.
    const uint32_t compatible = call("CreateCompatibleBitmap", {dc32, 3, 3});
    cpu.write32(asked + 14, 0);
    CHECK(compatible != 0 && call("GetDIBits", {dc32, compatible, 0, 3, 0, asked, 0}) == 1 &&
          cpu.read16(asked + 14) == 32 && cpu.read32(asked + 4) == 3);

    // Palettes: black, a PC_RESERVED white, red.
    const uint32_t logPalette = process.memory().alloc(16);
    cpu.write16(logPalette, 0x300);
    cpu.write16(logPalette + 2, 3);
    cpu.write32(logPalette + 4, 0);
    cpu.write32(logPalette + 8, 0x01ffffff);
    cpu.write32(logPalette + 12, 0x000000ff);
    const uint32_t palette = call("CreatePalette", {logPalette});
    CHECK(palette != 0 && call("GetNearestPaletteIndex", {palette, 0x000010e0}) == 2);
    CHECK(call("GetNearestPaletteIndex", {palette + 1, 0}) == 0xffffffffu);
    const uint32_t greens = process.memory().alloc(12);
    for (uint32_t i = 0; i < 3; ++i)
        cpu.write32(greens + 4 * i, 0x0000ff00);
    CHECK(call("AnimatePalette", {palette, 0, 3, greens}) == 1);
    CHECK(call("GetNearestPaletteIndex", {palette, 0x0000ff00}) == 1 &&
          call("GetNearestPaletteIndex", {palette, 0x000000ff}) == 2);
    const uint32_t stock = call("SelectPalette", {dc8, palette, 0});
    CHECK(stock != 0 && call("SelectPalette", {dc8, stock, 0}) == palette);
    CHECK(call("SelectPalette", {dc8, palette, 0}) == stock);
    CHECK(call("RealizePalette", {dc8}) == 0 && call("GetSystemPaletteUse", {dc8}) == 0);
    process.screenBits = 8;
    CHECK(call("RealizePalette", {dc8}) == 3 && call("GetSystemPaletteUse", {dc8}) == 1);

    CHECK(call("SelectObject", {dc8, first}) == dib8);
    CHECK(call("DeleteObject", {dib8}) == 1 && call("DeleteObject", {palette}) == 1);
    CHECK(call("DeleteDC", {dc8}) == 1 && call("DeleteDC", {dc8}) == 0);
}

// KERNEL32's, USER32's and ADVAPI32's odds and ends: string comparison,
// LocalAlloc, wsprintfA, a dialog that isn't there, programs that can't be had,
// RegDeleteKeyA and DebugBreak.
static void testOddsAndEnds() {
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    uint32_t r = 0;
    auto call = [&](const char *dll, const char *name, const std::vector<uint32_t> &args) {
        CHECK(!cpu.call(process.provided(dll, name), args, r));
        return r;
    };
    auto text = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(s.size() + 1));
        cpu.writeString(a, s, uint32_t(s.size() + 1));
        return a;
    };
    auto wide = [&](const std::string &s) {
        const uint32_t a = process.memory().alloc(uint32_t(2 * s.size() + 2));
        for (size_t i = 0; i <= s.size(); ++i)
            cpu.write16(a + 2 * uint32_t(i), i < s.size() ? uint8_t(s[i]) : 0);
        return a;
    };
    const char *k = "KERNEL32.dll";
    const char *u = "USER32.dll";
    const uint32_t minus = uint32_t(-1);
    CHECK(call(k, "CompareStringA", {0x409, 0, text("abc"), minus, text("abd"), minus}) == 1);
    CHECK(call(k, "CompareStringA", {0x409, 0, text("abc"), minus, text("ABC"), minus}) == 3);
    CHECK(call(k, "CompareStringA", {0x409, 1, text("abc\xe9"), minus, text("ABC\xc9"), minus}) == 2);
    CHECK(call(k, "CompareStringA", {0x409, 0, text("abcX"), 3, text("abc"), minus}) == 2);
    CHECK(call(k, "CompareStringA", {0x409, 0, text("abc"), minus, text("ab"), minus}) == 3);
    CHECK(call(k, "CompareStringA", {0x409, 0, 0, minus, text("ab"), minus}) == 0);
    CHECK(call(k, "CompareStringW", {0x409, 1, wide("Jedi"), minus, wide("JEDI"), 4}) == 2);
    CHECK(call(k, "CompareStringW", {0x409, 0, wide("Jedi"), 2, wide("Jedi"), minus}) == 1);

    const uint32_t block = call(k, "LocalAlloc", {0x40, 64});
    CHECK(block != 0 && cpu.read32(block) == 0 && cpu.read32(block + 60) == 0);
    CHECK(call(k, "LocalFree", {block}) == 0);

    const uint32_t buffer = process.memory().alloc(2048);
    CHECK(call(u, "wsprintfA", {buffer, text("%s=%d %04x %c"), text("n"), uint32_t(-5), 0xab, 'z'}) == 11 &&
          cpu.string(buffer) == "n=-5 00ab z");
    CHECK(call(u, "wsprintfA", {buffer, text("%s"), text(std::string(1500, 'x'))}) == 1023 &&
          cpu.string(buffer, 4096).size() == 1023);

    // What can't be had: other programs, and a dialog not in the resources.
    std::string said;
    process.output = [&](const std::string &s) { said += s; };
    CHECK(call(k, "WinExec", {text("control joy.cpl"), 1}) == 2);
    CHECK(said.find("control joy.cpl") != std::string::npos);
    CHECK(call(k, "LoadModule", {text("SETUP.EXE"), 0}) == 2);
    CHECK(call(u, "CreateDialogParamA", {0x400000, 101, 0, 0, 0}) == 0);
    CHECK(call(u, "GetDlgItem", {0, 101}) == 0 && call(u, "SetDlgItemTextA", {0, 101, text("x")}) == 0);
    cpu.write8(buffer, 'x');
    CHECK(call(u, "GetDlgItemTextA", {0, 101, buffer, 16}) == 0 && cpu.read8(buffer) == 0);
    CHECK(call(u, "IsDialogMessageA", {0, buffer}) == 0);

    // A key with keys under it isn't deleted.
    const uint32_t key = process.memory().alloc(4);
    const char *a = "ADVAPI32.dll";
    CHECK(call(a, "RegCreateKeyExA", {0x80000001u, text("Software\\Game\\Sub"), 0, 0, 0, 0, 0, key, 0}) == 0);
    CHECK(call(a, "RegDeleteKeyA", {0x80000001u, text("Software\\Game")}) == 5);
    CHECK(call(a, "RegDeleteKeyA", {0x80000001u, text("software\\game\\sub")}) == 0);
    CHECK(call(a, "RegDeleteKeyA", {0x80000001u, text("Software\\Game\\Sub")}) == 2);
    CHECK(call(a, "RegDeleteKeyA", {0x80000001u, text("Software\\Game")}) == 0);
    CHECK(call(a, "RegOpenKeyExA", {0x80000001u, text("Software\\Game"), 0, 1, key}) == 2);

    // DebugBreak ends the run.
    const Error e = cpu.call(process.provided(k, "DebugBreak"), {}, r);
    CHECK(e && e.message.find("DebugBreak") != std::string::npos);
}

// A3D created as a program creates it, a source made, filled through Lock,
// played once and looped, as the clock moves.
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
               {"direct3d 1", [](const std::filesystem::path &) { testDirect3D1(); }},
               {"directplay", [](const std::filesystem::path &) { testDirectPlay(); }},
               {"dialogs", [](const std::filesystem::path &) { testDialogs(); }},
               {"joysticks", [](const std::filesystem::path &) { testJoysticks(); }},
               {"gdi", [](const std::filesystem::path &) { testGdi(); }},
               {"odds and ends", [](const std::filesystem::path &) { testOddsAndEnds(); }},
               {"cd audio", [](const std::filesystem::path &tmp) { testCdAudio(tmp / "cdaudio"); }},
               {"directsound", [](const std::filesystem::path &) { testDirectSound(); }},
               {"sound 3d", [](const std::filesystem::path &) { testSound3d(); }},
               {"pixel conversion", [](const std::filesystem::path &) { testArgbConverter(); }},
           });
