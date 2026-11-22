// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's INI files, locale and string functions, global memory and time
// conversion, called as a program would, on a drive of the test's own.
//
//   reluva-tests common.unit.windows_kernel

#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace reluva;

namespace {

namespace stdfs = std::filesystem;

struct Fixture {
    stdfs::path root;
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    uint32_t mem = 0;

    explicit Fixture(const stdfs::path &tmp) : root(tmp) {
        stdfs::create_directories(root / "game" / "Exe");
        std::ofstream(root / "game" / "Exe" / "GAME.INI", std::ios::binary)
            << "[DIRECTORIES]\r\nRes=C:\\art-res\r\n  Name  =  \"quoted\"  \r\n\r\n[Locale]\r\nLanguage=ENGLISH\r\n"
               "Count=42x\r\n";
        x86::Win32Process::Drive c;
        c.root = root / "game";
        c.writable = root / "save";
        process.addDrive(c);
        mem = process.memory().alloc(4096);
    }
    uint32_t call(const char *dll, const char *name, const std::vector<uint32_t> &args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn(dll, name), args, r));
        return r;
    }
    uint32_t str(uint32_t offset, const std::string &s) {
        cpu.writeString(mem + offset, s, uint32_t(s.size() + 1));
        return mem + offset;
    }
    std::string saved(const char *name) const {
        std::ifstream in(root / "save" / "Exe" / name, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

void testIniRead(const stdfs::path &tmp) {
    Fixture f(tmp);
    const char *k = "KERNEL32.dll";
    const uint32_t file = f.str(0, "C:\\Exe\\GAME.INI");
    const uint32_t buf = f.mem + 2048;
    // A value, found ignoring case, its spaces and quotes taken off.
    CHECK(f.call(k, "GetPrivateProfileStringA", {f.str(100, "directories"), f.str(120, "RES"), 0, buf, 64, file}) ==
          10);
    CHECK(f.cpu.string(buf) == "C:\\art-res");
    CHECK(f.call(k, "GetPrivateProfileStringA", {f.str(100, "DIRECTORIES"), f.str(120, "name"), 0, buf, 64, file}) ==
          6);
    CHECK(f.cpu.string(buf) == "quoted");
    // The default, without its trailing spaces; cut to fit.
    CHECK(f.call(k, "GetPrivateProfileStringA",
                 {f.str(100, "Locale"), f.str(120, "None"), f.str(140, "dflt  "), buf, 64, file}) == 4);
    CHECK(f.cpu.string(buf) == "dflt");
    CHECK(f.call(k, "GetPrivateProfileStringA", {f.str(100, "Locale"), f.str(120, "Language"), 0, buf, 4, file}) == 3);
    CHECK(f.cpu.string(buf) == "ENG");
    // No key: the section's keys, each ending in a zero, and a zero after.
    CHECK(f.call(k, "GetPrivateProfileStringA", {f.str(100, "Locale"), 0, 0, buf, 64, file}) == 15);
    CHECK(f.cpu.string(buf) == "Language" && f.cpu.string(buf + 9) == "Count" && f.cpu.read8(buf + 15) == 0);
    // No section: the sections.
    CHECK(f.call(k, "GetPrivateProfileStringA", {0, 0, 0, buf, 64, file}) == 19);
    CHECK(f.cpu.string(buf) == "DIRECTORIES" && f.cpu.string(buf + 12) == "Locale");
    // A number: the digits the value starts with, or the default.
    CHECK(f.call(k, "GetPrivateProfileIntA", {f.str(100, "Locale"), f.str(120, "Count"), 7, file}) == 42);
    CHECK(f.call(k, "GetPrivateProfileIntA", {f.str(100, "Locale"), f.str(120, "Nothing"), 7, file}) == 7);
    // A file that isn't there gives the defaults.
    CHECK(f.call(k, "GetPrivateProfileIntA", {f.str(100, "Locale"), f.str(120, "Count"), 9, f.str(200, "NONE.INI")}) ==
          9);
}

void testIniWrite(const stdfs::path &tmp) {
    Fixture f(tmp);
    const char *k = "KERNEL32.dll";
    const uint32_t file = f.str(0, "C:\\Exe\\GAME.INI");
    // Set, added to its section, a new section: in the writable copy only.
    CHECK(f.call(k, "WritePrivateProfileStringA",
                 {f.str(100, "Locale"), f.str(120, "Language"), f.str(140, "FRENCH"), file}) == 1);
    CHECK(f.call(k, "WritePrivateProfileStringA",
                 {f.str(100, "DIRECTORIES"), f.str(120, "Exe"), f.str(140, "a.exe"), file}) == 1);
    CHECK(f.call(k, "WritePrivateProfileStringA", {f.str(100, "New"), f.str(120, "Key"), f.str(140, "1"), file}) == 1);
    CHECK(f.saved("GAME.INI") == "[DIRECTORIES]\r\nRes=C:\\art-res\r\n  Name  =  \"quoted\"  \r\nExe=a.exe\r\n\r\n"
                                 "[Locale]\r\nLanguage=FRENCH\r\nCount=42x\r\n\r\n[New]\r\nKey=1\r\n");
    std::ifstream original(f.root / "game" / "Exe" / "GAME.INI", std::ios::binary);
    CHECK(std::string((std::istreambuf_iterator<char>(original)), std::istreambuf_iterator<char>()).find("FRENCH") ==
          std::string::npos);
    // No text removes the key; no key the section.
    CHECK(f.call(k, "WritePrivateProfileStringA", {f.str(100, "Locale"), f.str(120, "Count"), 0, file}) == 1);
    CHECK(f.call(k, "WritePrivateProfileStringA", {f.str(100, "New"), 0, 0, file}) == 1);
    CHECK(
        f.saved("GAME.INI") ==
        "[DIRECTORIES]\r\nRes=C:\\art-res\r\n  Name  =  \"quoted\"  \r\nExe=a.exe\r\n\r\n[Locale]\r\nLanguage=FRENCH\r\n\r\n");
}

void testLocaleAndStrings(const stdfs::path &tmp) {
    Fixture f(tmp);
    const char *k = "KERNEL32.dll";
    const uint32_t buf = f.mem + 2048;
    CHECK(f.call(k, "GetSystemDefaultLCID", {}) == 0x409);
    // LOCALE_IDEFAULTANSICODEPAGE: with no buffer, the size it needs.
    CHECK(f.call(k, "GetLocaleInfoA", {0x409, 0x1004, buf, 0}) == 5);
    CHECK(f.call(k, "GetLocaleInfoA", {0x409, 0x1004, buf, 16}) == 5 && f.cpu.string(buf) == "1252");
    CHECK(f.call(k, "GetLocaleInfoA", {0x409, 0x1004, buf, 2}) == 0);
    CHECK(f.call(k, "GetLocaleInfoW", {0x409, 3, buf, 16}) == 4 && f.cpu.read16(buf) == 'E' &&
          f.cpu.read16(buf + 6) == 0);
    // CompareStringA: CSTR_LESS_THAN 1, CSTR_EQUAL 2, CSTR_GREATER_THAN 3; NORM_IGNORECASE.
    const uint32_t a = f.str(0, "Loco");
    const uint32_t b = f.str(16, "LOCO");
    CHECK(f.call(k, "CompareStringA", {0x409, 0, a, 0xffffffffu, b, 0xffffffffu}) == 3);
    CHECK(f.call(k, "CompareStringA", {0x409, 1, a, 0xffffffffu, b, 0xffffffffu}) == 2);
    CHECK(f.call(k, "CompareStringA", {0x409, 1, a, 2, b, 0xffffffffu}) == 1);
    CHECK(f.call(k, "lstrlenA", {a}) == 4);
    f.cpu.write32(buf, 5);
    CHECK(f.call(k, "InterlockedIncrement", {buf}) == 6 && f.call(k, "InterlockedDecrement", {buf}) == 5);
    // Global memory: the handle is the block, zeroed when asked.
    const uint32_t g = f.call(k, "GlobalAlloc", {0x42, 64});
    CHECK(g != 0 && f.call(k, "GlobalLock", {g}) == g && f.cpu.read32(g + 60) == 0);
    CHECK(f.call(k, "GlobalFree", {g}) == 0);
    // FILETIME to SYSTEMTIME: 2000-01-01 00:00:00.5 UTC, a Saturday.
    const uint64_t t = 125911584000000000ull + 5000000;
    f.cpu.write32(buf, uint32_t(t));
    f.cpu.write32(buf + 4, uint32_t(t >> 32));
    CHECK(f.call(k, "FileTimeToSystemTime", {buf, buf + 16}) == 1);
    CHECK(f.cpu.read16(buf + 16) == 2000 && f.cpu.read16(buf + 18) == 1 && f.cpu.read16(buf + 20) == 6 &&
          f.cpu.read16(buf + 22) == 1 && f.cpu.read16(buf + 24) == 0 && f.cpu.read16(buf + 30) == 500);
    // FormatMessageA allocating the buffer itself.
    CHECK(f.call(k, "FormatMessageA", {0x1100, 0, 2, 0, buf, 0, 0}) == 10);
    CHECK(f.cpu.string(f.cpu.read32(buf)) == "Error 2.\r\n");
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.windows_kernel",
           {
               {"ini read", [](const stdfs::path &tmp) { testIniRead(tmp); }},
               {"ini write", [](const stdfs::path &tmp) { testIniWrite(tmp); }},
               {"locale and strings", [](const stdfs::path &tmp) { testLocaleAndStrings(tmp); }},
           });
