// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's windows past making them (win32_user.cpp): their parents, IDs,
// text and longs, focus and capture, painting, timers and GetMessageA, the
// system's parameters, and the program's string table, as Microsoft
// documents them.

#include "emulation/x86/win32.h"

#include "emulation/x86/gdi.h"

#include "common/pe.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>

namespace reluva::x86 {

namespace {

constexpr uint32_t kWmPaint = 0x000f;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmTimer = 0x0113;
constexpr uint32_t kDesktop = 0x10000;

} // namespace

bool Win32Process::timerDue() const {
    const uint64_t now = milliseconds();
    return std::any_of(_windowTimers.begin(), _windowTimers.end(),
                       [now](const WindowTimer &t) { return t.dueMs <= now; });
}

std::optional<Win32Process::Message> Win32Process::takeDueTimer(uint32_t window) {
    const uint64_t now = milliseconds();
    WindowTimer *soonest = nullptr;
    for (WindowTimer &t : _windowTimers)
        if ((window == 0 || t.window == window) && t.dueMs <= now && (!soonest || t.dueMs < soonest->dueMs))
            soonest = &t;
    if (!soonest)
        return std::nullopt;
    // Ticks missed while the program was busy make one message, as Windows'.
    soonest->dueMs = now + soonest->intervalMs;
    return Message{soonest->window, kWmTimer, soonest->id, soonest->proc};
}

void Win32Process::addWindowStandIns() {
    const char *u = "USER32.dll";
    auto found = [this](uint32_t h) -> Window * {
        auto it = _windows.find(h);
        return it == _windows.end() ? nullptr : &it->second;
    };

    stdcall(u, "GetDesktopWindow", 0, [](Cpu &) { return kDesktop; });
    stdcall(u, "GetParent", 1, [found](Cpu &c) {
        const Window *w = found(c.arg(0));
        return w ? w->parent : 0u;
    });
    stdcall(u, "IsWindowVisible", 1, [found](Cpu &c) {
        const Window *w = found(c.arg(0));
        return w && w->visible ? 1u : 0u;
    });
    // EnableWindow(window, enable): whether it was disabled before.
    stdcall(u, "EnableWindow", 2, [found](Cpu &c) {
        Window *w = found(c.arg(0));
        if (!w)
            return 0u;
        const bool wasDisabled = !w->enabled;
        w->enabled = c.arg(1) != 0;
        return wasDisabled ? 1u : 0u;
    });
    for (const char *name : {"BringWindowToTop", "SetForegroundWindow"})
        stdcall(u, name, 1, [found](Cpu &c) { return found(c.arg(0)) ? 1u : 0u; });
    stdcall(u, "SetFocus", 1, [this](Cpu &c) {
        const uint32_t before = _focus;
        _focus = c.arg(0);
        return before;
    });
    stdcall(u, "SetCapture", 1, [this](Cpu &c) {
        const uint32_t before = _capture;
        _capture = c.arg(0);
        return before;
    });
    stdcall(u, "ReleaseCapture", 0, [this](Cpu &) {
        _capture = 0;
        return 1u;
    });
    stdcall(u, "GetCapture", 0, [this](Cpu &) { return _capture; });
    stdcall(u, "ScreenToClient", 2, [this](Cpu &c) {
        const auto area = clientArea(c.arg(0));
        if (!area || !c.valid(c.arg(1), 8))
            return 0u;
        c.write32(c.arg(1), c.read32(c.arg(1)) - uint32_t((*area)[0]));
        c.write32(c.arg(1) + 4, c.read32(c.arg(1) + 4) - uint32_t((*area)[1]));
        return 1u;
    });
    // WindowFromPoint(point by value).
    stdcall(u, "WindowFromPoint", 2, [this](Cpu &c) { return windowAt(int32_t(c.arg(0)), int32_t(c.arg(1))); });
    stdcall(u, "MoveWindow", 6, [this, found](Cpu &c) {
        if (!found(c.arg(0)))
            return 0u;
        moveWindow(c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)));
        return 1u;
    });

    // Longs: GWL_WNDPROC -4, GWL_HWNDPARENT -8, GWL_ID -12, GWL_STYLE -16,
    // GWL_EXSTYLE -20; the rest (GWL_USERDATA, a class's extra bytes, a
    // dialog's DWL_*) kept as set.
    stdcall(u, "GetWindowLongA", 2, [found](Cpu &c) {
        const Window *w = found(c.arg(0));
        if (!w)
            return 0u;
        switch (int32_t(c.arg(1))) {
        case -4:
            return w->proc;
        case -8:
            return w->parent;
        case -12:
            return w->id;
        case -16:
            return w->style;
        case -20:
            return w->exStyle;
        default: {
            const auto it = w->longs.find(int32_t(c.arg(1)));
            return it == w->longs.end() ? 0u : it->second;
        }
        }
    });
    stdcall(u, "SetWindowLongA", 3, [found](Cpu &c) {
        Window *w = found(c.arg(0));
        if (!w)
            return 0u;
        uint32_t *field = nullptr;
        switch (int32_t(c.arg(1))) {
        case -4:
            field = &w->proc;
            break;
        case -8:
            field = &w->parent;
            break;
        case -12:
            field = &w->id;
            break;
        case -16:
            field = &w->style;
            break;
        case -20:
            field = &w->exStyle;
            break;
        default:
            field = &w->longs[int32_t(c.arg(1))];
            break;
        }
        const uint32_t before = *field;
        *field = c.arg(2);
        return before;
    });
    // CallWindowProcA(procedure, window, message, wParam, lParam).
    stdcall(u, "CallWindowProcA", 5, [this](Cpu &c) {
        uint32_t result = 0;
        if (Error e = _cpu.call(c.arg(0), {c.arg(1), c.arg(2), c.arg(3), c.arg(4)}, result); e && output)
            output("A window procedure failed: " + e.message + "\n");
        return result;
    });
    stdcall(u, "SetWindowTextA", 2, [found](Cpu &c) {
        Window *w = found(c.arg(0));
        if (!w)
            return 0u;
        w->title = c.arg(1) ? c.string(c.arg(1)) : "";
        return 1u;
    });
    // GetWindowTextA(window, buffer, size): the characters copied, cut to fit.
    stdcall(u, "GetWindowTextA", 3, [found](Cpu &c) {
        const Window *w = found(c.arg(0));
        const uint32_t size = c.arg(2);
        if (!w || size == 0)
            return 0u;
        const std::string text = w->title.substr(0, size - 1);
        c.writeString(c.arg(1), text, uint32_t(text.size() + 1));
        return uint32_t(text.size());
    });

    // Painting: an invalidated window gets one WM_PAINT; until it's
    // painted, its update rectangle is its whole client area.
    stdcall(u, "InvalidateRect", 3, [this, found](Cpu &c) {
        const uint32_t h = c.arg(0);
        if (h && !found(h))
            return 0u;
        const uint32_t target = h ? h : mainWindow();
        const bool queued = std::any_of(_messages.begin(), _messages.end(),
                                        [&](const Message &m) { return m.window == target && m.message == kWmPaint; });
        if (target && !queued)
            postMessage(target, kWmPaint, 0, 0);
        return 1u;
    });
    stdcall(u, "GetUpdateRect", 3, [this](Cpu &c) {
        const uint32_t h = c.arg(0);
        const bool invalid = std::any_of(_messages.begin(), _messages.end(),
                                         [&](const Message &m) { return m.window == h && m.message == kWmPaint; });
        const auto area = clientArea(h);
        if (c.arg(1) && c.valid(c.arg(1), 16)) {
            c.write32(c.arg(1), 0);
            c.write32(c.arg(1) + 4, 0);
            c.write32(c.arg(1) + 8, invalid && area ? uint32_t((*area)[2]) : 0u);
            c.write32(c.arg(1) + 12, invalid && area ? uint32_t((*area)[3]) : 0u);
        }
        return invalid ? 1u : 0u;
    });

    // Timers: SetTimer(window, id, ms, procedure) gives the id (a new one
    // without a window); WM_TIMER's lParam is the procedure, which
    // DispatchMessageA calls if there is one.
    stdcall(u, "SetTimer", 4, [this](Cpu &c) {
        const uint32_t window = c.arg(0);
        uint32_t id = c.arg(1);
        if (!window)
            id = _nextTimerId++;
        _windowTimers.erase(std::remove_if(_windowTimers.begin(), _windowTimers.end(),
                                           [&](const WindowTimer &t) { return t.window == window && t.id == id; }),
                            _windowTimers.end());
        const uint32_t ms = std::max(c.arg(2), 10u);
        _windowTimers.push_back({window, id, ms, uint64_t(milliseconds()) + ms, c.arg(3)});
        return id ? id : 1u;
    });
    stdcall(u, "KillTimer", 2, [this](Cpu &c) {
        const size_t before = _windowTimers.size();
        _windowTimers.erase(
            std::remove_if(_windowTimers.begin(), _windowTimers.end(),
                           [&](const WindowTimer &t) { return t.window == c.arg(0) && t.id == c.arg(1); }),
            _windowTimers.end());
        return _windowTimers.size() < before ? 1u : 0u;
    });
    // GetMessageA(message, window, first, last): waits for one (with host
    // threads, letting the others run; else running the turns and idle);
    // 0 for WM_QUIT.
    stdcall(u, "GetMessageA", 4, [this](Cpu &c) {
        const uint32_t out = c.arg(0);
        const uint32_t hwnd = c.arg(1);
        const uint32_t first = c.arg(2);
        const uint32_t last = c.arg(3);
        auto matches = [&](const Message &m) {
            return (hwnd == 0 || m.window == hwnd || m.message == kWmQuit) &&
                   ((first == 0 && last == 0) || (m.message >= first && m.message <= last));
        };
        for (;;) {
            repeatScroll();
            auto it = std::find_if(_messages.begin(), _messages.end(), matches);
            std::optional<Message> m;
            if (it != _messages.end()) {
                m = *it;
                _messages.erase(it);
            } else {
                m = takeDueTimer(hwnd);
            }
            if (m) {
                const uint32_t fields[7] = {m->window,      m->message,        m->wParam,        m->lParam,
                                            milliseconds(), uint32_t(cursorX), uint32_t(cursorY)};
                for (int i = 0; i < 7; ++i)
                    c.write32(out + 4 * uint32_t(i), fields[i]);
                return m->message == kWmQuit ? 0u : 1u;
            }
            if (idle)
                idle();
            if (std::any_of(_messages.begin(), _messages.end(), matches))
                continue;
            if (hostThreads()) {
                uint64_t next = UINT64_MAX;
                for (const WindowTimer &t : _windowTimers)
                    next = std::min(next, t.dueMs);
                const uint64_t now = milliseconds();
                const uint32_t timeout =
                    next == UINT64_MAX ? 10u : uint32_t(next > now ? std::min<uint64_t>(next - now, 10) : 0);
                if (!wait([&] { return !_messages.empty() || timerDue() || scrollRepeatDue(); }, timeout) && ending())
                    return 0u;
            } else {
                runThreads();
                _slept += 1;
            }
        }
    });

    // SystemParametersInfoA(action, parameter, pointer, flags): the work
    // area is the screen; the screen saver isn't on; settings are taken.
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
        case 0x03: // SPI_GETMOUSE: the two thresholds and the acceleration, Windows' defaults
            if (!c.valid(c.arg(2), 12))
                return 0u;
            c.write32(c.arg(2), 6);
            c.write32(c.arg(2) + 4, 10);
            c.write32(c.arg(2) + 8, 1);
            return 1u;
        case 0x04: // SPI_SETMOUSE
        case 0x10: // SPI_GETSCREENSAVEACTIVE
        case 0x0e: // SPI_GETSCREENSAVETIMEOUT
            if (c.valid(c.arg(2), 4))
                c.write32(c.arg(2), 0);
            return 1u;
        case 0x11: // SPI_SETSCREENSAVEACTIVE
        case 0x0f: // SPI_SETSCREENSAVETIMEOUT
        case 0x61: // SPI_SCREENSAVERRUNNING
            return 1u;
        default:
            if (output)
                output("SystemParametersInfoA: action " + std::to_string(c.arg(0)) + " isn't known here\n");
            return 0u;
        }
    });

    // LoadStringA(instance, id, buffer, size): the program's string table,
    // read from its image in memory; the characters copied, cut to fit.
    auto tables = std::make_shared<std::map<const Module *, std::map<uint32_t, std::string>>>();
    stdcall(u, "LoadStringA", 4, [this, tables](Cpu &c) {
        const Module *m = moduleAt(c.arg(0));
        if (!m)
            m = modules().empty() ? nullptr : modules().back();
        if (!m || c.arg(3) == 0)
            return 0u;
        auto it = tables->find(m);
        if (it == tables->end()) {
            // The image as a file: its headers and sections are where its
            // file offsets say once laid out as the loader lays them.
            std::vector<uint8_t> image(c.memory().begin() + m->base, c.memory().begin() + m->base + m->size);
            PeImage flat = m->image;
            for (auto &s : flat.sections) {
                s.rawOffset = s.virtualAddress;
                s.rawSize = std::max(s.rawSize, s.virtualSize);
            }
            std::map<uint32_t, std::string> strings;
            readPeStrings(image, flat, strings);
            it = tables->emplace(m, std::move(strings)).first;
        }
        const auto s = it->second.find(c.arg(1));
        if (s == it->second.end()) {
            c.write8(c.arg(2), 0);
            return 0u;
        }
        const std::string text = s->second.substr(0, c.arg(3) - 1);
        c.writeString(c.arg(2), text, uint32_t(text.size() + 1));
        return uint32_t(text.size());
    });

    // Scroll bars, a window's own (SB_HORZ 0, SB_VERT 1, SB_BOTH 3; a
    // scroll bar control's, SB_CTL 2, aren't kept): shown or hidden with
    // the window's style, which changes its client area (WM_SIZE), and
    // drawn on its frame.
    auto repaintFrame = [this](uint32_t h) {
        if (shown(h))
            _gdi->paintFrame(h);
    };
    stdcall(u, "ShowScrollBar", 3, [this, found, repaintFrame](Cpu &c) {
        Window *w = found(c.arg(0));
        const uint32_t bar = c.arg(1);
        if (!w || bar == 2)
            return 0u;
        const uint32_t bits = bar == 0 ? 0x00100000u : bar == 1 ? 0x00200000u : 0x00300000u; // WS_HSCROLL, WS_VSCROLL
        const auto before = clientArea(c.arg(0));
        w->style = c.arg(2) ? w->style | bits : w->style & ~bits;
        const auto after = clientArea(c.arg(0));
        if (before && after && ((*before)[2] != (*after)[2] || (*before)[3] != (*after)[3]))
            sendMessage(c.arg(0), 0x0005, 0, uint32_t((*after)[2] & 0xffff) | uint32_t((*after)[3] & 0xffff) << 16);
        repaintFrame(c.arg(0));
        return 1u;
    });
    // SetScrollRange(window, bar, min, max, redraw).
    stdcall(u, "SetScrollRange", 5, [found, repaintFrame](Cpu &c) {
        Window *w = found(c.arg(0));
        if (!w || c.arg(1) > 1)
            return 0u;
        Window::ScrollBar &b = w->scroll[c.arg(1)];
        b.min = int32_t(c.arg(2));
        b.max = int32_t(c.arg(3));
        b.pos = std::clamp(b.pos, b.min, std::max(b.min, b.max));
        if (c.arg(4))
            repaintFrame(c.arg(0));
        return 1u;
    });
    // SetScrollPos(window, bar, pos, redraw): the position before.
    stdcall(u, "SetScrollPos", 4, [found, repaintFrame](Cpu &c) {
        Window *w = found(c.arg(0));
        if (!w || c.arg(1) > 1)
            return 0u;
        Window::ScrollBar &b = w->scroll[c.arg(1)];
        const int before = b.pos;
        b.pos = std::clamp(int32_t(c.arg(2)), b.min, std::max(b.min, b.max));
        if (c.arg(3))
            repaintFrame(c.arg(0));
        return uint32_t(before);
    });
    stdcall(u, "GetScrollPos", 2, [found](Cpu &c) {
        Window *w = found(c.arg(0));
        return w && c.arg(1) <= 1 ? uint32_t(w->scroll[c.arg(1)].pos) : 0u;
    });
    // GetScrollRange(window, bar, *min, *max).
    stdcall(u, "GetScrollRange", 4, [found](Cpu &c) {
        Window *w = found(c.arg(0));
        if (!w || c.arg(1) > 1 || !c.valid(c.arg(2), 4) || !c.valid(c.arg(3), 4))
            return 0u;
        c.write32(c.arg(2), uint32_t(w->scroll[c.arg(1)].min));
        c.write32(c.arg(3), uint32_t(w->scroll[c.arg(1)].max));
        return 1u;
    });
}

} // namespace reluva::x86
