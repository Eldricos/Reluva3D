// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What a game asks of the Windows around it besides its files and its
// window's messages: global memory, locales, menus, GDI's text, the
// clipboard, the display's modes, the file dialogs, other programs, the
// event log. Written from Microsoft's documentation of each
// (docs/common/x86/windows.md); where a game would get a feature Reluva
// doesn't have, it gets the answer Windows gives when it isn't there.

#include "emulation/x86/win32.h"

#include "common/fs.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace reluva::x86 {

namespace {

constexpr uint32_t kLcidEnglishUs = 0x409;
constexpr uint32_t kErrorFileNotFound = 2;
constexpr uint32_t kErrorInvalidHandle = 6;
constexpr uint32_t kErrorInvalidParameter = 87;
constexpr uint32_t kErrorNotSupported = 50;

// A locale's value GetLocaleInfo gives: the code pages, the only ones the C
// runtime asks for; "" for the rest.
std::string localeValue(uint32_t type) {
    switch (type & 0xffff) {
    case 0x1004: // LOCALE_IDEFAULTANSICODEPAGE
        return "1252";
    case 0xb: // LOCALE_IDEFAULTCODEPAGE
        return "437";
    default:
        return {};
    }
}

// CompareString's answer for two strings: CSTR_LESS_THAN (1), CSTR_EQUAL,
// CSTR_GREATER_THAN; NORM_IGNORECASE (1) the only flag heeded.
uint32_t compareStrings(std::string a, std::string b, uint32_t flags) {
    if (flags & 1) {
        a = fs::toLower(a);
        b = fs::toLower(b);
    }
    const int r = a.compare(b);
    return r < 0 ? 1u : r == 0 ? 2u : 3u;
}

} // namespace

void Win32Process::addDesktopStandIns() {
    const char *k = "KERNEL32.dll";
    const char *u = "USER32.dll";
    const char *g = "GDI32.dll";
    const char *a = "ADVAPI32.dll";
    const char *d = "comdlg32.dll";

    // Threads take turns on the emulated CPU, so these needn't be atomic on
    // the host.
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
    // Not in Windows 95's KERNEL32: a program for both asks for it by name.
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

    // Global memory as fixed blocks: a handle is its address, so locking it
    // gives the handle back.
    stdcall(k, "GlobalAlloc", 2, [this](Cpu &c) { return memory().alloc(std::max(c.arg(1), 1u)); });
    stdcall(k, "GlobalFree", 1, [this](Cpu &c) {
        if (c.arg(0))
            memory().free(c.arg(0));
        return 0u;
    });
    stdcall(k, "GlobalLock", 1, [](Cpu &c) { return c.arg(0); });
    stdcall(k, "GlobalUnlock", 1, [](Cpu &) { return 1u; });
    stdcall(k, "GlobalHandle", 1, [](Cpu &c) { return c.arg(0); });
    stdcall(k, "GlobalReAlloc", 3, [this](Cpu &c) {
        const uint32_t old = c.arg(0);
        const uint32_t size = std::max(c.arg(1), 1u);
        const uint32_t fresh = memory().alloc(size);
        if (old && fresh) {
            const uint32_t keep = std::min(size, memory().size(old));
            for (uint32_t i = 0; i < keep; ++i)
                c.write8(fresh + i, c.read8(old + i));
            memory().free(old);
        }
        return fresh;
    });
    stdcall(k, "lstrlenA", 1, [](Cpu &c) { return c.arg(0) ? uint32_t(c.string(c.arg(0)).size()) : 0u; });
    stdcall(k, "SetErrorMode", 1, [](Cpu &) { return 0u; });
    stdcall(k, "SetEnvironmentVariableA", 2, [](Cpu &) { return 1u; });
    // No message text: the caller shows its own, or none.
    stdcall(k, "FormatMessageA", 7, [this](Cpu &) {
        setLastError(kErrorFileNotFound);
        return 0u;
    });
    // One committed, read-write region around the address (MEM_COMMIT,
    // PAGE_READWRITE, MEM_PRIVATE), into a MEMORY_BASIC_INFORMATION.
    const auto query = [](Cpu &c, uint32_t address, uint32_t info, uint32_t size) {
        const uint32_t page = address & ~0xfffu;
        const uint32_t fields[7] = {page, page, 4, 0x1000, 0x1000, 4, 0x20000};
        for (uint32_t i = 0; i < 7 && 4 * i + 4 <= size; ++i)
            c.write32(info + 4 * i, fields[i]);
        return std::min(size, 28u);
    };
    stdcall(k, "VirtualQuery", 3, [query](Cpu &c) { return query(c, c.arg(0), c.arg(1), c.arg(2)); });
    stdcall(k, "VirtualQueryEx", 4, [query](Cpu &c) { return query(c, c.arg(1), c.arg(2), c.arg(3)); });
    // Memory is never paged out.
    stdcall(k, "VirtualLock", 2, [](Cpu &) { return 1u; });

    // The locale: English (United States), code page 1252.
    stdcall(k, "GetUserDefaultLCID", 0, [](Cpu &) { return kLcidEnglishUs; });
    stdcall(k, "IsValidCodePage", 1, [](Cpu &c) { return c.arg(0) == 1252 || c.arg(0) == 437 ? 1u : 0u; });
    stdcall(k, "IsValidLocale", 2, [](Cpu &c) { return (c.arg(0) & 0x3ff) == 0x09 ? 1u : 0u; });
    stdcall(k, "EnumSystemLocalesA", 2, [](Cpu &) { return 1u; });
    // GetLocaleInfoA(lcid, type, buffer, size): the length with its NUL; 0
    // if there's no value or no room.
    stdcall(k, "GetLocaleInfoA", 4, [](Cpu &c) {
        const std::string value = localeValue(c.arg(1));
        if (value.empty() || (c.arg(3) && c.arg(3) < value.size() + 1))
            return 0u;
        if (c.arg(3))
            c.writeString(c.arg(2), value, c.arg(3));
        return uint32_t(value.size() + 1);
    });
    stdcall(k, "GetLocaleInfoW", 4, [](Cpu &c) {
        const std::string value = localeValue(c.arg(1));
        if (value.empty() || (c.arg(3) && c.arg(3) < value.size() + 1))
            return 0u;
        if (c.arg(3))
            for (size_t i = 0; i <= value.size(); ++i)
                c.write16(c.arg(2) + 2 * uint32_t(i), i < value.size() ? uint16_t(uint8_t(value[i])) : 0);
        return uint32_t(value.size() + 1);
    });
    // CompareStringA(lcid, flags, a, aLength, b, bLength): a length of -1
    // runs to the NUL.
    stdcall(k, "CompareStringA", 6, [](Cpu &c) {
        const auto get = [&c](uint32_t at, uint32_t length) {
            return int32_t(length) < 0 ? c.string(at) : c.string(at, length);
        };
        return compareStrings(get(c.arg(2), c.arg(3)), get(c.arg(4), c.arg(5)), c.arg(1));
    });
    stdcall(k, "CompareStringW", 6, [](Cpu &c) {
        const auto get = [&c](uint32_t at, uint32_t length) {
            std::string s;
            for (uint32_t i = 0; int32_t(length) < 0 || i < length; ++i) {
                const uint16_t ch = c.read16(at + 2 * i);
                if (int32_t(length) < 0 && ch == 0)
                    break;
                s += ch < 0x100 ? char(ch) : '?';
            }
            return s;
        };
        return compareStrings(get(c.arg(2), c.arg(3)), get(c.arg(4), c.arg(5)), c.arg(1));
    });

    // No other program runs: not another copy (OpenMutexA finds none),
    // nor one started (CreateProcessA, WinExec), nor one opened.
    stdcall(k, "OpenMutexA", 3, [this](Cpu &) {
        setLastError(kErrorFileNotFound);
        return 0u;
    });
    stdcall(k, "CreateProcessA", 10, [this](Cpu &) {
        setLastError(kErrorFileNotFound);
        return 0u;
    });
    stdcall(k, "WinExec", 2, [](Cpu &) { return kErrorFileNotFound; });
    stdcall(k, "OpenProcess", 3, [this](Cpu &) {
        setLastError(kErrorInvalidParameter);
        return 0u;
    });
    // No serial ports, and no console window to size or colour.
    for (const auto &[name, args] : std::vector<std::pair<const char *, int>>{{"GetCommState", 2},
                                                                              {"SetCommState", 2},
                                                                              {"GetConsoleScreenBufferInfo", 2},
                                                                              {"SetConsoleScreenBufferSize", 2},
                                                                              {"SetConsoleTextAttribute", 2},
                                                                              {"SetConsoleWindowInfo", 3}})
        stdcall(k, name, args, [this](Cpu &) {
            setLastError(kErrorInvalidHandle);
            return 0u;
        });

    // The event log: written to nowhere, as on Windows 95, which hasn't one.
    stdcall(a, "RegisterEventSourceA", 2, [](Cpu &) { return 0x7200u; });
    stdcall(a, "ReportEventA", 9, [](Cpu &) { return 1u; });
    stdcall(a, "DeregisterEventSource", 1, [](Cpu &) { return 1u; });

    // The window's menus: handles that hold nothing, a game being shown
    // full-screen without them.
    stdcall(u, "LoadMenuA", 2, [](Cpu &) { return 0x7100u; });
    stdcall(u, "GetMenu", 1, [](Cpu &) { return 0u; });
    stdcall(u, "GetSystemMenu", 2, [](Cpu &) { return 0x7101u; });
    stdcall(u, "CheckMenuItem", 3, [](Cpu &) { return 0u; });
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
                                                                              {"FillRect", 3},
                                                                              {"ScreenToClient", 2}})
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
    stdcall(u, "IsIconic", 1, [](Cpu &) { return 0u; });
    // The focus and the active window are one: the window given last.
    stdcall(u, "SetFocus", 1, [this](Cpu &c) { return std::exchange(_focus, c.arg(0)); });
    stdcall(u, "SetActiveWindow", 1, [this](Cpu &c) { return std::exchange(_focus, c.arg(0)); });
    stdcall(u, "GetActiveWindow", 0, [this](Cpu &) { return _focus; });
    stdcall(u, "GetDesktopWindow", 0, [](Cpu &) { return 0x10010u; });
    stdcall(u, "GetUpdateRect", 3, [](Cpu &) { return 0u; });
    // BeginPaint(window, PAINTSTRUCT*): its device context, nothing to paint.
    stdcall(u, "BeginPaint", 2, [](Cpu &c) {
        for (uint32_t i = 0; i < 64; i += 4)
            c.write32(c.arg(1) + i, 0);
        c.write32(c.arg(1), 0x5100);
        return 0x5100u;
    });

    // The display's modes: only the one it's in, as the game's own
    // DirectDraw sets the mode it wants.
    stdcall(u, "EnumDisplaySettingsA", 3, [](Cpu &) { return 0u; });
    stdcall(u, "ChangeDisplaySettingsA", 2, [](Cpu &) { return uint32_t(-2); }); // DISP_CHANGE_BADMODE

    // The clipboard: there's none to open, so nothing to paste or copy.
    stdcall(u, "OpenClipboard", 1, [](Cpu &) { return 0u; });
    stdcall(u, "CloseClipboard", 0, [](Cpu &) { return 0u; });
    stdcall(u, "EmptyClipboard", 0, [](Cpu &) { return 0u; });
    stdcall(u, "GetClipboardData", 1, [](Cpu &) { return 0u; });
    stdcall(u, "SetClipboardData", 2, [](Cpu &) { return 0u; });

    // Dialog boxes from a template, and the file dialogs: never shown, as
    // if cancelled (CommDlgExtendedError 0: no error, the user's choice).
    stdcall(u, "DialogBoxIndirectParamA", 5, [this](Cpu &) {
        setLastError(kErrorNotSupported);
        return uint32_t(-1);
    });
    stdcall(u, "EndDialog", 2, [](Cpu &) { return 1u; });
    for (const char *name : {"GetOpenFileNameA", "GetOpenFileNameW", "GetSaveFileNameA", "GetSaveFileNameW"})
        stdcall(d, name, 1, [](Cpu &) { return 0u; });
    stdcall(d, "CommDlgExtendedError", 0, [](Cpu &) { return 0u; });

    // Text and pictures drawn with GDI (a debug console, an error box):
    // accepted, not drawn.
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
    // A 16-pixel font, 8 wide: TEXTMETRIC's height, ascent and average width.
    stdcall(g, "GetTextMetricsA", 2, [](Cpu &c) {
        for (uint32_t i = 0; i < 56; i += 4)
            c.write32(c.arg(1) + i, 0);
        c.write32(c.arg(1), 16);
        c.write32(c.arg(1) + 4, 13);
        c.write32(c.arg(1) + 20, 8);
        return 1u;
    });
    stdcall(g, "GetTextExtentPoint32A", 4, [](Cpu &c) {
        c.write32(c.arg(3), c.arg(2) * 8);
        c.write32(c.arg(3) + 4, 16);
        return 1u;
    });
    // CreateDIBSection(dc, BITMAPINFO*, usage, bits*, section, offset): its
    // pixels' memory, rows rounded to four bytes, given back as the bitmap
    // (selected and drawn into, it isn't shown, as GDI's drawing isn't).
    // No DIB's colours are kept: Get/SetDIBColorTable set none.
    stdcall(g, "CreateDIBSection", 6, [this](Cpu &c) {
        const uint32_t info = c.arg(1);
        if (!c.valid(info, 40) || !c.arg(3)) {
            setLastError(kErrorInvalidParameter);
            return 0u;
        }
        const uint32_t width = c.read32(info + 4);
        const int32_t height = int32_t(c.read32(info + 8));
        const uint32_t bits = c.read16(info + 14);
        const uint64_t row = (uint64_t(width) * bits + 31) / 32 * 4;
        const uint64_t size = row * uint64_t(height < 0 ? -int64_t(height) : height);
        const uint32_t pixels = size && size < (64u << 20) ? memory().alloc(uint32_t(size)) : 0;
        c.write32(c.arg(3), pixels);
        if (!pixels)
            setLastError(8); // ERROR_NOT_ENOUGH_MEMORY
        return pixels;
    });
    for (const char *name : {"GetDIBColorTable", "SetDIBColorTable"})
        stdcall(g, name, 4, [](Cpu &) { return 0u; });

    // Strings, as lstrlenA is: copied and joined, the destination given back.
    stdcall(k, "lstrcpyA", 2, [](Cpu &c) {
        const std::string from = c.string(c.arg(1));
        c.writeString(c.arg(0), from, uint32_t(from.size() + 1));
        return c.arg(0);
    });
    stdcall(k, "lstrcatA", 2, [](Cpu &c) {
        const std::string joined = c.string(c.arg(0)) + c.string(c.arg(1));
        c.writeString(c.arg(0), joined, uint32_t(joined.size() + 1));
        return c.arg(0);
    });
    // Code page 1252 has no double-byte characters.
    stdcall(k, "IsDBCSLeadByte", 1, [](Cpu &) { return 0u; });
    // LocalFree: local memory is global memory, as on Win32.
    stdcall(k, "LocalFree", 1, [this](Cpu &c) {
        if (c.arg(0))
            memory().free(c.arg(0));
        return 0u;
    });
    // Beep(frequency, duration): no speaker; done.
    stdcall(k, "Beep", 2, [](Cpu &) { return 1u; });
    // ExpandEnvironmentStringsA(from, to, size): the process's environment
    // is empty (GetEnvironmentVariableA finds nothing), and Windows leaves a
    // %NAME% it doesn't know as it is, so the text is copied. The length
    // counts the 0; with too small a buffer, nothing's written.
    stdcall(k, "ExpandEnvironmentStringsA", 3, [](Cpu &c) {
        const std::string out = c.string(c.arg(0));
        const uint32_t need = uint32_t(out.size() + 1);
        if (c.arg(1) && c.arg(2) >= need)
            c.writeString(c.arg(1), out, need);
        return need;
    });
    // Named objects aren't kept: none is found to open.
    for (const char *name : {"OpenEventA", "OpenFileMappingA"})
        stdcall(k, name, 3, [this](Cpu &) {
            setLastError(kErrorFileNotFound);
            return 0u;
        });
    // MapViewOfFileEx(mapping, access, offset high, low, bytes, address):
    // MapViewOfFile's where no address is asked; at one, it can't.
    stdcall(k, "MapViewOfFileEx", 6, [this](Cpu &c) {
        if (c.arg(5)) {
            setLastError(487); // ERROR_INVALID_ADDRESS
            return 0u;
        }
        uint32_t view = 0;
        if (c.call(provided("KERNEL32.dll", "MapViewOfFile"), {c.arg(0), c.arg(1), c.arg(2), c.arg(3), c.arg(4)}, view))
            return 0u;
        return view;
    });
    // A debugger's (a crash handler's): this process's own memory read, no
    // thread's registers.
    stdcall(k, "ReadProcessMemory", 5, [this](Cpu &c) {
        const bool own = c.arg(0) == 0xffffffffu;
        const uint32_t from = c.arg(1);
        const uint32_t n = c.arg(3);
        if (!own || !c.valid(from, n) || !c.valid(c.arg(2), n)) {
            setLastError(299); // ERROR_PARTIAL_COPY
            if (c.arg(4))
                c.write32(c.arg(4), 0);
            return 0u;
        }
        std::copy_n(c.memory().data() + from, n, c.memory().data() + c.arg(2));
        if (c.arg(4))
            c.write32(c.arg(4), n);
        return 1u;
    });
    stdcall(k, "GetThreadContext", 2, [this](Cpu &) {
        setLastError(kErrorNotSupported);
        return 0u;
    });

    // SystemParametersInfoA(action, value, data, ini): the desktop's work
    // area, the whole screen; no screen saver; others refused.
    stdcall(u, "SystemParametersInfoA", 4, [this](Cpu &c) {
        switch (c.arg(0)) {
        case 0x30: // SPI_GETWORKAREA
            if (!c.valid(c.arg(2), 16))
                return 0u;
            c.write32(c.arg(2), 0);
            c.write32(c.arg(2) + 4, 0);
            c.write32(c.arg(2) + 8, uint32_t(screenWidth));
            c.write32(c.arg(2) + 12, uint32_t(screenHeight));
            return 1u;
        case 0x10: // SPI_GETSCREENSAVEACTIVE
            if (c.valid(c.arg(2), 4))
                c.write32(c.arg(2), 0);
            return 1u;
        case 0x11: // SPI_SETSCREENSAVEACTIVE
            return 1u;
        default:
            setLastError(kErrorInvalidParameter);
            return 0u;
        }
    });
}

} // namespace reluva::x86
