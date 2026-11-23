// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Windows functions StarFleet2.exe calls that the shared stand-ins
// (src/emulation/x86/) don't have, as the hosting spike found it needed
// them (docs/engines/sfc/games/sfc2/systems/hosting.md). Written from
// Microsoft's documentation of each; most belong in the shared layer once
// the game runs, and are kept here until then.

#include "common/fs.h"
#include "common/stream.h"
#include "emulation/x86/format.h"
#include "engines/sfc/host/game_process.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace reluva::sfc {

namespace {

using x86::Cpu;

constexpr uint32_t kLcidEnglishUs = 0x409;

// A SYSTEMTIME's eight 16-bit fields for a moment, as UTC or local time.
void writeSystemTime(Cpu &c, uint32_t at, std::time_t t, bool local) {
    std::tm tm{};
#if defined(_WIN32)
    local ? localtime_s(&tm, &t) : gmtime_s(&tm, &t);
#else
    local ? localtime_r(&t, &tm) : gmtime_r(&t, &tm);
#endif
    const uint16_t fields[8] = {
        uint16_t(tm.tm_year + 1900), uint16_t(tm.tm_mon + 1), uint16_t(tm.tm_wday), uint16_t(tm.tm_mday),
        uint16_t(tm.tm_hour),        uint16_t(tm.tm_min),     uint16_t(tm.tm_sec),  0};
    for (int i = 0; i < 8; ++i)
        c.write16(at + 2 * uint32_t(i), fields[i]);
}

// 100-nanosecond intervals since 1601, as a FILETIME counts them, from
// microseconds since 1970.
uint64_t fileTime(uint64_t since1970) {
    return since1970 * 10 + 116444736000000000ull;
}

// What GetPrivateProfileString reads: a key's value in a section of an INI
// file, the names compared without case; nothing if there's none.
bool iniValue(const std::filesystem::path &file, const std::string &section, const std::string &key, std::string &out) {
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return false;
    const std::string text(data.begin(), data.end());
    const std::string wantSection = fs::toLower(section);
    const std::string wantKey = fs::toLower(key);
    bool inSection = false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (!line.empty() && line[0] == '[') {
            const size_t close = line.find(']');
            inSection = close != std::string::npos && fs::toLower(line.substr(1, close - 1)) == wantSection;
            continue;
        }
        const size_t eq = line.find('=');
        if (!inSection || eq == std::string::npos)
            continue;
        std::string name = line.substr(0, eq);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        if (fs::toLower(name) != wantKey)
            continue;
        std::string value = line.substr(eq + 1);
        // sfc.ini puts comments after a value: "Rescale=0\t; Set this to 1..."
        if (const size_t semi = value.find(';'); semi != std::string::npos)
            value = value.substr(0, semi);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();
        size_t first = 0;
        while (first < value.size() && (value[first] == ' ' || value[first] == '\t'))
            ++first;
        out = value.substr(first);
        return true;
    }
    return false;
}

} // namespace

void GameProcess::stdcall(const char *dll, const char *name, int args, std::function<uint32_t(x86::Cpu &)> fn) {
    const uint32_t pop = uint32_t(args) * 4;
    _process->provide(dll, name, [fn = std::move(fn), pop](Cpu &c) {
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    });
}

std::string GameProcess::describeException(x86::Cpu &c, uint32_t record) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "exception 0x%08x at 0x%08x", c.read32(record), c.read32(record + 12));
    std::string what = buf;
    // Visual C++'s throw raises 0xe06d7363 with 0x19930520, the object and
    // its ThrowInfo; ThrowInfo's fourth word is its catchable types, whose
    // first names the thrown type (at its type descriptor's +8).
    if (c.read32(record) != 0xe06d7363 || c.read32(record + 16) < 3)
        return what;
    const uint32_t object = c.read32(record + 24);
    const uint32_t throwInfo = c.read32(record + 28);
    const uint32_t types = throwInfo ? c.read32(throwInfo + 12) : 0;
    const uint32_t count = types ? c.read32(types) : 0;
    for (uint32_t i = 0; i < count && i < 8; ++i) {
        const uint32_t type = c.read32(c.read32(types + 4 + 4 * i) + 4);
        what += (i ? ", " : ": a C++ throw of ") + c.string(type + 8, 200);
    }
    // The game's tErrInfo holds its message as text, in fields of their
    // own: every run of text in the object's first 1 KB.
    std::string text;
    for (uint32_t i = 0; i < 1024 && c.valid(object + i, 1); ++i) {
        const uint8_t ch = c.read8(object + i);
        if (ch >= 0x20 && ch < 0x7f)
            text += char(ch);
        else if (!text.empty() && text.back() != '|')
            text += '|';
    }
    if (text.size() > 8)
        what += " text: " + text;
    return what;
}

void GameProcess::addSystem() {
    const char *k = "KERNEL32.dll";
    // Threads take turns, so these needn't be atomic on the host.
    stdcall(k, "InterlockedIncrement", 1, [](Cpu &c) {
        const uint32_t v = c.read32(c.arg(0)) + 1;
        c.write32(c.arg(0), v);
        return v;
    });
    stdcall(k, "InterlockedDecrement", 1, [](Cpu &c) {
        const uint32_t v = c.read32(c.arg(0)) - 1;
        c.write32(c.arg(0), v);
        return v;
    });
    // Asked for by name (GetProcAddress) on Windows NT and its successors,
    // where the game doesn't use its OSSpecific95 and 98 DLLs instead.
    stdcall(k, "InterlockedCompareExchange", 3, [](Cpu &c) {
        const uint32_t old = c.read32(c.arg(0));
        if (old == c.arg(2))
            c.write32(c.arg(0), c.arg(1));
        return old;
    });
    stdcall(k, "InterlockedExchange", 2, [](Cpu &c) {
        const uint32_t old = c.read32(c.arg(0));
        c.write32(c.arg(0), c.arg(1));
        return old;
    });
    // Global memory as fixed blocks: a handle is its address.
    stdcall(k, "GlobalAlloc", 2, [this](Cpu &c) { return _process->memory().alloc(std::max(c.arg(1), 1u)); });
    stdcall(k, "GlobalFree", 1, [this](Cpu &c) {
        if (c.arg(0))
            _process->memory().free(c.arg(0));
        return 0u;
    });
    stdcall(k, "GlobalLock", 1, [](Cpu &c) { return c.arg(0); });
    stdcall(k, "GlobalUnlock", 1, [](Cpu &) { return 1u; });
    stdcall(k, "GlobalHandle", 1, [](Cpu &c) { return c.arg(0); });
    stdcall(k, "GlobalReAlloc", 3, [this](Cpu &c) {
        const uint32_t old = c.arg(0);
        const uint32_t size = std::max(c.arg(1), 1u);
        const uint32_t fresh = _process->memory().alloc(size);
        if (old) {
            const uint32_t keep = std::min(size, _process->memory().size(old));
            for (uint32_t i = 0; i < keep; ++i)
                c.write8(fresh + i, c.read8(old + i));
            _process->memory().free(old);
        }
        return fresh;
    });
    stdcall(k, "lstrlenA", 1, [](Cpu &c) { return c.arg(0) ? uint32_t(c.string(c.arg(0)).size()) : 0u; });
    stdcall(k, "SetErrorMode", 1, [](Cpu &) { return 0u; });
    // English (United States), code page 1252.
    stdcall(k, "GetUserDefaultLCID", 0, [](Cpu &) { return kLcidEnglishUs; });
    stdcall(k, "IsValidCodePage", 1, [](Cpu &c) { return c.arg(0) == 1252 || c.arg(0) == 437 ? 1u : 0u; });
    stdcall(k, "IsValidLocale", 2, [](Cpu &c) { return (c.arg(0) & 0x3ff) == 0x09 ? 1u : 0u; });
    stdcall(k, "EnumSystemLocalesA", 2, [](Cpu &) { return 1u; });
    stdcall(k, "GetLocaleInfoA", 4, [](Cpu &c) {
        // LOCALE_IDEFAULTANSICODEPAGE (0x1004), the one the C runtime asks.
        const std::string value = (c.arg(1) & 0xffff) == 0x1004 ? "1252" : (c.arg(1) & 0xffff) == 0xb ? "437" : "";
        if (value.empty())
            return 0u;
        if (c.arg(3) == 0)
            return uint32_t(value.size() + 1);
        if (c.arg(3) < value.size() + 1)
            return 0u;
        for (size_t i = 0; i <= value.size(); ++i)
            c.write8(c.arg(2) + uint32_t(i), i < value.size() ? uint8_t(value[i]) : 0);
        return uint32_t(value.size() + 1);
    });
    stdcall(k, "GetLocalTime", 1, [this](Cpu &c) {
        writeSystemTime(c, c.arg(0), std::time_t(wallClockMicros() / 1000000), true);
        return 0u;
    });
    stdcall(k, "GetSystemTime", 1, [this](Cpu &c) {
        writeSystemTime(c, c.arg(0), std::time_t(wallClockMicros() / 1000000), false);
        return 0u;
    });
    stdcall(k, "GetSystemTimeAsFileTime", 1, [this](Cpu &c) {
        const uint64_t t = fileTime(wallClockMicros());
        c.write32(c.arg(0), uint32_t(t));
        c.write32(c.arg(0) + 4, uint32_t(t >> 32));
        return 0u;
    });
    stdcall(k, "GetTimeZoneInformation", 1, [](Cpu &c) {
        // UTC, no daylight saving: TIME_ZONE_ID_UNKNOWN.
        for (uint32_t i = 0; i < 172; i += 4)
            c.write32(c.arg(0) + i, 0);
        return 0u;
    });
    stdcall(k, "GetWindowsDirectoryA", 2, [](Cpu &c) {
        const std::string dir = "C:\\WINDOWS";
        if (c.arg(1) <= dir.size())
            return uint32_t(dir.size() + 1);
        for (size_t i = 0; i <= dir.size(); ++i)
            c.write8(c.arg(0) + uint32_t(i), i < dir.size() ? uint8_t(dir[i]) : 0);
        return uint32_t(dir.size());
    });
    stdcall(k, "GetSystemDirectoryA", 2, [](Cpu &c) {
        const std::string dir = "C:\\WINDOWS\\SYSTEM";
        if (c.arg(1) <= dir.size())
            return uint32_t(dir.size() + 1);
        for (size_t i = 0; i <= dir.size(); ++i)
            c.write8(c.arg(0) + uint32_t(i), i < dir.size() ? uint8_t(dir[i]) : 0);
        return uint32_t(dir.size());
    });
    // Room enough on any drive: 2 GB free, as Windows 95 could count it.
    stdcall(k, "GetDiskFreeSpaceA", 5, [](Cpu &c) {
        const uint32_t out[4] = {8, 512, 0x80000, 0x100000}; // sectors a cluster, bytes a sector, free, all
        for (int i = 0; i < 4; ++i)
            if (c.arg(1 + i))
                c.write32(c.arg(1 + i), out[i]);
        return 1u;
    });
    stdcall(k, "GetFullPathNameA", 4, [this](Cpu &c) {
        const std::string full = _process->fullPath(c.string(c.arg(0)));
        if (c.arg(1) <= full.size())
            return uint32_t(full.size() + 1);
        uint32_t filePart = 0;
        for (size_t i = 0; i <= full.size(); ++i) {
            c.write8(c.arg(2) + uint32_t(i), i < full.size() ? uint8_t(full[i]) : 0);
            if (i < full.size() && full[i] == '\\')
                filePart = c.arg(2) + uint32_t(i) + 1;
        }
        if (c.arg(3))
            c.write32(c.arg(3), filePart);
        return uint32_t(full.size());
    });
    stdcall(k, "OpenMutexA", 3, [this](Cpu &) {
        _process->setLastError(2); // ERROR_FILE_NOT_FOUND: no other copy running
        return 0u;
    });
    stdcall(k, "SetEnvironmentVariableA", 2, [](Cpu &) { return 1u; });
    stdcall(k, "FormatMessageA", 7, [](Cpu &) { return 0u; });
    stdcall(k, "VirtualQuery", 3, [](Cpu &c) {
        // One committed, read-write region around the address.
        const uint32_t page = c.arg(0) & ~0xfffu;
        const uint32_t info[7] = {page, page, 4, 0x1000, 0x1000, 4, 0x20000}; // MEM_COMMIT, PAGE_READWRITE, MEM_PRIVATE
        for (uint32_t i = 0; i < 7 && 4 * i < c.arg(2); ++i)
            c.write32(c.arg(1) + 4 * i, info[i]);
        return std::min(c.arg(2), 28u);
    });
    stdcall(k, "CompareStringA", 6, [](Cpu &c) {
        // NORM_IGNORECASE (1) the only flag heeded; lengths of -1 are to the zero.
        const auto get = [&](uint32_t at, uint32_t len) {
            std::string s = int32_t(len) < 0 ? c.string(at) : c.string(at, len);
            return (c.arg(1) & 1) ? fs::toLower(s) : s;
        };
        const int r = get(c.arg(2), c.arg(3)).compare(get(c.arg(4), c.arg(5)));
        return r < 0 ? 1u : r == 0 ? 2u : 3u;
    });
    stdcall(k, "FileTimeToLocalFileTime", 2, [](Cpu &c) {
        c.write32(c.arg(1), c.read32(c.arg(0)));
        c.write32(c.arg(1) + 4, c.read32(c.arg(0) + 4));
        return 1u;
    });
    stdcall(k, "FileTimeToSystemTime", 2, [](Cpu &c) {
        const uint64_t t = c.read32(c.arg(0)) | uint64_t(c.read32(c.arg(0) + 4)) << 32;
        writeSystemTime(c, c.arg(1), std::time_t((t - 116444736000000000ull) / 10000000), false);
        return 1u;
    });
    stdcall("USER32.dll", "wsprintfA", 0, [](Cpu &c) {
        // cdecl: the caller pops; the output, the format, then its arguments.
        const std::string text = x86::formatPrintf(c, c.arg(1), c.reg[Cpu::ESP] + 12);
        for (size_t i = 0; i <= text.size(); ++i)
            c.write8(c.arg(0) + uint32_t(i), i < text.size() ? uint8_t(text[i]) : 0);
        return uint32_t(text.size());
    });
    const char *u = "USER32.dll";
    // The window's menus: handles that hold nothing, as the game is shown
    // full-screen without them.
    stdcall(u, "LoadMenuA", 2, [](Cpu &) { return 0x7100u; });
    stdcall(u, "GetMenu", 1, [](Cpu &) { return 0u; });
    stdcall(u, "GetSystemMenu", 2, [](Cpu &) { return 0x7101u; });
    for (const auto &[name, args] : std::vector<std::pair<const char *, int>>{{"SetMenu", 2},
                                                                              {"DrawMenuBar", 1},
                                                                              {"DeleteMenu", 3},
                                                                              {"AppendMenuA", 4},
                                                                              {"SetForegroundWindow", 1},
                                                                              {"IsWindow", 1},
                                                                              {"SetWindowTextA", 2},
                                                                              {"InvalidateRect", 3},
                                                                              {"RedrawWindow", 4},
                                                                              {"EndPaint", 2},
                                                                              {"MessageBeep", 1},
                                                                              {"FillRect", 3}})
        stdcall(u, name, args, [](Cpu &) { return 1u; });
    stdcall(u, "SetRect", 5, [](Cpu &c) {
        for (uint32_t i = 0; i < 4; ++i)
            c.write32(c.arg(0) + 4 * i, c.arg(1 + int(i)));
        return 1u;
    });
    stdcall(u, "OffsetRect", 3, [](Cpu &c) {
        for (uint32_t i = 0; i < 4; ++i)
            c.write32(c.arg(0) + 4 * i, c.read32(c.arg(0) + 4 * i) + c.arg(1 + int(i % 2)));
        return 1u;
    });
    stdcall(u, "CheckMenuItem", 3, [](Cpu &) { return 0u; });
    stdcall(u, "IsIconic", 1, [](Cpu &) { return 0u; });
    stdcall(u, "SetFocus", 1, [this](Cpu &c) {
        const uint32_t old = _focus;
        _focus = c.arg(0);
        return old;
    });
    stdcall(u, "SetActiveWindow", 1, [this](Cpu &c) {
        const uint32_t old = _focus;
        _focus = c.arg(0);
        return old;
    });
    stdcall(u, "GetActiveWindow", 0, [this](Cpu &) { return _focus; });
    stdcall(u, "GetDesktopWindow", 0, [](Cpu &) { return 0x10010u; });
    stdcall(u, "GetUpdateRect", 3, [](Cpu &) { return 0u; });
    stdcall(u, "BeginPaint", 2, [](Cpu &c) {
        for (uint32_t i = 0; i < 64; i += 4)
            c.write32(c.arg(1) + i, 0);
        c.write32(c.arg(1), 0x5100); // its device context
        return 0x5100u;
    });
    stdcall(u, "MapVirtualKeyA", 2, [](Cpu &c) { return c.arg(0); });
    stdcall(u, "VkKeyScanA", 1, [](Cpu &c) {
        const uint8_t ch = uint8_t(c.arg(0));
        return uint32_t(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
    });
    stdcall(u, "ScreenToClient", 2, [](Cpu &) { return 1u; });
    const char *g = "GDI32.dll";
    // Text and pictures drawn with GDI (its debug console, error boxes): accepted, not drawn.
    for (const auto &[name, args] : std::vector<std::pair<const char *, int>>{{"TextOutA", 5},
                                                                              {"ExtTextOutA", 8},
                                                                              {"DeleteObject", 1},
                                                                              {"DeleteDC", 1},
                                                                              {"BitBlt", 9},
                                                                              {"StretchBlt", 11},
                                                                              {"PatBlt", 6}})
        stdcall(g, name, args, [](Cpu &) { return 1u; });
    for (const auto &[name, args] : std::vector<std::pair<const char *, int>>{
             {"SetTextAlign", 2}, {"SetBkColor", 2}, {"SetBkMode", 2}, {"SetTextColor", 2}, {"SelectObject", 2}})
        stdcall(g, name, args, [](Cpu &) { return 0u; });
    stdcall(g, "CreateFontA", 14, [](Cpu &) { return 0x5200u; });
    stdcall(g, "CreateCompatibleDC", 1, [](Cpu &) { return 0x5101u; });
    stdcall(g, "GetTextMetricsA", 2, [](Cpu &c) {
        for (uint32_t i = 0; i < 56; i += 4)
            c.write32(c.arg(1) + i, 0);
        c.write32(c.arg(1), 16);     // height
        c.write32(c.arg(1) + 4, 13); // ascent
        c.write32(c.arg(1) + 20, 8); // average width
        return 1u;
    });
    stdcall(g, "GetTextExtentPoint32A", 4, [](Cpu &c) {
        c.write32(c.arg(3), c.arg(2) * 8);
        c.write32(c.arg(3) + 4, 16);
        return 1u;
    });
    // The INI files the program reads, sfc.ini first, in its folder unless
    // given a path.
    const auto profileString = [this](Cpu &c, uint32_t section, uint32_t key, uint32_t file, std::string &out) {
        if (!section || !key)
            return false;
        std::string name = c.string(file);
        if (name.find('\\') == std::string::npos && name.find(':') == std::string::npos)
            name = std::string(kInstallFolder) + name;
        const auto path = _process->mapPath ? _process->mapPath(name, false) : std::nullopt;
        if (iniReads)
            iniReads(name + " [" + c.string(section) + "] " + c.string(key));
        return path && iniValue(*path, c.string(section), c.string(key), out);
    };
    stdcall(k, "GetPrivateProfileStringA", 6, [profileString](Cpu &c) {
        std::string value;
        if (!profileString(c, c.arg(0), c.arg(1), c.arg(5), value))
            value = c.arg(2) ? c.string(c.arg(2)) : std::string();
        const uint32_t size = c.arg(4);
        if (size == 0)
            return 0u;
        if (value.size() >= size)
            value.resize(size - 1);
        for (size_t i = 0; i <= value.size(); ++i)
            c.write8(c.arg(3) + uint32_t(i), i < value.size() ? uint8_t(value[i]) : 0);
        return uint32_t(value.size());
    });
    stdcall(k, "GetPrivateProfileIntA", 4, [profileString](Cpu &c) {
        std::string value;
        if (!profileString(c, c.arg(0), c.arg(1), c.arg(3), value))
            return c.arg(2);
        return uint32_t(std::strtol(value.c_str(), nullptr, 10));
    });
}

} // namespace reluva::sfc
