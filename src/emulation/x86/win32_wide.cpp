// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's UTF-16 file functions, as Microsoft documents them, done by
// their ANSI twins: the strings converted (a character past code page
// 1252's first 256 becomes '?'), the ANSI stand-in called, its results
// converted back. So the two behave the same, path mapping and errors
// alike.

#include "emulation/x86/win32.h"

#include <string>
#include <vector>

namespace reluva::x86 {

namespace {

// WIN32_FIND_DATAA's and WIN32_FIND_DATAW's names: the same fields before them.
constexpr uint32_t kFindFixed = 44;
constexpr uint32_t kFindDataA = 320;
constexpr uint32_t kAltNameA = kFindFixed + 260;
constexpr uint32_t kAltNameW = kFindFixed + 2 * 260;

std::string readWide(const Cpu &c, uint32_t at, uint32_t max = 32767) {
    std::string out;
    for (uint32_t i = 0; i < max && c.valid(at + 2 * i, 2); ++i) {
        const uint16_t ch = c.read16(at + 2 * i);
        if (!ch)
            break;
        out += char(ch < 0x100 ? ch : '?');
    }
    return out;
}

// Writes `text` and its terminating zero as UTF-16, in at most `chars` characters.
void writeWide(Cpu &c, uint32_t at, const std::string &text, uint32_t chars) {
    for (uint32_t i = 0; i < chars; ++i) {
        const bool last = i + 1 == chars || i == text.size();
        c.write16(at + 2 * i, last ? 0 : uint8_t(text[i]));
        if (last)
            break;
    }
}

} // namespace

void Win32Process::addWideStandIns() {
    const char *k = "KERNEL32.dll";

    // An ANSI stand-in called from a UTF-16 one, with its own arguments.
    auto ansi = [this, k](Cpu &c, const char *name, const std::vector<uint32_t> &args) -> uint32_t {
        const uint32_t fn = provided(k, name);
        if (!fn) {
            c.abort(std::string("its UTF-16 twin needs ") + name + ", which isn't stood in for");
            return 0u;
        }
        uint32_t result = 0;
        if (Error e = c.call(fn, args, result))
            c.abort(e.message);
        return result;
    };
    // A UTF-16 string as ANSI in the process's memory, for the call; 0 for none.
    struct Strings {
        Arena &arena;
        std::vector<uint32_t> made;
        uint32_t operator()(Cpu &c, uint32_t wide) {
            if (!wide)
                return 0;
            const std::string text = readWide(c, wide);
            const uint32_t at = arena.alloc(uint32_t(text.size()) + 1);
            c.writeString(at, text, uint32_t(text.size()) + 1);
            made.push_back(at);
            return at;
        }
        ~Strings() {
            for (uint32_t at : made)
                arena.free(at);
        }
    };

    // Those whose first argument alone is a string, the rest passed as they are.
    const std::vector<std::pair<std::string, int>> pathFirst = {
        {"CreateFileW", 7},          {"DeleteFileW", 1},      {"GetFileAttributesW", 1},
        {"SetFileAttributesW", 2},   {"CreateDirectoryW", 2}, {"RemoveDirectoryW", 1},
        {"SetCurrentDirectoryW", 1}, {"GetDriveTypeW", 1},    {"GetDiskFreeSpaceW", 5},
    };
    for (const auto &[wideName, args] : pathFirst) {
        const std::string ansiName = wideName.substr(0, wideName.size() - 1) + "A";
        const int count = args;
        stdcall(k, wideName.c_str(), count, [this, ansi, ansiName, count](Cpu &c) {
            Strings strings{*_arena, {}};
            std::vector<uint32_t> a;
            for (int i = 0; i < count; ++i)
                a.push_back(i == 0 ? strings(c, c.arg(0)) : c.arg(i));
            return ansi(c, ansiName.c_str(), a);
        });
    }

    // GetCurrentDirectoryW(size in characters, buffer)
    stdcall(k, "GetCurrentDirectoryW", 2, [this, ansi](Cpu &c) {
        const uint32_t buffer = _arena->alloc(260);
        const uint32_t length = ansi(c, "GetCurrentDirectoryA", {260, buffer});
        const std::string folder = length ? c.string(buffer, 260) : std::string();
        _arena->free(buffer);
        if (!length)
            return 0u;
        if (c.arg(0) < folder.size() + 1)
            return uint32_t(folder.size() + 1);
        writeWide(c, c.arg(1), folder, c.arg(0));
        return uint32_t(folder.size());
    });

    // FindFirstFileW(pattern, WIN32_FIND_DATAW*), FindNextFileW(handle,
    // WIN32_FIND_DATAW*): the fixed fields as they are, the names widened.
    auto toWide = [](Cpu &c, uint32_t from, uint32_t to) {
        for (uint32_t i = 0; i < kFindFixed; i += 4)
            c.write32(to + i, c.read32(from + i));
        writeWide(c, to + kFindFixed, c.string(from + kFindFixed, 260), 260);
        writeWide(c, to + kAltNameW, c.string(from + kAltNameA, 14), 14);
    };
    stdcall(k, "FindFirstFileW", 2, [this, ansi, toWide](Cpu &c) {
        Strings strings{*_arena, {}};
        const uint32_t data = _arena->alloc(kFindDataA);
        const uint32_t handle = ansi(c, "FindFirstFileA", {strings(c, c.arg(0)), data});
        if (handle != 0xffffffffu)
            toWide(c, data, c.arg(1));
        _arena->free(data);
        return handle;
    });
    stdcall(k, "FindNextFileW", 2, [this, ansi, toWide](Cpu &c) {
        const uint32_t data = _arena->alloc(kFindDataA);
        const uint32_t found = ansi(c, "FindNextFileA", {c.arg(0), data});
        if (found)
            toWide(c, data, c.arg(1));
        _arena->free(data);
        return found;
    });
}

} // namespace reluva::x86
