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
#include <chrono>
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

// A date (milliseconds since 1970, UTC) in this machine's time zone, or
// in UTC; its milliseconds.
SystemTime dateOf(int64_t ms, bool utc, int *millis = nullptr) {
    const std::time_t secs = std::time_t(ms / 1000);
    if (millis)
        *millis = int(ms % 1000);
    std::tm tm{};
#ifdef _WIN32
    if (utc)
        gmtime_s(&tm, &secs);
    else
        localtime_s(&tm, &secs);
#else
    if (utc)
        gmtime_r(&secs, &tm);
    else
        localtime_r(&secs, &tm);
#endif
    return {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_wday, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
}

// A SYSTEMTIME's 8 words.
void writeSystemTime(Cpu &c, uint32_t p, const SystemTime &t, int millis) {
    const int words[] = {t.year, t.month, t.dayOfWeek, t.day, t.hour, t.minute, t.second, millis};
    for (int i = 0; i < 8; ++i)
        c.write16(p + uint32_t(2 * i), uint16_t(words[i]));
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

    // The program's threads change over between instructions, never inside
    // a stand-in, so these are atomic as they are.
    stdcall(k, "InterlockedExchange", 2, [](Cpu &c) {
        const uint32_t old = c.read32(c.arg(0));
        c.write32(c.arg(0), c.arg(1));
        return old;
    });
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

    // Local time is UTC once the date is the process's own (startDate), so
    // a run doesn't depend on this machine's time zone.
    auto localNow = [this](int *millis = nullptr) { return dateOf(dateMs(), startDate.has_value(), millis); };
    stdcall(k, "GetLocalTime", 1, [localNow](Cpu &c) {
        if (c.valid(c.arg(0), 16)) {
            int millis = 0;
            const SystemTime t = localNow(&millis);
            writeSystemTime(c, c.arg(0), t, millis);
        }
        return 0u;
    });
    stdcall(k, "GetSystemTime", 1, [this](Cpu &c) {
        if (c.valid(c.arg(0), 16)) {
            int millis = 0;
            const SystemTime t = dateOf(dateMs(), true, &millis);
            writeSystemTime(c, c.arg(0), t, millis);
        }
        return 0u;
    });
    // FILETIME: 100 ns units since 1601 (UTC).
    stdcall(k, "GetSystemTimeAsFileTime", 1, [this](Cpu &c) {
        if (c.valid(c.arg(0), 8)) {
            constexpr uint64_t kUnixEpoch = 116444736000000000ULL;
            const uint64_t t = kUnixEpoch + uint64_t(dateMs()) * 10000;
            c.write32(c.arg(0), uint32_t(t));
            c.write32(c.arg(0) + 4, uint32_t(t >> 32));
        }
        return 0u;
    });

    // English (United States), Windows' Western European code page.
    stdcall(k, "GetSystemDefaultLCID", 0, [](Cpu &) { return 0x409u; });
    stdcall(k, "GetUserDefaultLCID", 0, [](Cpu &) { return 0x409u; });
    stdcall(k, "IsValidCodePage", 1, [](Cpu &c) {
        const uint32_t cp = c.arg(0);
        return uint32_t(cp == 437 || cp == 850 || cp == 1252);
    });
    // The mode is only kept: no error a program could hide shows a box here.
    stdcall(k, "SetErrorMode", 1, [this](Cpu &c) {
        const uint32_t old = _errorMode;
        _errorMode = c.arg(0);
        return old;
    });
    stdcall(k, "SetPriorityClass", 2, [](Cpu &) { return 1u; });

    stdcall(k, "lstrlenA", 1, [](Cpu &c) {
        uint32_t n = 0;
        if (c.arg(0))
            while (c.valid(c.arg(0) + n, 1) && c.read8(c.arg(0) + n))
                ++n;
        return n;
    });
    stdcall(k, "lstrcpyA", 2, [](Cpu &c) {
        const uint32_t to = c.arg(0);
        const uint32_t from = c.arg(1);
        uint32_t i = 0;
        for (; c.valid(from + i, 1) && c.valid(to + i, 1); ++i) {
            const uint8_t b = c.read8(from + i);
            c.write8(to + i, b);
            if (!b)
                return to;
        }
        return 0u;
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
    stdcall(k, "GetDateFormatA", 6, [formatted, localNow](Cpu &c) {
        const SystemTime t = c.arg(2) ? readSystemTime(c, c.arg(2)) : localNow();
        const std::string picture = c.arg(3)         ? c.string(c.arg(3))
                                    : (c.arg(1) & 2) ? "dddd, MMMM dd, yyyy" // DATE_LONGDATE
                                                     : "M/d/yyyy";
        return formatted(c, formatPicture(picture, t));
    });
    stdcall(k, "GetTimeFormatA", 6, [formatted, localNow](Cpu &c) {
        const SystemTime t = c.arg(2) ? readSystemTime(c, c.arg(2)) : localNow();
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

    // WINMM's sound outside DirectSound: no wave or auxiliary (CD audio)
    // device, as on a machine without sound.
    const char *w = "WINMM.dll";
    stdcall(w, "PlaySoundA", 3, [](Cpu &) { return 0u; });
    stdcall(w, "auxGetNumDevs", 0, [](Cpu &) { return 0u; });
    const uint32_t kBadDeviceId = 2; // MMSYSERR_BADDEVICEID
    stdcall(w, "auxGetDevCapsA", 3, [](Cpu &) { return kBadDeviceId; });
    stdcall(w, "auxGetVolume", 2, [](Cpu &) { return kBadDeviceId; });
    stdcall(w, "auxSetVolume", 2, [](Cpu &) { return kBadDeviceId; });
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
