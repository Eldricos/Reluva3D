// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's windows and messages, and the little of GDI32 that's asked for,
// as Microsoft documents them. Nothing is drawn: a window is a record of its
// class's procedure, place and size, which the engine shows however it
// likes. Messages the engine posts (its input) wait in one queue, which
// PeekMessageA reads and DispatchMessageA hands to the window's procedure.
// The sizes of frames and captions are Windows 98's at 96 dpi.

#include "common/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kWmCreate = 0x1, kWmDestroy = 0x2, kWmActivate = 0x6, kWmSetFocus = 0x7, kWmClose = 0x10, kWmQuit = 0x12,
    kWmShowWindow = 0x18, kWmActivateApp = 0x1c, kWmNcCreate = 0x81, kWmNcDestroy = 0x82, kWmKeyDown = 0x100,
    kWmChar = 0x102, kWmSysCommand = 0x112
};
constexpr uint32_t kScClose = 0xf060;
constexpr uint32_t kWsBorder = 0x00800000, kWsDlgFrame = 0x00400000, kWsCaption = 0x00c00000,
                   kWsThickFrame = 0x00040000, kWsVisible = 0x10000000, kWsChild = 0x40000000;
constexpr uint32_t kCwUseDefault = 0x80000000u;
constexpr int kCaption = 19, kMenu = 19, kFixedFrame = 3, kSizeFrame = 4, kBorder = 1;
// Handles that are only ever passed back: icons and cursors, GDI's stock
// objects, device contexts.
constexpr uint32_t kResourceHandles = 0x20000, kStockObjects = 0x28000, kDeviceContext = 0x30000;

std::string lower(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// How far the frame (and caption, and menu) reach in from each side of a
// window with this style: left, top, right, bottom.
struct Insets {
    int left, top, right, bottom;
};
Insets frameOf(uint32_t style, bool menu) {
    int edge = 0;
    if (style & kWsThickFrame)
        edge = kSizeFrame;
    else if ((style & kWsCaption) == kWsCaption || (style & kWsDlgFrame))
        edge = kFixedFrame;
    else if (style & kWsBorder)
        edge = kBorder;
    const int top = edge + ((style & kWsCaption) == kWsCaption ? kCaption : 0) + (menu ? kMenu : 0);
    return {edge, top, edge, edge};
}

} // namespace

const Win32Process::Window* Win32Process::window(uint32_t handle) const {
    auto it = windows_.find(handle);
    return it == windows_.end() ? nullptr : &it->second;
}

uint32_t Win32Process::mainWindow() const { return windows_.empty() ? 0 : windows_.begin()->first; }

void Win32Process::postMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam) {
    messages_.push_back({window, message, wParam, lParam});
}

uint32_t Win32Process::sendMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam) {
    auto it = windows_.find(window);
    if (it == windows_.end() || !it->second.proc)
        return 0;
    uint32_t result = 0;
    // From inside the program, a fault goes on up through call(); only a
    // call from the engine itself sees it here.
    if (Error e = cpu_.call(it->second.proc, {window, message, wParam, lParam}, result)) {
        if (output)
            output("A window procedure failed: " + e.message + "\n");
        return 0;
    }
    return result;
}

void Win32Process::addUserStandIns() {
    const char* u = "USER32.dll";
    const char* g = "GDI32.dll";
    auto rect = [](Cpu& c, uint32_t p, int left, int top, int right, int bottom) {
        c.write32(p, uint32_t(left));
        c.write32(p + 4, uint32_t(top));
        c.write32(p + 8, uint32_t(right));
        c.write32(p + 12, uint32_t(bottom));
    };

    // --- Classes and windows
    // RegisterClassA(WNDCLASSA*): the procedure is at +4, the name at +36.
    stdcall(u, "RegisterClassA", 1, [this](Cpu& c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 40))
            return 0u;
        const std::string name = lower(c.string(c.read32(p + 36)));
        if (name.empty() || classes_.count(name))
            return 0u;
        classes_[name] = c.read32(p + 4);
        return uint32_t(0xc000 + classes_.size()); // an atom
    });
    // CreateWindowExA: WM_NCCREATE then WM_CREATE go to the procedure, with
    // a CREATESTRUCTA; either saying no destroys the window again.
    stdcall(u, "CreateWindowExA", 12, [this](Cpu& c) {
        const uint32_t exStyle = c.arg(0), className = c.arg(1), title = c.arg(2), style = c.arg(3);
        const uint32_t param = c.arg(11), instance = c.arg(10), menu = c.arg(9), parent = c.arg(8);
        int x = int32_t(c.arg(4)), y = int32_t(c.arg(5)), w = int32_t(c.arg(6)), h = int32_t(c.arg(7));
        auto cls = classes_.find(lower(className < 0x10000 ? "" : c.string(className)));
        if (cls == classes_.end()) {
            setLastError(1407); // ERROR_CANNOT_FIND_WND_CLASS
            return 0u;
        }
        if (uint32_t(x) == kCwUseDefault)
            x = y = 0;
        if (uint32_t(w) == kCwUseDefault) {
            w = screenWidth;
            h = screenHeight;
        }
        const uint32_t handle = nextWindow_;
        nextWindow_ += 4;
        Window& win = windows_[handle];
        win.className = c.string(className);
        win.title = title ? c.string(title) : std::string();
        win.proc = cls->second;
        win.style = style & ~kWsVisible;
        win.exStyle = exStyle;
        win.x = x, win.y = y, win.width = w, win.height = h;

        const uint32_t create = memory().alloc(48); // CREATESTRUCTA
        const uint32_t fields[12] = {param, instance, menu, parent, uint32_t(h), uint32_t(w), uint32_t(y),
                                     uint32_t(x), style, title, className, exStyle};
        for (int i = 0; i < 12; ++i)
            c.write32(create + 4 * uint32_t(i), fields[i]);
        const bool made = sendMessage(handle, kWmNcCreate, 0, create) != 0 &&
                          int32_t(sendMessage(handle, kWmCreate, 0, create)) != -1;
        memory().free(create);
        if (!made) {
            windows_.erase(handle);
            return 0u;
        }
        if (style & kWsVisible)
            windows_[handle].visible = true;
        return handle;
    });
    stdcall(u, "DestroyWindow", 1, [this](Cpu& c) {
        const uint32_t handle = c.arg(0);
        if (!windows_.count(handle))
            return 0u;
        sendMessage(handle, kWmDestroy, 0, 0);
        sendMessage(handle, kWmNcDestroy, 0, 0);
        windows_.erase(handle);
        return 1u;
    });
    // FindWindowA(class, title): either may be 0, matching any.
    stdcall(u, "FindWindowA", 2, [this](Cpu& c) {
        const std::string cls = c.arg(0) >= 0x10000 ? lower(c.string(c.arg(0))) : std::string();
        const std::string title = c.arg(1) ? c.string(c.arg(1)) : std::string();
        for (const auto& [handle, w] : windows_)
            if ((cls.empty() || lower(w.className) == cls) && (!c.arg(1) || w.title == title))
                return handle;
        return 0u;
    });
    // ShowWindow: the first time it's shown, the window is activated and the
    // application with it (WM_ACTIVATEAPP, WM_ACTIVATE, WM_SETFOCUS), as the
    // foreground window's is. Returns whether it was visible.
    stdcall(u, "ShowWindow", 2, [this](Cpu& c) {
        auto it = windows_.find(c.arg(0));
        if (it == windows_.end())
            return 0u;
        const uint32_t handle = it->first;
        const bool was = it->second.visible, show = c.arg(1) != 0; // SW_HIDE is 0
        it->second.visible = show;
        if (show != was)
            sendMessage(handle, kWmShowWindow, show, 0);
        if (show && !was && !(it->second.style & kWsChild)) {
            sendMessage(handle, kWmActivateApp, 1, 0);
            sendMessage(handle, kWmActivate, 1, 0); // WA_ACTIVE
            sendMessage(handle, kWmSetFocus, 0, 0);
        }
        return was ? 1u : 0u;
    });
    stdcall(u, "UpdateWindow", 1, [this](Cpu& c) { return windows_.count(c.arg(0)) ? 1u : 0u; });
    // SetWindowPos(window, after, x, y, cx, cy, flags): SWP_NOSIZE 1,
    // SWP_NOMOVE 2, SWP_SHOWWINDOW 0x40, SWP_HIDEWINDOW 0x80.
    stdcall(u, "SetWindowPos", 7, [this](Cpu& c) {
        auto it = windows_.find(c.arg(0));
        if (it == windows_.end())
            return 0u;
        Window& w = it->second;
        const uint32_t flags = c.arg(6);
        if (!(flags & 2))
            w.x = int32_t(c.arg(2)), w.y = int32_t(c.arg(3));
        if (!(flags & 1))
            w.width = int32_t(c.arg(4)), w.height = int32_t(c.arg(5));
        if (flags & 0x40)
            w.visible = true;
        if (flags & 0x80)
            w.visible = false;
        return 1u;
    });
    stdcall(u, "GetWindowRect", 2, [this, rect](Cpu& c) {
        const Window* w = window(c.arg(0));
        if (!w || !c.valid(c.arg(1), 16))
            return 0u;
        rect(c, c.arg(1), w->x, w->y, w->x + w->width, w->y + w->height);
        return 1u;
    });
    stdcall(u, "GetClientRect", 2, [this, rect](Cpu& c) {
        const Window* w = window(c.arg(0));
        if (!w || !c.valid(c.arg(1), 16))
            return 0u;
        const Insets f = frameOf(w->style, false);
        rect(c, c.arg(1), 0, 0, std::max(0, w->width - f.left - f.right), std::max(0, w->height - f.top - f.bottom));
        return 1u;
    });
    stdcall(u, "ClientToScreen", 2, [this](Cpu& c) {
        const Window* w = window(c.arg(0));
        if (!w || !c.valid(c.arg(1), 8))
            return 0u;
        const Insets f = frameOf(w->style, false);
        c.write32(c.arg(1), c.read32(c.arg(1)) + uint32_t(w->x + f.left));
        c.write32(c.arg(1) + 4, c.read32(c.arg(1) + 4) + uint32_t(w->y + f.top));
        return 1u;
    });
    // AdjustWindowRect(rect, style, menu): the window's rectangle for a
    // client rectangle.
    stdcall(u, "AdjustWindowRect", 3, [](Cpu& c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 16))
            return 0u;
        const Insets f = frameOf(c.arg(1), c.arg(2) != 0);
        c.write32(p, c.read32(p) - uint32_t(f.left));
        c.write32(p + 4, c.read32(p + 4) - uint32_t(f.top));
        c.write32(p + 8, c.read32(p + 8) + uint32_t(f.right));
        c.write32(p + 12, c.read32(p + 12) + uint32_t(f.bottom));
        return 1u;
    });
    stdcall(u, "GetSystemMetrics", 1, [this](Cpu& c) {
        switch (c.arg(0)) {
        case 0: return uint32_t(screenWidth);  // SM_CXSCREEN
        case 1: return uint32_t(screenHeight); // SM_CYSCREEN
        case 4: return uint32_t(kCaption);     // SM_CYCAPTION
        case 5:
        case 6: return uint32_t(kBorder); // SM_CXBORDER, SM_CYBORDER
        case 7:
        case 8: return uint32_t(kFixedFrame); // SM_CXFIXEDFRAME, SM_CYFIXEDFRAME
        case 15: return uint32_t(kMenu);      // SM_CYMENU
        case 16: return uint32_t(screenWidth);                        // SM_CXFULLSCREEN
        case 17: return uint32_t(screenHeight - kCaption);            // SM_CYFULLSCREEN
        case 32:
        case 33: return uint32_t(kSizeFrame); // SM_CXFRAME, SM_CYFRAME
        case 43: return 3u;                   // SM_CMOUSEBUTTONS
        default: return 0u;
        }
    });

    // --- Messages
    stdcall(u, "PostMessageA", 4, [this](Cpu& c) {
        postMessage(c.arg(0), c.arg(1), c.arg(2), c.arg(3));
        return 1u;
    });
    stdcall(u, "SendMessageA", 4, [this](Cpu& c) { return sendMessage(c.arg(0), c.arg(1), c.arg(2), c.arg(3)); });
    stdcall(u, "PostQuitMessage", 1, [this](Cpu& c) {
        postMessage(0, kWmQuit, c.arg(0), 0);
        return 0u;
    });
    // PeekMessageA(msg, window, first, last, remove): the oldest message for
    // the window (0: any) in the range (0, 0: any). WM_QUIT is for any window.
    stdcall(u, "PeekMessageA", 5, [this](Cpu& c) {
        const uint32_t out = c.arg(0), hwnd = c.arg(1), first = c.arg(2), last = c.arg(3), remove = c.arg(4);
        auto matches = [&](const Message& m) {
            return (hwnd == 0 || m.window == hwnd || m.message == kWmQuit) &&
                   ((first == 0 && last == 0) || (m.message >= first && m.message <= last));
        };
        auto it = std::find_if(messages_.begin(), messages_.end(), matches);
        if (it == messages_.end() && idle) {
            idle();
            it = std::find_if(messages_.begin(), messages_.end(), matches);
        }
        if (it == messages_.end() || !c.valid(out, 28))
            return 0u;
        const uint32_t fields[7] = {it->window, it->message, it->wParam, it->lParam, milliseconds(),
                                    uint32_t(cursorX), uint32_t(cursorY)};
        for (int i = 0; i < 7; ++i)
            c.write32(out + 4 * uint32_t(i), fields[i]);
        if (remove & 1) // PM_REMOVE
            messages_.erase(it);
        return 1u;
    });
    stdcall(u, "DispatchMessageA", 1, [this](Cpu& c) {
        const uint32_t m = c.arg(0);
        if (!c.valid(m, 16))
            return 0u;
        return sendMessage(c.read32(m), c.read32(m + 4), c.read32(m + 8), c.read32(m + 12));
    });
    // TranslateMessage: a key that types a character (letters, digits,
    // space, Enter, Backspace, Escape, Tab) queues WM_CHAR for it.
    stdcall(u, "TranslateMessage", 1, [this](Cpu& c) {
        const uint32_t m = c.arg(0);
        if (!c.valid(m, 16) || c.read32(m + 4) != kWmKeyDown)
            return 0u;
        const uint32_t key = c.read32(m + 8);
        uint32_t ch = 0;
        if (key >= 'A' && key <= 'Z')
            ch = key - 'A' + 'a';
        else if ((key >= '0' && key <= '9') || key == ' ' || key == 8 || key == 9 || key == 13 || key == 27)
            ch = key;
        if (!ch)
            return 0u;
        postMessage(c.read32(m), kWmChar, ch, c.read32(m + 12));
        return 1u;
    });
    stdcall(u, "TranslateAcceleratorA", 3, [](Cpu&) { return 0u; });
    // DefWindowProcA: closing destroys the window (WM_CLOSE, and the close
    // command); WM_NCCREATE goes on; everything else needs nothing.
    stdcall(u, "DefWindowProcA", 4, [this](Cpu& c) {
        const uint32_t handle = c.arg(0), message = c.arg(1), wParam = c.arg(2);
        if (message == kWmNcCreate)
            return 1u;
        if (message == kWmSysCommand && (wParam & 0xfff0) == kScClose)
            return sendMessage(handle, kWmClose, 0, 0);
        if (message == kWmClose && windows_.count(handle)) {
            sendMessage(handle, kWmDestroy, 0, 0);
            sendMessage(handle, kWmNcDestroy, 0, 0);
            windows_.erase(handle);
        }
        return 0u;
    });
    // MessageBoxA(window, text, caption, type): reported, and answered with
    // the button that lets the program go on: OK, Yes, or Cancel/Abort
    // where there's no OK.
    stdcall(u, "MessageBoxA", 4, [this](Cpu& c) {
        const std::string text = c.arg(1) ? c.string(c.arg(1)) : std::string();
        const std::string caption = c.arg(2) ? c.string(c.arg(2)) : std::string("Error");
        if (messageBox)
            messageBox(caption, text);
        else if (output)
            output("[" + caption + "] " + text + "\n");
        switch (c.arg(3) & 0xf) {
        case 2: return 3u;  // MB_ABORTRETRYIGNORE: IDABORT
        case 3:             // MB_YESNOCANCEL
        case 4: return 6u;  // MB_YESNO: IDYES
        case 5: return 2u;  // MB_RETRYCANCEL: IDCANCEL
        default: return 1u; // IDOK
        }
    });

    // --- Icons, cursors and the mouse pointer
    stdcall(u, "LoadIconA", 2, [](Cpu& c) { return kResourceHandles + (c.arg(1) & 0xffff); });
    stdcall(u, "LoadCursorA", 2, [](Cpu& c) { return kResourceHandles + (c.arg(1) & 0xffff); });
    stdcall(u, "SetCursor", 1, [](Cpu&) { return 0u; });
    stdcall(u, "GetCursorPos", 1, [this](Cpu& c) {
        if (!c.valid(c.arg(0), 8))
            return 0u;
        c.write32(c.arg(0), uint32_t(cursorX));
        c.write32(c.arg(0) + 4, uint32_t(cursorY));
        return 1u;
    });
    stdcall(u, "SetCursorPos", 2, [this](Cpu& c) {
        cursorX = std::clamp(int(int32_t(c.arg(0))), 0, screenWidth - 1);
        cursorY = std::clamp(int(int32_t(c.arg(1))), 0, screenHeight - 1);
        return 1u;
    });
    // ShowCursor(show): the display count after counting up or down.
    stdcall(u, "ShowCursor", 1, [this](Cpu& c) { return uint32_t(cursorShown_ += c.arg(0) ? 1 : -1); });
    stdcall(u, "ClipCursor", 1, [this](Cpu& c) {
        if (c.arg(0) && c.valid(c.arg(0), 16)) {
            const uint32_t p = c.arg(0);
            cursorX = std::clamp(cursorX, int(int32_t(c.read32(p))), int(int32_t(c.read32(p + 8))) - 1);
            cursorY = std::clamp(cursorY, int(int32_t(c.read32(p + 4))), int(int32_t(c.read32(p + 12))) - 1);
        }
        return 1u;
    });

    // --- Device contexts
    stdcall(u, "GetDC", 1, [](Cpu& c) { return kDeviceContext + (c.arg(0) & 0xffff); });
    stdcall(u, "ReleaseDC", 2, [](Cpu&) { return 1u; });
    // GetDeviceCaps(dc, index): the desktop's.
    stdcall(g, "GetDeviceCaps", 2, [this](Cpu& c) {
        switch (c.arg(1)) {
        case 8: return uint32_t(screenWidth);         // HORZRES
        case 10: return uint32_t(screenHeight);       // VERTRES
        case 12: return uint32_t(screenBits);         // BITSPIXEL
        case 14: return 1u;                           // PLANES
        case 24: return screenBits <= 8 ? 256u : ~0u; // NUMCOLORS: -1 above 8 bits
        case 38: return screenBits <= 8 ? 0x100u : 0u; // RASTERCAPS: RC_PALETTE
        case 88:
        case 90: return 96u; // LOGPIXELSX, LOGPIXELSY
        case 104: return screenBits <= 8 ? 256u : 0u; // SIZEPALETTE
        case 116: return 60u;                         // VREFRESH
        default: return 0u;
        }
    });
    // GetSystemPaletteEntries: none unless the desktop has a palette, when
    // they're greys.
    stdcall(g, "GetSystemPaletteEntries", 4, [this](Cpu& c) {
        const uint32_t start = c.arg(1), count = c.arg(2), out = c.arg(3);
        if (screenBits > 8)
            return 0u;
        if (!out)
            return 256u;
        uint32_t n = 0;
        for (; n < count && start + n < 256 && c.valid(out + 4 * n, 4); ++n) {
            const uint32_t v = start + n;
            c.write32(out + 4 * n, v | v << 8 | v << 16);
        }
        return n;
    });
    stdcall(g, "GetStockObject", 1, [](Cpu& c) { return kStockObjects + c.arg(0); });
}

} // namespace reluva::x86
