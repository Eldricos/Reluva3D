// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's locale and string functions, local and universal time, global
// and local memory, and VERSION.dll's reading of a file's version
// information, as Microsoft documents them. The locale is English (United
// States), code page 1252, as the C runtimes of the time ask it.

#include "common/pe.h"
#include "emulation/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>

namespace reluva::x86 {

namespace {

constexpr uint32_t kErrorInsufficientBuffer = 122;
constexpr uint32_t kErrorInvalidFlags = 1004;
constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorResourceTypeNotFound = 1813;
// 100-nanosecond intervals from 1601 to 1970.
constexpr uint64_t kUnixEpoch = 116444736000000000ull;

std::tm toTm(std::time_t t, bool local) {
    std::tm tm{};
#ifdef _WIN32
    local ? localtime_s(&tm, &t) : gmtime_s(&tm, &t);
#else
    local ? localtime_r(&t, &tm) : gmtime_r(&t, &tm);
#endif
    return tm;
}

void writeSystemTime(Cpu &c, uint32_t p, const std::tm &tm, int ms) {
    c.write16(p, uint16_t(tm.tm_year + 1900));
    c.write16(p + 2, uint16_t(tm.tm_mon + 1));
    c.write16(p + 4, uint16_t(tm.tm_wday));
    c.write16(p + 6, uint16_t(tm.tm_mday));
    c.write16(p + 8, uint16_t(tm.tm_hour));
    c.write16(p + 10, uint16_t(tm.tm_min));
    c.write16(p + 12, uint16_t(tm.tm_sec));
    c.write16(p + 14, uint16_t(ms));
}

// The minutes local time is behind universal time (TIME_ZONE_INFORMATION's Bias).
int32_t biasMinutes() {
    const std::time_t t = std::time(nullptr);
    std::tm utc = toTm(t, false);
    std::tm local = toTm(t, true);
    utc.tm_isdst = local.tm_isdst;
    return int32_t(std::difftime(std::mktime(&utc), std::mktime(&local)) / 60);
}

// The answers GetLocaleInfo gives for English (United States), by LCTYPE.
const char *localeInfo(uint32_t type) {
    switch (type & 0xffff) {
    case 0x01:
        return "0409"; // LOCALE_ILANGUAGE
    case 0x02:
        return "English (United States)"; // LOCALE_SLANGUAGE
    case 0x03:
        return "ENU"; // LOCALE_SABBREVLANGNAME
    case 0x04:
        return "English"; // LOCALE_SNATIVELANGNAME
    case 0x05:
        return "1"; // LOCALE_ICOUNTRY
    case 0x06:
        return "United States"; // LOCALE_SCOUNTRY
    case 0x07:
        return "USA"; // LOCALE_SABBREVCTRYNAME
    case 0x09:
        return "0409"; // LOCALE_IDEFAULTLANGUAGE
    case 0x0b:
        return "437"; // LOCALE_IDEFAULTCODEPAGE
    case 0x0c:
        return ","; // LOCALE_SLIST
    case 0x0e:
        return "."; // LOCALE_SDECIMAL
    case 0x0f:
        return ","; // LOCALE_STHOUSAND
    case 0x10:
        return "3;0"; // LOCALE_SGROUPING
    case 0x14:
        return "$"; // LOCALE_SCURRENCY
    case 0x1d:
        return "/"; // LOCALE_SDATE
    case 0x1e:
        return ":"; // LOCALE_STIME
    case 0x1f:
        return "M/d/yyyy"; // LOCALE_SSHORTDATE
    case 0x20:
        return "dddd, MMMM dd, yyyy"; // LOCALE_SLONGDATE
    case 0x1001:
        return "English"; // LOCALE_SENGLANGUAGE
    case 0x1002:
        return "United States"; // LOCALE_SENGCOUNTRY
    case 0x1004:
        return "1252"; // LOCALE_IDEFAULTANSICODEPAGE
    default:
        return nullptr;
    }
}

int compareText(std::string a, std::string b, bool ignoreCase) {
    if (ignoreCase) {
        for (char &ch : a)
            ch = char(std::tolower(uint8_t(ch)));
        for (char &ch : b)
            ch = char(std::tolower(uint8_t(ch)));
    }
    const int r = a.compare(b);
    return r < 0 ? 1 : r == 0 ? 2 : 3; // CSTR_LESS_THAN, CSTR_EQUAL, CSTR_GREATER_THAN
}

// A string given with its length, or -1 for one ending in a zero.
std::string countedString(Cpu &c, uint32_t p, int32_t n) {
    return n < 0 ? c.string(p) : c.string(p, uint32_t(n));
}

std::string countedWide(Cpu &c, uint32_t p, int32_t n) {
    std::string out;
    for (uint32_t i = 0; n < 0 || i < uint32_t(n); ++i) {
        const uint16_t ch = c.read16(p + 2 * i);
        if (n < 0 && ch == 0)
            break;
        out += ch < 0x100 ? char(ch) : '?';
    }
    return out;
}

// A version resource's node (VS_VERSIONINFO and its children): its key,
// where its value is and how long, and its children's span.
struct VersionNode {
    std::u16string key;
    uint32_t value = 0;
    uint32_t valueLength = 0; // in bytes for binary values, in characters for text
    bool text = false;
    uint32_t children = 0;
    uint32_t end = 0;
};

VersionNode readNode(Cpu &c, uint32_t at) {
    VersionNode n;
    const uint16_t length = c.read16(at);
    n.valueLength = c.read16(at + 2);
    n.text = c.read16(at + 4) == 1;
    uint32_t p = at + 6;
    for (uint16_t ch; (ch = c.read16(p)) != 0; p += 2)
        n.key += char16_t(ch);
    p = (p + 2 + 3) & ~3u;
    n.value = p;
    p += n.text ? n.valueLength * 2 : n.valueLength;
    n.children = (p + 3) & ~3u;
    n.end = at + length;
    return n;
}

} // namespace

void Win32Process::addLocaleStandIns() {
    const char *k = "KERNEL32.dll";

    // The locale.
    for (const char *name : {"GetSystemDefaultLCID", "GetUserDefaultLCID", "GetThreadLocale"})
        stdcall(k, name, 0, [](Cpu &) { return 0x409u; });
    for (const char *name : {"GetSystemDefaultLangID", "GetUserDefaultLangID"})
        stdcall(k, name, 0, [](Cpu &) { return 0x409u; });
    // GetLocaleInfoA(lcid, type, buffer, size): with size 0, the size needed.
    stdcall(k, "GetLocaleInfoA", 4, [this](Cpu &c) {
        const char *info = localeInfo(c.arg(1));
        if (!info) {
            if (output)
                output("GetLocaleInfoA: type " + std::to_string(c.arg(1)) + " isn't known here\n");
            setLastError(kErrorInvalidFlags);
            return 0u;
        }
        const uint32_t needed = uint32_t(std::strlen(info) + 1);
        if (c.arg(3) == 0)
            return needed;
        if (c.arg(3) < needed) {
            setLastError(kErrorInsufficientBuffer);
            return 0u;
        }
        c.writeString(c.arg(2), info, needed);
        return needed;
    });
    stdcall(k, "GetLocaleInfoW", 4, [this](Cpu &c) {
        const char *info = localeInfo(c.arg(1));
        if (!info) {
            setLastError(kErrorInvalidFlags);
            return 0u;
        }
        const uint32_t needed = uint32_t(std::strlen(info) + 1);
        if (c.arg(3) == 0)
            return needed;
        if (c.arg(3) < needed) {
            setLastError(kErrorInsufficientBuffer);
            return 0u;
        }
        for (uint32_t i = 0; i < needed; ++i)
            c.write16(c.arg(2) + 2 * i, uint8_t(info[i]));
        return needed;
    });
    // CompareStringA(lcid, flags, a, aLength, b, bLength); NORM_IGNORECASE is 1.
    stdcall(k, "CompareStringA", 6, [](Cpu &c) {
        return uint32_t(compareText(countedString(c, c.arg(2), int32_t(c.arg(3))),
                                    countedString(c, c.arg(4), int32_t(c.arg(5))), c.arg(1) & 1));
    });
    stdcall(k, "CompareStringW", 6, [](Cpu &c) {
        return uint32_t(compareText(countedWide(c, c.arg(2), int32_t(c.arg(3))),
                                    countedWide(c, c.arg(4), int32_t(c.arg(5))), c.arg(1) & 1));
    });

    // Strings and counters.
    stdcall(k, "lstrlenA", 1, [](Cpu &c) { return c.arg(0) ? uint32_t(c.string(c.arg(0)).size()) : 0u; });
    // One thread of the program runs at a time, so these needn't lock.
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
    // The C runtime keeps its own copy of the environment; Windows' isn't read back.
    stdcall(k, "SetEnvironmentVariableA", 2, [](Cpu &) { return 1u; });
    // FormatMessageA(flags, source, id, language, buffer, size, arguments):
    // a system message is "Error <id>." With FORMAT_MESSAGE_ALLOCATE_BUFFER
    // (0x100) the buffer is made here and its address written to `buffer`.
    stdcall(k, "FormatMessageA", 7, [this](Cpu &c) {
        const std::string text = "Error " + std::to_string(c.arg(2)) + ".\r\n";
        const uint32_t n = uint32_t(text.size());
        uint32_t to = c.arg(4);
        if (c.arg(0) & 0x100) {
            const uint32_t block = memory().alloc(std::max(n + 1, c.arg(5)));
            if (!block)
                return 0u;
            c.write32(to, block);
            to = block;
        } else if (c.arg(5) < n + 1) {
            setLastError(kErrorInsufficientBuffer);
            return 0u;
        }
        c.writeString(to, text, n + 1);
        return n;
    });

    // Global and local memory: a block's handle is its address, so locking
    // gives it back as it is.
    stdcall(k, "GlobalAlloc", 2, [this](Cpu &c) {
        const uint32_t size = std::max(c.arg(1), 1u);
        const uint32_t p = memory().alloc(size);
        if (p && (c.arg(0) & 0x40)) // GMEM_ZEROINIT
            std::memset(c.memory().data() + p, 0, size);
        return p;
    });
    stdcall(k, "GlobalLock", 1, [](Cpu &c) { return c.arg(0); });
    stdcall(k, "GlobalUnlock", 1, [](Cpu &) { return 0u; });
    stdcall(k, "GlobalHandle", 1, [](Cpu &c) { return c.arg(0); });
    for (const char *name : {"GlobalFree", "LocalFree"})
        stdcall(k, name, 1, [this](Cpu &c) {
            memory().free(c.arg(0));
            return 0u;
        });

    // Time: SYSTEMTIME is 8 WORDs (year, month, day of the week, day, hour,
    // minute, second, milliseconds); FILETIME counts 100 ns from 1601, UTC.
    stdcall(k, "GetLocalTime", 1, [this](Cpu &c) {
        writeSystemTime(c, c.arg(0), dateTimeNow(true), 0);
        return 0u;
    });
    stdcall(k, "GetSystemTime", 1, [this](Cpu &c) {
        writeSystemTime(c, c.arg(0), dateTimeNow(false), 0);
        return 0u;
    });
    // TIME_ZONE_INFORMATION: its Bias, the rest zero (no names, no
    // daylight-saving dates): TIME_ZONE_ID_UNKNOWN.
    stdcall(k, "GetTimeZoneInformation", 1, [this](Cpu &c) {
        if (!c.valid(c.arg(0), 172))
            return 0xffffffffu;
        std::memset(c.memory().data() + c.arg(0), 0, 172);
        c.write32(c.arg(0), uint32_t(timeZoneBias()));
        return 0u;
    });
    stdcall(k, "FileTimeToLocalFileTime", 2, [this](Cpu &c) {
        uint64_t t = uint64_t(c.read32(c.arg(0))) | uint64_t(c.read32(c.arg(0) + 4)) << 32;
        t -= int64_t(timeZoneBias()) * 600000000ll;
        c.write32(c.arg(1), uint32_t(t));
        c.write32(c.arg(1) + 4, uint32_t(t >> 32));
        return 1u;
    });
    stdcall(k, "FileTimeToSystemTime", 2, [](Cpu &c) {
        const uint64_t t = uint64_t(c.read32(c.arg(0))) | uint64_t(c.read32(c.arg(0) + 4)) << 32;
        if (t < kUnixEpoch) {
            std::memset(c.memory().data() + c.arg(1), 0, 16);
            return 0u;
        }
        const uint64_t since = t - kUnixEpoch;
        writeSystemTime(c, c.arg(1), toTm(std::time_t(since / 10000000), false), int(since / 10000 % 1000));
        return 1u;
    });

    // The user: GetUserNameA(buffer, size*), the size counting the zero.
    stdcall("ADVAPI32.dll", "GetUserNameA", 2, [this](Cpu &c) {
        static const std::string kName = "Player";
        const uint32_t needed = uint32_t(kName.size() + 1);
        if (c.read32(c.arg(1)) < needed) {
            c.write32(c.arg(1), needed);
            setLastError(kErrorInsufficientBuffer);
            return 0u;
        }
        c.writeString(c.arg(0), kName, needed);
        c.write32(c.arg(1), needed);
        return 1u;
    });

    // VERSION.dll: a file's version information, read from its resources
    // (type 16), as the file has it.
    const auto versionResource = [this](Cpu &c, uint32_t name, std::vector<uint8_t> &out) {
        const auto host = mapPath ? mapPath(fullPath(c.string(name)), false) : std::nullopt;
        std::ifstream in(host ? *host : std::filesystem::path(), std::ios::binary);
        if (!host || !in) {
            setLastError(kErrorFileNotFound);
            return false;
        }
        const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        PeImage pe;
        if (parsePe(file, pe) || (out = findResource(pe, file, 16)).empty()) {
            setLastError(kErrorResourceTypeNotFound);
            return false;
        }
        return true;
    };
    // GetFileVersionInfoSizeA(name, handle*): room for the resource and its text as ANSI.
    stdcall("VERSION.dll", "GetFileVersionInfoSizeA", 2, [versionResource](Cpu &c) {
        std::vector<uint8_t> data;
        if (c.arg(1))
            c.write32(c.arg(1), 0);
        return versionResource(c, c.arg(0), data) ? uint32_t(data.size() * 2) : 0u;
    });
    // GetFileVersionInfoA(name, handle, size, buffer).
    stdcall("VERSION.dll", "GetFileVersionInfoA", 4, [versionResource](Cpu &c) {
        std::vector<uint8_t> data;
        if (!versionResource(c, c.arg(0), data))
            return 0u;
        const uint32_t n = std::min(uint32_t(data.size()), c.arg(2));
        if (!c.valid(c.arg(3), n))
            return 0u;
        std::memcpy(c.memory().data() + c.arg(3), data.data(), n);
        return 1u;
    });
    // VerQueryValueA(block, path, value*, length*): "\" is the fixed
    // information, "\StringFileInfo\<language>\<name>" a text (given as
    // ANSI, in the block's second half, as GetFileVersionInfoSizeA made
    // room for), "\VarFileInfo\Translation" the languages.
    stdcall("VERSION.dll", "VerQueryValueA", 4, [](Cpu &c) {
        const uint32_t block = c.arg(0);
        std::string path = c.string(c.arg(1));
        std::vector<std::u16string> parts;
        for (size_t i = 0; i < path.size();) {
            const size_t next = path.find('\\', i);
            const std::string part = path.substr(i, next == std::string::npos ? std::string::npos : next - i);
            if (!part.empty())
                parts.emplace_back(part.begin(), part.end());
            if (next == std::string::npos)
                break;
            i = next + 1;
        }
        VersionNode node = readNode(c, block);
        for (const std::u16string &part : parts) {
            bool found = false;
            for (uint32_t at = node.children; at + 6 <= node.end;) {
                const VersionNode child = readNode(c, at);
                if (child.end <= at)
                    break;
                std::u16string a = child.key;
                std::u16string b = part;
                for (auto *s : {&a, &b})
                    for (char16_t &ch : *s)
                        ch = ch < 0x80 ? char16_t(std::tolower(int(ch))) : ch;
                if (a == b) {
                    node = child;
                    found = true;
                    break;
                }
                at = (child.end + 3) & ~3u;
            }
            if (!found)
                return 0u;
        }
        if (!node.text) {
            c.write32(c.arg(2), node.value);
            c.write32(c.arg(3), node.valueLength);
            return 1u;
        }
        // The ANSI copy goes as far into the second half as the text is into the first.
        const uint32_t total = c.read16(block);
        const uint32_t ansi = block + total + (node.value - block) / 2;
        for (uint32_t i = 0; i < node.valueLength; ++i)
            c.write8(ansi + i, uint8_t(c.read16(node.value + 2 * i)));
        c.write32(c.arg(2), ansi);
        c.write32(c.arg(3), node.valueLength);
        return 1u;
    });
}

std::tm Win32Process::dateTimeNow(bool local) const {
    return toTm(std::time_t(dateTimeSeconds()), local && !dateTime);
}

int64_t Win32Process::dateTimeSeconds() const {
    return dateTime ? dateTime() : int64_t(std::time(nullptr));
}

int32_t Win32Process::timeZoneBias() const {
    return dateTime ? 0 : biasMinutes();
}

} // namespace reluva::x86
