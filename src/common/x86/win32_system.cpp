// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's system information, timing, synchronisation and date and time
// text, as Microsoft documents them. The process runs one thread, so
// critical sections have nothing to guard, Sleep has nothing to wait for,
// and a wait on an event finds it signalled (nothing else could set it).

#include "common/x86/win32.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace reluva::x86 {

namespace {

constexpr uint32_t kErrorBufferOverflow = 111, kErrorInsufficientBuffer = 122;
constexpr uint32_t kEventHandles = 0x7fd00000;

struct SystemTime { // SYSTEMTIME
    int year, month, dayOfWeek, day, hour, minute, second;
};

SystemTime now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_wday, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}

SystemTime readSystemTime(Cpu& c, uint32_t p) {
    return {c.read16(p), c.read16(p + 2), c.read16(p + 4), c.read16(p + 6),
            c.read16(p + 8), c.read16(p + 10), c.read16(p + 12)};
}

const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                               "July",    "August",   "September", "October", "November", "December"};

// A date or time picture (English (United States)'s names): d dd ddd dddd,
// M MM MMM MMMM, y yy yyyy, h hh H HH, m mm, s ss, t tt, and text in
// single quotes.
std::string formatPicture(const std::string& picture, const SystemTime& t) {
    std::string out;
    auto number = [&](int v, int width) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%0*d", width, v);
        out += buf;
    };
    for (size_t i = 0; i < picture.size();) {
        const char ch = picture[i];
        if (ch == '\'') {
            size_t end = picture.find('\'', i + 1);
            if (end == std::string::npos)
                end = picture.size();
            out += picture.substr(i + 1, end - i - 1);
            i = end + 1;
            continue;
        }
        size_t n = 1;
        while (i + n < picture.size() && picture[i + n] == ch)
            ++n;
        const int count = int(n);
        const int hour12 = t.hour % 12 == 0 ? 12 : t.hour % 12;
        switch (ch) {
        case 'd':
            if (count <= 2)
                number(t.day, count);
            else
                out += count == 3 ? std::string(kDays[t.dayOfWeek % 7], 3) : kDays[t.dayOfWeek % 7];
            break;
        case 'M':
            if (count <= 2)
                number(t.month, count);
            else
                out += count == 3 ? std::string(kMonths[(t.month + 11) % 12], 3) : kMonths[(t.month + 11) % 12];
            break;
        case 'y':
            if (count <= 2)
                number(t.year % 100, count);
            else
                number(t.year, 4);
            break;
        case 'h': number(hour12, std::min(count, 2)); break;
        case 'H': number(t.hour, std::min(count, 2)); break;
        case 'm': number(t.minute, std::min(count, 2)); break;
        case 's': number(t.second, std::min(count, 2)); break;
        case 't': out += count == 1 ? (t.hour < 12 ? "A" : "P") : (t.hour < 12 ? "AM" : "PM"); break;
        default: out.append(n, ch); break;
        }
        i += n;
    }
    return out;
}

} // namespace

void Win32Process::addSystemStandIns() {
    const char* k = "KERNEL32.dll";

    // A Pentium II-class single processor, 4 KB pages.
    stdcall(k, "GetSystemInfo", 1, [](Cpu& c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 36))
            return 0u;
        std::memset(c.memory().data() + p, 0, 36);
        c.write32(p + 4, 4096);        // dwPageSize
        c.write32(p + 8, 0x10000);     // lpMinimumApplicationAddress
        c.write32(p + 12, 0x7ffeffff); // lpMaximumApplicationAddress
        c.write32(p + 16, 1);          // dwActiveProcessorMask
        c.write32(p + 20, 1);          // dwNumberOfProcessors
        c.write32(p + 24, 586);        // dwProcessorType: PROCESSOR_INTEL_PENTIUM
        c.write32(p + 28, 0x10000);    // dwAllocationGranularity
        c.write16(p + 32, 6);          // wProcessorLevel
        return 0u;
    });
    // 256 MB of memory, as much free as the process's memory has.
    stdcall(k, "GlobalMemoryStatus", 1, [this](Cpu& c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 32))
            return 0u;
        const uint32_t total = 256u << 20, free = std::min(memory().freeBytes(), total);
        c.write32(p, 32);
        c.write32(p + 4, uint32_t(100 - uint64_t(free) * 100 / total)); // dwMemoryLoad
        c.write32(p + 8, total);
        c.write32(p + 12, free);
        c.write32(p + 16, 2 * total); // the page file
        c.write32(p + 20, 2 * total - (total - free));
        c.write32(p + 24, 0x7ffe0000); // the address space
        c.write32(p + 28, free);
        return 0u;
    });
    stdcall(k, "GetComputerNameA", 2, [this](Cpu& c) {
        static const std::string kName = "RELUVA";
        const uint32_t buffer = c.arg(0), size = c.arg(1);
        if (!c.valid(size, 4))
            return 0u;
        if (c.read32(size) < kName.size() + 1 || !c.valid(buffer, uint32_t(kName.size() + 1))) {
            c.write32(size, uint32_t(kName.size() + 1));
            setLastError(kErrorBufferOverflow);
            return 0u;
        }
        c.writeString(buffer, kName, uint32_t(kName.size() + 1));
        c.write32(size, uint32_t(kName.size()));
        return 1u;
    });
    // a × b ÷ c with a 64-bit product, rounded half away from zero; -1 if c
    // is 0 or the result doesn't fit.
    stdcall(k, "MulDiv", 3, [](Cpu& c) {
        const int64_t a = int32_t(c.arg(0)), b = int32_t(c.arg(1)), d = int32_t(c.arg(2));
        if (d == 0)
            return 0xffffffffu;
        const int64_t p = a * b;
        const int64_t half = (d < 0 ? -d : d) / 2;
        const int64_t q = ((p < 0) != (d < 0) ? p - (p < 0 ? half : -half) : p + (p < 0 ? -half : half)) / d;
        return q > INT32_MAX || q < INT32_MIN ? 0xffffffffu : uint32_t(int32_t(q));
    });

    // Timing: the performance counter counts microseconds (at the
    // milliseconds' resolution).
    stdcall(k, "QueryPerformanceFrequency", 1, [](Cpu& c) {
        if (!c.valid(c.arg(0), 8))
            return 0u;
        c.write32(c.arg(0), 1000000);
        c.write32(c.arg(0) + 4, 0);
        return 1u;
    });
    stdcall(k, "QueryPerformanceCounter", 1, [this](Cpu& c) {
        if (!c.valid(c.arg(0), 8))
            return 0u;
        const uint64_t us = uint64_t(milliseconds()) * 1000;
        c.write32(c.arg(0), uint32_t(us));
        c.write32(c.arg(0) + 4, uint32_t(us >> 32));
        return 1u;
    });
    stdcall(k, "Sleep", 1, [](Cpu&) { return 0u; });

    // Synchronisation, for one thread.
    stdcall(k, "InitializeCriticalSection", 1, [](Cpu& c) {
        if (c.valid(c.arg(0), 24))
            std::memset(c.memory().data() + c.arg(0), 0, 24);
        return 0u;
    });
    for (const char* name : {"EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection"})
        stdcall(k, name, 1, [](Cpu&) { return 0u; });
    stdcall(k, "CreateEventA", 4, [this](Cpu&) { return kEventHandles + nextEvent_++; });
    stdcall(k, "SetEvent", 1, [](Cpu&) { return 1u; });
    stdcall(k, "ResetEvent", 1, [](Cpu&) { return 1u; });
    stdcall(k, "WaitForSingleObject", 2, [](Cpu&) { return 0u; }); // WAIT_OBJECT_0

    // GetDateFormatA and GetTimeFormatA (locale, flags, time or 0 for now,
    // picture or 0 for the default, buffer, its size): the characters
    // written with the terminator, or with a size of 0 those needed.
    auto formatted = [this](Cpu& c, const std::string& text) {
        const uint32_t buffer = c.arg(4), size = c.arg(5);
        const uint32_t n = uint32_t(text.size() + 1);
        if (size == 0)
            return n;
        if (size < n || !c.valid(buffer, n)) {
            setLastError(kErrorInsufficientBuffer);
            return 0u;
        }
        c.writeString(buffer, text, n);
        return n;
    };
    stdcall(k, "GetDateFormatA", 6, [formatted](Cpu& c) {
        const SystemTime t = c.arg(2) ? readSystemTime(c, c.arg(2)) : now();
        const std::string picture = c.arg(3) ? c.string(c.arg(3))
                                  : (c.arg(1) & 2)  ? "dddd, MMMM dd, yyyy" // DATE_LONGDATE
                                                    : "M/d/yyyy";
        return formatted(c, formatPicture(picture, t));
    });
    stdcall(k, "GetTimeFormatA", 6, [formatted](Cpu& c) {
        const SystemTime t = c.arg(2) ? readSystemTime(c, c.arg(2)) : now();
        const uint32_t flags = c.arg(1);
        std::string picture;
        if (c.arg(3)) {
            picture = c.string(c.arg(3));
        } else {
            picture = flags & 8 ? "H" : "h"; // TIME_FORCE24HOURFORMAT
            if (!(flags & 1))                // TIME_NOMINUTESORSECONDS
                picture += flags & 2 ? ":mm" : ":mm:ss"; // TIME_NOSECONDS
            if (!(flags & 4) && !(flags & 8)) // TIME_NOTIMEMARKER
                picture += " tt";
        }
        return formatted(c, formatPicture(picture, t));
    });
}

} // namespace reluva::x86
