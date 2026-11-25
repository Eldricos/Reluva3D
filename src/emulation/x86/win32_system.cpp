// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// KERNEL32's system information, timing, synchronisation and date and time
// text, as Microsoft documents them. The program's threads take turns,
// giving way only at Sleep, so critical sections have nothing to guard,
// and a wait on an event finds it signalled unless it's tracked
// (Win32Process::trackEvent). Host threads (win32_threads.cpp) put
// stand-ins of their own in place of these.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace reluva::x86 {

namespace {

constexpr uint32_t kErrorBufferOverflow = 111;
constexpr uint32_t kErrorInsufficientBuffer = 122;
constexpr uint32_t kEventHandles = 0x7fd00000;

struct SystemTime { // SYSTEMTIME
    int year;
    int month;
    int dayOfWeek;
    int day;
    int hour;
    int minute;
    int second;
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

// A FILETIME counts 100-nanosecond intervals from 1601; 1970's is this.
constexpr uint64_t kFileTime1970 = 116444736000000000ull;

uint64_t readFileTime(Cpu &c, uint32_t at) {
    return c.read32(at) | uint64_t(c.read32(at + 4)) << 32;
}
void writeFileTime(Cpu &c, uint32_t at, uint64_t t) {
    c.write32(at, uint32_t(t));
    c.write32(at + 4, uint32_t(t >> 32));
}

SystemTime readSystemTime(Cpu &c, uint32_t p) {
    return {c.read16(p),     c.read16(p + 2),  c.read16(p + 4), c.read16(p + 6),
            c.read16(p + 8), c.read16(p + 10), c.read16(p + 12)};
}

const char *const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                               "July",    "August",   "September", "October", "November", "December"};

// A date or time picture (English (United States)'s names): d dd ddd dddd,
// M MM MMM MMMM, y yy yyyy, h hh H HH, m mm, s ss, t tt, and text in
// single quotes.
std::string formatPicture(const std::string &picture, const SystemTime &t) {
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
        case 'h':
            number(hour12, std::min(count, 2));
            break;
        case 'H':
            number(t.hour, std::min(count, 2));
            break;
        case 'm':
            number(t.minute, std::min(count, 2));
            break;
        case 's':
            number(t.second, std::min(count, 2));
            break;
        case 't':
            out += count == 1 ? (t.hour < 12 ? "A" : "P") : (t.hour < 12 ? "AM" : "PM");
            break;
        default:
            out.append(n, ch);
            break;
        }
        i += n;
    }
    return out;
}

} // namespace

void Win32Process::addSystemStandIns() {
    const char *k = "KERNEL32.dll";

    // The date and time, by wallClockMicros. Times are UTC: no time zone,
    // no daylight saving (TIME_ZONE_ID_UNKNOWN), local time the same.
    stdcall(k, "GetLocalTime", 1, [this](Cpu &c) {
        writeSystemTime(c, c.arg(0), std::time_t(wallClockMicros() / 1000000), false);
        return 0u;
    });
    stdcall(k, "GetSystemTime", 1, [this](Cpu &c) {
        writeSystemTime(c, c.arg(0), std::time_t(wallClockMicros() / 1000000), false);
        return 0u;
    });
    stdcall(k, "GetSystemTimeAsFileTime", 1, [this](Cpu &c) {
        writeFileTime(c, c.arg(0), wallClockMicros() * 10 + kFileTime1970);
        return 0u;
    });
    stdcall(k, "GetTimeZoneInformation", 1, [](Cpu &c) {
        for (uint32_t i = 0; i < 172; i += 4)
            c.write32(c.arg(0) + i, 0);
        return 0u;
    });
    stdcall(k, "FileTimeToLocalFileTime", 2, [](Cpu &c) {
        writeFileTime(c, c.arg(1), readFileTime(c, c.arg(0)));
        return 1u;
    });
    stdcall(k, "LocalFileTimeToFileTime", 2, [](Cpu &c) {
        writeFileTime(c, c.arg(1), readFileTime(c, c.arg(0)));
        return 1u;
    });
    stdcall(k, "FileTimeToSystemTime", 2, [](Cpu &c) {
        const uint64_t t = readFileTime(c, c.arg(0));
        if (t < kFileTime1970)
            return 0u;
        writeSystemTime(c, c.arg(1), std::time_t((t - kFileTime1970) / 10000000), false);
        return 1u;
    });
    stdcall(k, "SystemTimeToFileTime", 2, [](Cpu &c) {
        const SystemTime s = readSystemTime(c, c.arg(0));
        // Days from 1970 by the proleptic Gregorian calendar.
        const int y = s.year - (s.month <= 2);
        const int era = (y >= 0 ? y : y - 399) / 400;
        const int yoe = y - era * 400;
        const int doy = (153 * (s.month + (s.month > 2 ? -3 : 9)) + 2) / 5 + s.day - 1;
        const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        const int64_t days = int64_t(era) * 146097 + doe - 719468;
        const int64_t seconds = days * 86400 + s.hour * 3600 + s.minute * 60 + s.second;
        if (seconds < 0)
            return 0u;
        writeFileTime(c, c.arg(1), uint64_t(seconds) * 10000000 + kFileTime1970);
        return 1u;
    });

    // A Pentium II-class single processor, 4 KB pages.
    stdcall(k, "GetSystemInfo", 1, [](Cpu &c) {
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
    stdcall(k, "GlobalMemoryStatus", 1, [this](Cpu &c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 32))
            return 0u;
        const uint32_t total = 256u << 20;
        const uint32_t free = std::min(memory().freeBytes(), total);
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
    stdcall(k, "GetComputerNameA", 2, [this](Cpu &c) {
        static const std::string kName = "RELUVA";
        const uint32_t buffer = c.arg(0);
        const uint32_t size = c.arg(1);
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
    stdcall(k, "MulDiv", 3, [](Cpu &c) {
        const int64_t a = int32_t(c.arg(0));
        const int64_t b = int32_t(c.arg(1));
        const int64_t d = int32_t(c.arg(2));
        if (d == 0)
            return 0xffffffffu;
        const int64_t p = a * b;
        const int64_t half = (d < 0 ? -d : d) / 2;
        const int64_t q = ((p < 0) != (d < 0) ? p - (p < 0 ? half : -half) : p + (p < 0 ? -half : half)) / d;
        return q > INT32_MAX || q < INT32_MIN ? 0xffffffffu : uint32_t(int32_t(q));
    });

    // Timing: the performance counter counts microseconds (at the
    // milliseconds' resolution).
    stdcall(k, "QueryPerformanceFrequency", 1, [](Cpu &c) {
        if (!c.valid(c.arg(0), 8))
            return 0u;
        c.write32(c.arg(0), 1000000);
        c.write32(c.arg(0) + 4, 0);
        return 1u;
    });
    stdcall(k, "QueryPerformanceCounter", 1, [this](Cpu &c) {
        if (!c.valid(c.arg(0), 8))
            return 0u;
        const uint64_t us = uint64_t(milliseconds()) * 1000;
        c.write32(c.arg(0), uint32_t(us));
        c.write32(c.arg(0) + 4, uint32_t(us >> 32));
        return 1u;
    });
    // Sleep: a thread gives way for that long; the main thread lets the
    // threads that are due have their turn.
    stdcall(k, "Sleep", 1, [this](Cpu &c) {
        if (inThread()) {
            threadSleep(c.arg(0));
        } else {
            _slept += c.arg(0);
            runThreads();
        }
        return 0u;
    });

    // Synchronisation, for threads that take turns.
    stdcall(k, "InitializeCriticalSection", 1, [](Cpu &c) {
        if (c.valid(c.arg(0), 24))
            std::memset(c.memory().data() + c.arg(0), 0, 24);
        return 0u;
    });
    for (const char *name : {"EnterCriticalSection", "LeaveCriticalSection", "DeleteCriticalSection"})
        stdcall(k, name, 1, [](Cpu &) { return 0u; });
    // CreateEventA(attributes, manual reset, initially set, name).
    stdcall(k, "CreateEventA", 4, [this](Cpu &c) {
        const uint32_t handle = kEventHandles + _nextEvent++;
        _events[handle] = {c.arg(1) != 0, c.arg(2) != 0, false};
        return handle;
    });
    stdcall(k, "SetEvent", 1, [this](Cpu &c) {
        setEvent(c.arg(0));
        return 1u;
    });
    stdcall(k, "ResetEvent", 1, [this](Cpu &c) {
        if (auto it = _events.find(c.arg(0)); it != _events.end())
            it->second.set = false;
        return 1u;
    });
    stdcall(k, "WaitForSingleObject", 2, [this](Cpu &c) {
        auto it = _events.find(c.arg(0));
        if (it == _events.end() || !it->second.tracked)
            return 0u; // WAIT_OBJECT_0
        if (!it->second.set)
            return 0x102u; // WAIT_TIMEOUT
        if (!it->second.manualReset)
            it->second.set = false;
        return 0u;
    });

    // GetDateFormatA and GetTimeFormatA (locale, flags, time or 0 for now,
    // picture or 0 for the default, buffer, its size): the characters
    // written with the terminator, or with a size of 0 those needed.
    auto formatted = [this](Cpu &c, const std::string &text) {
        const uint32_t buffer = c.arg(4);
        const uint32_t size = c.arg(5);
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
    stdcall(k, "GetDateFormatA", 6, [formatted](Cpu &c) {
        const SystemTime t =
            c.arg(2) ? readSystemTime(c, c.arg(2)) : x86::now(); // (not Win32Process::now, the threads' clock)
        const std::string picture = c.arg(3)         ? c.string(c.arg(3))
                                    : (c.arg(1) & 2) ? "dddd, MMMM dd, yyyy" // DATE_LONGDATE
                                                     : "M/d/yyyy";
        return formatted(c, formatPicture(picture, t));
    });
    stdcall(k, "GetTimeFormatA", 6, [formatted](Cpu &c) {
        const SystemTime t = c.arg(2) ? readSystemTime(c, c.arg(2)) : x86::now();
        const uint32_t flags = c.arg(1);
        std::string picture;
        if (c.arg(3)) {
            picture = c.string(c.arg(3));
        } else {
            picture = flags & 8 ? "H" : "h";             // TIME_FORCE24HOURFORMAT
            if (!(flags & 1))                            // TIME_NOMINUTESORSECONDS
                picture += flags & 2 ? ":mm" : ":mm:ss"; // TIME_NOSECONDS
            if (!(flags & 4) && !(flags & 8))            // TIME_NOTIMEMARKER
                picture += " tt";
        }
        return formatted(c, formatPicture(picture, t));
    });
}

void Win32Process::trackEvent(uint32_t handle) {
    if (auto it = _events.find(handle); it != _events.end())
        it->second.tracked = true;
}

bool Win32Process::setEvent(uint32_t handle) {
    if (auto it = _events.find(handle); it != _events.end()) {
        it->second.set = true;
        return true;
    }
    return setHostEvent(handle);
}

} // namespace reluva::x86
