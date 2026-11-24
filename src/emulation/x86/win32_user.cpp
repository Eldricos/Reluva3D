// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's windows and messages, and the little of GDI32 that's asked for,
// as Microsoft documents them. Nothing is drawn: a window is a record of its
// class's procedure, place and size, which the engine shows however it
// likes. Messages the engine posts (its input) wait in one queue, which
// PeekMessageA reads and DispatchMessageA hands to the window's procedure.
// The sizes of frames and captions are Windows 98's at 96 dpi.

#include "emulation/x86/win32.h"

#include "common/events.h"
#include "emulation/x86/format.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kWmCreate = 0x1,
    kWmDestroy = 0x2,
    kWmActivate = 0x6,
    kWmSetFocus = 0x7,
    kWmKillFocus = 0x8,
    kWmClose = 0x10,
    kWmQuit = 0x12,
    kWmShowWindow = 0x18,
    kWmActivateApp = 0x1c,
    kWmNcCreate = 0x81,
    kWmNcDestroy = 0x82,
    kWmKeyDown = 0x100,
    kWmChar = 0x102,
    kWmSysCommand = 0x112
};
constexpr uint32_t kScClose = 0xf060;
constexpr uint32_t kErrorCallNotImplemented = 120;
constexpr uint32_t kErrorInvalidWindowHandle = 1400;
constexpr uint32_t kWsBorder = 0x00800000;
constexpr uint32_t kWsDlgFrame = 0x00400000;
constexpr uint32_t kWsCaption = 0x00c00000;
constexpr uint32_t kWsThickFrame = 0x00040000;
constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kWsChild = 0x40000000;
// GetCurrentProcessId's, and the main thread's GetCurrentThreadId.
constexpr uint32_t kProcessId = 0x100;
constexpr uint32_t kMainThreadId = 0x104;
constexpr uint32_t kCwUseDefault = 0x80000000u;
constexpr int kCaption = 19;
constexpr int kMenu = 19;
constexpr int kFixedFrame = 3;
constexpr int kSizeFrame = 4;
constexpr int kBorder = 1;
// Handles that are only ever passed back: icons and cursors, GDI's stock
// objects, device contexts.
constexpr uint32_t kResourceHandles = 0x20000;
constexpr uint32_t kStockObjects = 0x28000;
constexpr uint32_t kDesktopWindow = 0x10004; // below the windows the program makes

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// How far the frame (and caption, and menu) reach in from each side of a
// window with this style: left, top, right, bottom.
struct Insets {
    int left;
    int top;
    int right;
    int bottom;
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

const Win32Process::Window *Win32Process::window(uint32_t handle) const {
    auto it = _windows.find(handle);
    return it == _windows.end() ? nullptr : &it->second;
}

std::optional<std::array<int, 4>> Win32Process::clientArea(uint32_t handle) const {
    const Window *w = window(handle);
    if (!w)
        return std::nullopt;
    const Insets f = frameOf(w->style, false);
    return std::array<int, 4>{w->x + f.left, w->y + f.top, std::max(0, w->width - f.left - f.right),
                              std::max(0, w->height - f.top - f.bottom)};
}

// Input.

bool windowsKey(uint32_t k, uint32_t &vk, uint32_t &scan) {
    // Scan codes of the letters a to z, and of the digits 1 to 9 then 0, on
    // a US keyboard.
    static const uint8_t kLetters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                         0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    static const uint8_t kKeypad[10] = {0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49};
    struct Entry {
        uint32_t key;
        uint32_t vk;
        uint32_t scan;
    };
    static const Entry kOthers[] = {
        {key::kReturn, 0x0d, 0x1c},
        {key::kEscape, 0x1b, 0x01},
        {key::kBackspace, 0x08, 0x0e},
        {key::kTab, 0x09, 0x0f},
        {key::kSpace, 0x20, 0x39},
        {key::kDelete, 0x2e, 0x153},
        {key::kInsert, 0x2d, 0x152},
        {key::kHome, 0x24, 0x147},
        {key::kEnd, 0x23, 0x14f},
        {key::kPageUp, 0x21, 0x149},
        {key::kPageDown, 0x22, 0x151},
        {key::kUp, 0x26, 0x148},
        {key::kDown, 0x28, 0x150},
        {key::kLeft, 0x25, 0x14b},
        {key::kRight, 0x27, 0x14d},
        {key::kLShift, 0x10, 0x2a},
        {key::kRShift, 0x10, 0x36},
        {key::kLCtrl, 0x11, 0x1d},
        {key::kRCtrl, 0x11, 0x11d},
        {key::kLAlt, 0x12, 0x38},
        {key::kRAlt, 0x12, 0x138},
        {key::kKpPeriod, 0x6e, 0x53},
        {key::kKpEnter, 0x0d, 0x11c},
        {'-', 0xbd, 0x0c},
        {'=', 0xbb, 0x0d},
        {'[', 0xdb, 0x1a},
        {']', 0xdd, 0x1b},
        {';', 0xba, 0x27},
        {'\'', 0xde, 0x28},
        {'`', 0xc0, 0x29},
        {'\\', 0xdc, 0x2b},
        {',', 0xbc, 0x33},
        {'.', 0xbe, 0x34},
        {'/', 0xbf, 0x35},
    };
    if (k >= 'a' && k <= 'z') {
        vk = 'A' + (k - 'a');
        scan = kLetters[k - 'a'];
        return true;
    }
    if (k >= '0' && k <= '9') {
        vk = k;
        scan = k == '0' ? 0x0b : 0x02 + (k - '1');
        return true;
    }
    if (k >= key::kF1 && k < key::kF1 + 12) {
        const uint32_t n = k - key::kF1;
        vk = 0x70 + n;
        scan = n < 10 ? 0x3b + n : 0x57 + (n - 10);
        return true;
    }
    if (k >= key::kKp0 && k < key::kKp0 + 10) {
        vk = 0x60 + (k - key::kKp0);
        scan = kKeypad[k - key::kKp0];
        return true;
    }
    for (const Entry &e : kOthers)
        if (e.key == k) {
            vk = e.vk;
            scan = e.scan;
            return true;
        }
    return false;
}

namespace {
constexpr uint32_t kWmMouseMove = 0x200;
constexpr uint32_t kWmLButtonDown = 0x201;
constexpr uint32_t kWmLButtonDblClk = 0x203;
constexpr uint32_t kWmMouseWheel = 0x20a;
constexpr uint32_t kWmKeyUp = 0x101;
constexpr uint32_t kCsDblClks = 0x8;
constexpr uint32_t kDoubleClickMs = 500;
constexpr int kDoubleClickDistance = 4;
// MK_ flags for buttons 0, 1, 2.
constexpr uint32_t kButtonFlags[3] = {0x1, 0x2, 0x10};
uint32_t point(int x, int y) {
    return uint32_t(uint16_t(int16_t(x))) | uint32_t(uint16_t(int16_t(y))) << 16;
}
} // namespace

void Win32Process::mouseMoveTo(int x, int y) {
    if (_clip) {
        x = std::clamp(x, (*_clip)[0], std::max((*_clip)[0], (*_clip)[2] - 1));
        y = std::clamp(y, (*_clip)[1], std::max((*_clip)[1], (*_clip)[3] - 1));
    }
    const int dx = x - cursorX;
    const int dy = y - cursorY;
    cursorX = x, cursorY = y;
    if (dx || dy)
        notify({RawInput::Kind::kMotion, dx, dy});
    if (const uint32_t w = mainWindow())
        if (auto area = clientArea(w))
            postMessage(w, kWmMouseMove, _buttons, point(x - (*area)[0], y - (*area)[1]));
}

void Win32Process::mouseButton(int button, bool down) {
    if (button < 0 || button > 2)
        return;
    const uint32_t flag = kButtonFlags[button];
    if (down)
        _buttons |= flag;
    else
        _buttons &= ~flag;
    _keysDown[button == 0 ? 1 : button == 1 ? 2 : 4] = down;
    notify({RawInput::Kind::kButton, button, down});
    const uint32_t w = mainWindow();
    auto area = w ? clientArea(w) : std::nullopt;
    if (!area)
        return;
    // WM_LBUTTONDOWN/UP/DBLCLK, then the right button's three, then the
    // middle's.
    uint32_t message = kWmLButtonDown + 3 * uint32_t(button) + (down ? 0 : 1);
    if (down) {
        const uint32_t now = milliseconds();
        if ((window(w)->classStyle & kCsDblClks) && _lastClickButton == button &&
            now - _lastClickTime <= kDoubleClickMs && std::abs(cursorX - _lastClickX) <= kDoubleClickDistance &&
            std::abs(cursorY - _lastClickY) <= kDoubleClickDistance) {
            message = kWmLButtonDblClk + 3 * uint32_t(button);
            _lastClickButton = -1;
        } else {
            _lastClickButton = button;
            _lastClickTime = now;
            _lastClickX = cursorX, _lastClickY = cursorY;
        }
    }
    postMessage(w, message, _buttons, point(cursorX - (*area)[0], cursorY - (*area)[1]));
}

void Win32Process::mouseWheel(int delta) {
    notify({RawInput::Kind::kWheel, delta, 0});
    if (const uint32_t w = mainWindow())
        postMessage(w, kWmMouseWheel, _buttons | uint32_t(uint16_t(int16_t(delta))) << 16, point(cursorX, cursorY));
}

// WM_KEYDOWN and WM_KEYUP: lParam has the repeat count (1), the scan
// code, and for a release the previous-state and transition bits.
void Win32Process::key(uint32_t virtualKey, uint32_t scanCode, bool down) {
    const bool was = _keysDown[virtualKey & 0xff] != 0;
    _keysDown[virtualKey & 0xff] = down;
    notify({RawInput::Kind::kKey, int(scanCode), down});
    const uint32_t w = mainWindow();
    if (!w)
        return;
    uint32_t lParam = 1 | (scanCode & 0xff) << 16 | (scanCode & 0x100 ? 1u << 24 : 0); // 0x100: an extended key
    if (down && was)
        lParam |= 1u << 30;
    if (!down)
        lParam |= 3u << 30;
    postMessage(w, down ? kWmKeyDown : kWmKeyUp, virtualKey, lParam);
}

void Win32Process::moveWindow(uint32_t handle, int x, int y, int width, int height) {
    auto it = _windows.find(handle);
    if (it == _windows.end())
        return;
    it->second.x = x, it->second.y = y, it->second.width = width, it->second.height = height;
}

uint32_t Win32Process::mainWindow() const {
    return _windows.empty() ? 0 : _windows.begin()->first;
}

void Win32Process::postMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam) {
    _messages.push_back({window, message, wParam, lParam});
}

uint32_t Win32Process::sendMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam) {
    auto it = _windows.find(window);
    if (it == _windows.end() || !it->second.proc)
        return 0;
    uint32_t result = 0;
    // From inside the program, a fault goes on up through call(); only a
    // call from the engine itself sees it here.
    if (Error e = _cpu.call(it->second.proc, {window, message, wParam, lParam}, result)) {
        if (output)
            output("A window procedure failed: " + e.message + "\n");
        return 0;
    }
    return result;
}

void Win32Process::addUserStandIns() {
    const char *u = "USER32.dll";
    const char *g = "GDI32.dll";
    auto rect = [](Cpu &c, uint32_t p, int left, int top, int right, int bottom) {
        c.write32(p, uint32_t(left));
        c.write32(p + 4, uint32_t(top));
        c.write32(p + 8, uint32_t(right));
        c.write32(p + 12, uint32_t(bottom));
    };

    // Classes and windows.
    // RegisterClassA(WNDCLASSA*) and RegisterClassExA(WNDCLASSEXA*), which
    // is the same after a size: the style, the procedure, then the name 32
    // bytes on.
    auto registerClass = [this](Cpu &c, uint32_t p) {
        if (!c.valid(p, 40))
            return 0u;
        const std::string name = lower(c.string(c.read32(p + 36)));
        if (name.empty() || _classes.count(name))
            return 0u;
        _classes[name] = {c.read32(p + 4), c.read32(p)};
        return uint32_t(0xc000 + _classes.size()); // an atom
    };
    stdcall(u, "RegisterClassA", 1, [registerClass](Cpu &c) { return registerClass(c, c.arg(0)); });
    stdcall(u, "RegisterClassExA", 1, [registerClass](Cpu &c) { return registerClass(c, c.arg(0) + 4); });
    // GetClassInfoA(instance, name, WNDCLASSA*): its style and procedure.
    stdcall(u, "GetClassInfoA", 3, [this](Cpu &c) {
        const uint32_t p = c.arg(2);
        auto it = _classes.find(c.arg(1) >= 0x10000 ? lower(c.string(c.arg(1))) : std::string());
        if (it == _classes.end() || !c.valid(p, 40)) {
            setLastError(1411); // ERROR_CLASS_DOES_NOT_EXIST
            return 0u;
        }
        std::memset(c.memory().data() + p, 0, 40);
        c.write32(p, it->second.style);
        c.write32(p + 4, it->second.proc);
        c.write32(p + 16, c.arg(0));
        c.write32(p + 36, c.arg(1));
        return 1u;
    });
    // A top-level window shown the first time is activated, and the
    // application with it, as the foreground window's is.
    auto activate = [this](uint32_t handle) {
        _active = handle;
        sendMessage(handle, kWmActivateApp, 1, 0);
        sendMessage(handle, kWmActivate, 1, 0); // WA_ACTIVE
        _focus = handle;
        sendMessage(handle, kWmSetFocus, 0, 0);
    };
    // CreateWindowExA: WM_NCCREATE then WM_CREATE go to the procedure, with
    // a CREATESTRUCTA; either saying no destroys the window again.
    stdcall(u, "CreateWindowExA", 12, [this, activate](Cpu &c) {
        const uint32_t exStyle = c.arg(0);
        const uint32_t className = c.arg(1);
        const uint32_t title = c.arg(2);
        const uint32_t style = c.arg(3);
        const uint32_t param = c.arg(11);
        const uint32_t instance = c.arg(10);
        const uint32_t menu = c.arg(9);
        const uint32_t parent = c.arg(8);
        int x = int32_t(c.arg(4));
        int y = int32_t(c.arg(5));
        int w = int32_t(c.arg(6));
        int h = int32_t(c.arg(7));
        auto cls = _classes.find(lower(className < 0x10000 ? "" : c.string(className)));
        if (cls == _classes.end()) {
            setLastError(1407); // ERROR_CANNOT_FIND_WND_CLASS
            return 0u;
        }
        if (uint32_t(x) == kCwUseDefault)
            x = y = 0;
        if (uint32_t(w) == kCwUseDefault) {
            w = screenWidth;
            h = screenHeight;
        }
        const uint32_t handle = _nextWindow;
        _nextWindow += 4;
        Window &win = _windows[handle];
        win.className = c.string(className);
        win.title = title ? c.string(title) : std::string();
        win.proc = cls->second.proc;
        win.classStyle = cls->second.style;
        win.style = style & ~kWsVisible;
        win.exStyle = exStyle;
        win.x = x, win.y = y, win.width = w, win.height = h;
        win.parent = parent;
        if (style & kWsChild)
            win.id = menu;

        const uint32_t create = memory().alloc(48); // CREATESTRUCTA
        const uint32_t fields[12] = {param,       instance,    menu,  parent, uint32_t(h), uint32_t(w),
                                     uint32_t(y), uint32_t(x), style, title,  className,   exStyle};
        for (int i = 0; i < 12; ++i)
            c.write32(create + 4 * uint32_t(i), fields[i]);
        const bool made = sendMessage(handle, kWmNcCreate, 0, create) != 0 &&
                          int32_t(sendMessage(handle, kWmCreate, 0, create)) != -1;
        memory().free(create);
        if (!made) {
            _windows.erase(handle);
            return 0u;
        }
        // Made visible, it's shown and activated as ShowWindow does it.
        if (style & kWsVisible) {
            _windows[handle].visible = true;
            sendMessage(handle, kWmShowWindow, 1, 0);
            if (!(style & kWsChild))
                activate(handle);
        }
        return handle;
    });
    // DestroyWindow(window): its children first, as Windows does.
    stdcall(u, "DestroyWindow", 1, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        if (!_windows.count(handle))
            return 0u;
        std::vector<uint32_t> gone{handle};
        for (size_t k = 0; k < gone.size(); ++k)
            for (const auto &[h, w] : _windows)
                if (w.parent == gone[k] && (w.style & kWsChild))
                    gone.push_back(h);
        for (auto it = gone.rbegin(); it != gone.rend(); ++it) {
            if (!_windows.count(*it))
                continue;
            sendMessage(*it, kWmDestroy, 0, 0);
            sendMessage(*it, kWmNcDestroy, 0, 0);
            _windows.erase(*it);
            _dialogs.erase(*it);
            if (_focus == *it)
                _focus = 0;
            if (_active == *it)
                _active = 0;
        }
        return 1u;
    });
    // FindWindowA(class, title): either may be 0, matching any.
    stdcall(u, "FindWindowA", 2, [this](Cpu &c) {
        const std::string cls = c.arg(0) >= 0x10000 ? lower(c.string(c.arg(0))) : std::string();
        const std::string title = c.arg(1) ? c.string(c.arg(1)) : std::string();
        for (const auto &[handle, w] : _windows)
            if ((cls.empty() || lower(w.className) == cls) && (!c.arg(1) || w.title == title))
                return handle;
        return 0u;
    });
    // ShowWindow: the first time it's shown, the window is activated (WM_ACTIVATEAPP,
    // WM_ACTIVATE, WM_SETFOCUS). Returns whether it was visible.
    stdcall(u, "ShowWindow", 2, [this, activate](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        const uint32_t handle = it->first;
        const bool was = it->second.visible;
        const bool show = c.arg(1) != 0; // SW_HIDE is 0
        it->second.visible = show;
        if (show != was)
            sendMessage(handle, kWmShowWindow, show, 0);
        if (show && !was && !(it->second.style & kWsChild))
            activate(handle);
        return was ? 1u : 0u;
    });
    stdcall(u, "UpdateWindow", 1, [this](Cpu &c) { return _windows.count(c.arg(0)) ? 1u : 0u; });
    stdcall(u, "IsWindow", 1, [this](Cpu &c) { return _windows.count(c.arg(0)) ? 1u : 0u; });
    stdcall(u, "InvalidateRect", 3, [this](Cpu &c) { return _windows.count(c.arg(0)) ? 1u : 0u; });
    stdcall(u, "GetActiveWindow", 0, [this](Cpu &) { return _active; });
    stdcall(u, "SetActiveWindow", 1, [this](Cpu &c) {
        if (!_windows.count(c.arg(0)))
            return 0u;
        return std::exchange(_active, c.arg(0));
    });
    stdcall(u, "GetDesktopWindow", 0, [](Cpu &) { return kDesktopWindow; });
    // Nothing is ever waiting to be painted: the engine shows what's drawn.
    stdcall(u, "ValidateRect", 2, [](Cpu &) { return 1u; });
    stdcall(u, "GetUpdateRect", 3, [](Cpu &c) {
        if (c.arg(1) && c.valid(c.arg(1), 16))
            std::memset(c.memory().data() + c.arg(1), 0, 16);
        return 0u;
    });
    // BeginPaint(window, PAINTSTRUCT*): the window's device context, and
    // nothing to paint.
    stdcall(u, "BeginPaint", 2, [this](Cpu &c) {
        const uint32_t dc = kWindowDeviceContexts + (c.arg(0) & 0xffff);
        if (!_windows.count(c.arg(0)) || !c.valid(c.arg(1), 64))
            return 0u;
        std::memset(c.memory().data() + c.arg(1), 0, 64);
        c.write32(c.arg(1), dc);
        return dc;
    });
    stdcall(u, "EndPaint", 2, [](Cpu &) { return 1u; });
    stdcall(u, "GetFocus", 0, [this](Cpu &) { return _focus; });
    // SetFocus(window): WM_KILLFOCUS to the one losing it, WM_SETFOCUS to the
    // one gaining it; returns the one that had it.
    stdcall(u, "SetFocus", 1, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        const uint32_t was = _focus;
        if (handle && !_windows.count(handle))
            return 0u;
        if (handle == was)
            return was;
        _focus = handle;
        if (was)
            sendMessage(was, kWmKillFocus, handle, 0);
        if (handle)
            sendMessage(handle, kWmSetFocus, was, 0);
        return was;
    });
    // GetWindowThreadProcessId(window, process id out): every window is the
    // main thread's.
    stdcall(u, "GetWindowThreadProcessId", 2, [this](Cpu &c) {
        if (!_windows.count(c.arg(0)))
            return 0u;
        if (c.arg(1) && c.valid(c.arg(1), 4))
            c.write32(c.arg(1), kProcessId);
        return kMainThreadId;
    });
    // GetWindowLongA and SetWindowLongA: GWL_WNDPROC -4, GWL_ID -12,
    // GWL_STYLE -16, GWL_EXSTYLE -20, GWL_USERDATA -21. Setting returns the
    // value before.
    auto windowLong = [](Window &w, int32_t index) -> uint32_t * {
        switch (index) {
        case -4:
            return &w.proc;
        case -12:
            return &w.id;
        case -16:
            return &w.style;
        case -20:
            return &w.exStyle;
        case -21:
            return &w.userData;
        default:
            return nullptr;
        }
    };
    stdcall(u, "GetWindowLongA", 2, [this, windowLong](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        if (int32_t(c.arg(1)) == -16) // with WS_VISIBLE, which `style` leaves out
            return it->second.style | (it->second.visible ? kWsVisible : 0);
        const uint32_t *v = windowLong(it->second, int32_t(c.arg(1)));
        return v ? *v : 0u;
    });
    stdcall(u, "SetWindowLongA", 3, [this, windowLong](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        if (int32_t(c.arg(1)) == -16) {
            const uint32_t was = it->second.style | (it->second.visible ? kWsVisible : 0);
            it->second.style = c.arg(2) & ~kWsVisible;
            it->second.visible = c.arg(2) & kWsVisible;
            return was;
        }
        uint32_t *v = windowLong(it->second, int32_t(c.arg(1)));
        if (!v)
            return 0u;
        const uint32_t was = *v;
        *v = c.arg(2);
        return was;
    });
    // CallWindowProcA(procedure, window, message, wParam, lParam)
    stdcall(u, "CallWindowProcA", 5, [](Cpu &c) {
        uint32_t result = 0;
        if (Error e = c.call(c.arg(0), {c.arg(1), c.arg(2), c.arg(3), c.arg(4)}, result))
            return 0u;
        return result;
    });
    // GetWindow(window, relation): GW_HWNDFIRST 0, GW_HWNDLAST 1,
    // GW_HWNDNEXT 2, GW_HWNDPREV 3, GW_OWNER 4, GW_CHILD 5, among the
    // window's siblings in the order they were made.
    stdcall(u, "GetWindow", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        const uint32_t relation = c.arg(1);
        if (relation == 4) // the owner: top-level windows here have none
            return it->second.style & kWsChild ? 0u : it->second.parent;
        std::vector<uint32_t> group;
        for (const auto &[handle, w] : _windows)
            if (relation == 5 ? w.parent == c.arg(0) : w.parent == it->second.parent)
                group.push_back(handle);
        if (group.empty())
            return 0u;
        const auto at = std::find(group.begin(), group.end(), c.arg(0));
        switch (relation) {
        case 0:
        case 5:
            return group.front();
        case 1:
            return group.back();
        case 2:
            return at + 1 < group.end() ? *(at + 1) : 0u;
        case 3:
            return at > group.begin() && at < group.end() ? *(at - 1) : 0u;
        default:
            return 0u;
        }
    });
    // GetTopWindow(window): its first child; with none given, the first
    // top-level window.
    stdcall(u, "GetTopWindow", 1, [this](Cpu &c) {
        for (const auto &[handle, w] : _windows)
            if (w.parent == c.arg(0))
                return handle;
        return 0u;
    });
    // SetWindowPos(window, after, x, y, cx, cy, flags): SWP_NOSIZE 1,
    // SWP_NOMOVE 2, SWP_SHOWWINDOW 0x40, SWP_HIDEWINDOW 0x80.
    stdcall(u, "SetWindowPos", 7, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        Window &w = it->second;
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
    stdcall(u, "GetWindowRect", 2, [this, rect](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w || !c.valid(c.arg(1), 16))
            return 0u;
        rect(c, c.arg(1), w->x, w->y, w->x + w->width, w->y + w->height);
        return 1u;
    });
    stdcall(u, "GetClientRect", 2, [this, rect](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w || !c.valid(c.arg(1), 16))
            return 0u;
        const Insets f = frameOf(w->style, false);
        rect(c, c.arg(1), 0, 0, std::max(0, w->width - f.left - f.right), std::max(0, w->height - f.top - f.bottom));
        return 1u;
    });
    stdcall(u, "ClientToScreen", 2, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w || !c.valid(c.arg(1), 8))
            return 0u;
        const Insets f = frameOf(w->style, false);
        c.write32(c.arg(1), c.read32(c.arg(1)) + uint32_t(w->x + f.left));
        c.write32(c.arg(1) + 4, c.read32(c.arg(1) + 4) + uint32_t(w->y + f.top));
        return 1u;
    });
    // AdjustWindowRect(rect, style, menu): the window's rectangle for a
    // client rectangle.
    stdcall(u, "AdjustWindowRect", 3, [](Cpu &c) {
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
    stdcall(u, "GetSystemMetrics", 1, [this](Cpu &c) {
        switch (c.arg(0)) {
        case 0:
            return uint32_t(screenWidth); // SM_CXSCREEN
        case 1:
            return uint32_t(screenHeight); // SM_CYSCREEN
        case 4:
            return uint32_t(kCaption); // SM_CYCAPTION
        case 5:
        case 6:
            return uint32_t(kBorder); // SM_CXBORDER, SM_CYBORDER
        case 7:
        case 8:
            return uint32_t(kFixedFrame); // SM_CXFIXEDFRAME, SM_CYFIXEDFRAME
        case 15:
            return uint32_t(kMenu); // SM_CYMENU
        case 16:
            return uint32_t(screenWidth); // SM_CXFULLSCREEN
        case 17:
            return uint32_t(screenHeight - kCaption); // SM_CYFULLSCREEN
        case 32:
        case 33:
            return uint32_t(kSizeFrame); // SM_CXFRAME, SM_CYFRAME
        case 43:
            return 3u; // SM_CMOUSEBUTTONS
        default:
            return 0u;
        }
    });

    // Messages.
    stdcall(u, "PostMessageA", 4, [this](Cpu &c) {
        postMessage(c.arg(0), c.arg(1), c.arg(2), c.arg(3));
        return 1u;
    });
    stdcall(u, "SendMessageA", 4, [this](Cpu &c) { return sendMessage(c.arg(0), c.arg(1), c.arg(2), c.arg(3)); });
    stdcall(u, "PostQuitMessage", 1, [this](Cpu &c) {
        postMessage(0, kWmQuit, c.arg(0), 0);
        return 0u;
    });
    // PeekMessageA(msg, window, first, last, remove): the oldest message for
    // the window (0: any) in the range (0, 0: any). WM_QUIT is for any window.
    stdcall(u, "PeekMessageA", 5, [this](Cpu &c) {
        const uint32_t out = c.arg(0);
        const uint32_t hwnd = c.arg(1);
        const uint32_t first = c.arg(2);
        const uint32_t last = c.arg(3);
        const uint32_t remove = c.arg(4);
        auto matches = [&](const Message &m) {
            return (hwnd == 0 || m.window == hwnd || m.message == kWmQuit) &&
                   ((first == 0 && last == 0) || (m.message >= first && m.message <= last));
        };
        runThreads();
        auto it = std::find_if(_messages.begin(), _messages.end(), matches);
        if (it == _messages.end() && idle) {
            idle();
            it = std::find_if(_messages.begin(), _messages.end(), matches);
        }
        if (it == _messages.end() || !c.valid(out, 28))
            return 0u;
        const uint32_t fields[7] = {it->window,     it->message,       it->wParam,       it->lParam,
                                    milliseconds(), uint32_t(cursorX), uint32_t(cursorY)};
        for (int i = 0; i < 7; ++i)
            c.write32(out + 4 * uint32_t(i), fields[i]);
        if (remove & 1) // PM_REMOVE
            _messages.erase(it);
        return 1u;
    });
    // GetMessageA(out, window, first, last): PeekMessageA's, waiting for a
    // message. Returns 0 for WM_QUIT, and when the process is ending.
    stdcall(u, "GetMessageA", 4, [this](Cpu &c) {
        const uint32_t out = c.arg(0);
        const uint32_t hwnd = c.arg(1);
        const uint32_t first = c.arg(2);
        const uint32_t last = c.arg(3);
        auto matches = [&](const Message &m) {
            return (hwnd == 0 || m.window == hwnd || m.message == kWmQuit) &&
                   ((first == 0 && last == 0) || (m.message >= first && m.message <= last));
        };
        auto found = [&] { return std::find_if(_messages.begin(), _messages.end(), matches) != _messages.end(); };
        while (!found()) {
            runThreads();
            if (idle)
                idle();
            if (!found() && !wait(found, 10) && ending())
                return 0u;
        }
        auto it = std::find_if(_messages.begin(), _messages.end(), matches);
        if (!c.valid(out, 28))
            return uint32_t(-1);
        const uint32_t fields[7] = {it->window,     it->message,       it->wParam,       it->lParam,
                                    milliseconds(), uint32_t(cursorX), uint32_t(cursorY)};
        for (int i = 0; i < 7; ++i)
            c.write32(out + 4 * uint32_t(i), fields[i]);
        const bool quit = it->message == kWmQuit;
        _messages.erase(it);
        return quit ? 0u : 1u;
    });
    stdcall(u, "DispatchMessageA", 1, [this](Cpu &c) {
        const uint32_t m = c.arg(0);
        if (!c.valid(m, 16))
            return 0u;
        return sendMessage(c.read32(m), c.read32(m + 4), c.read32(m + 8), c.read32(m + 12));
    });
    // TranslateMessage: a key that types a character (letters, digits,
    // marks, the keypad's, space, Enter, Backspace, Escape, Tab) queues
    // WM_CHAR for it, unshifted, as on a US keyboard.
    stdcall(u, "TranslateMessage", 1, [this](Cpu &c) {
        const uint32_t m = c.arg(0);
        if (!c.valid(m, 16) || c.read32(m + 4) != kWmKeyDown)
            return 0u;
        const uint32_t key = c.read32(m + 8);
        static const std::map<uint32_t, char> kMarks = {
            {0xba, ';'},  {0xbb, '='}, {0xbc, ','},  {0xbd, '-'}, {0xbe, '.'}, {0xbf, '/'}, {0xc0, '`'}, {0xdb, '['},
            {0xdc, '\\'}, {0xdd, ']'}, {0xde, '\''}, {0x6a, '*'}, {0x6b, '+'}, {0x6d, '-'}, {0x6e, '.'}, {0x6f, '/'},
        };
        uint32_t ch = 0;
        if (key >= 'A' && key <= 'Z')
            ch = key - 'A' + 'a';
        else if ((key >= '0' && key <= '9') || key == ' ' || key == 8 || key == 9 || key == 13 || key == 27)
            ch = key;
        else if (key >= 0x60 && key <= 0x69) // the keypad's digits
            ch = '0' + (key - 0x60);
        else if (auto mark = kMarks.find(key); mark != kMarks.end())
            ch = uint8_t(mark->second);
        if (!ch)
            return 0u;
        postMessage(c.read32(m), kWmChar, ch, c.read32(m + 12));
        return 1u;
    });
    stdcall(u, "TranslateAcceleratorA", 3, [](Cpu &) { return 0u; });
    // DefWindowProcA: closing destroys the window (WM_CLOSE, and the close
    // command); WM_NCCREATE goes on; everything else needs nothing.
    stdcall(u, "DefWindowProcA", 4, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        const uint32_t message = c.arg(1);
        const uint32_t wParam = c.arg(2);
        if (message == kWmNcCreate)
            return 1u;
        if (message == kWmSysCommand && (wParam & 0xfff0) == kScClose)
            return sendMessage(handle, kWmClose, 0, 0);
        if (message == kWmClose && _windows.count(handle)) {
            sendMessage(handle, kWmDestroy, 0, 0);
            sendMessage(handle, kWmNcDestroy, 0, 0);
            _windows.erase(handle);
        }
        return 0u;
    });
    // MessageBoxW: MessageBoxA's, its UTF-16 text taken as Latin-1 ('?'
    // beyond); answered OK.
    stdcall(u, "MessageBoxW", 4, [this](Cpu &c) {
        auto wide = [&c](uint32_t at) {
            std::string out;
            for (uint32_t i = 0; at && i < 8192 && c.valid(at + i, 2); i += 2) {
                const uint16_t ch = c.read16(at + i);
                if (!ch)
                    break;
                out.push_back(ch < 256 ? char(ch) : '?');
            }
            return out;
        };
        const std::string text = wide(c.arg(1));
        const std::string caption = c.arg(2) ? wide(c.arg(2)) : std::string("Error");
        if (messageBox)
            messageBox(caption, text);
        else if (output)
            output("[" + caption + "] " + text + "\n");
        return 1u; // IDOK
    });
    stdcall(u, "MessageBeep", 1, [](Cpu &) { return 1u; });

    // LoadStringA(instance, id, buffer, size): from the module's string
    // table resources (RT_STRING, 6: blocks of 16 strings, block n holding
    // the ids (n - 1) * 16 on, each a length and that many UTF-16 units),
    // taken as Latin-1 ('?' beyond), cut to fit with its NUL. The
    // characters copied; 0 if there's no such string.
    stdcall(u, "LoadStringA", 4, [this](Cpu &c) {
        const uint32_t buffer = c.arg(2);
        const uint32_t size = c.arg(3);
        if (!size || !c.valid(buffer, size))
            return 0u;
        c.write8(buffer, 0);
        const Module *m = moduleAt(c.arg(0) ? c.arg(0) : exeBase());
        if (!m || !m->image.resourceRva)
            return 0u;
        const uint32_t resources = m->base + m->image.resourceRva;
        // A directory's entry (16 bytes of header, the named and id
        // entries' counts at 12 and 14, then 8-byte entries of an id and an
        // offset from the resources' start), for `id`, or its first if
        // `any`; the offset, 0 if there's none.
        auto entry = [&c, resources](uint32_t directory, uint32_t id, bool any) {
            const uint32_t at = resources + (directory & 0x7fffffff);
            const uint32_t count = uint32_t(c.read16(at + 12)) + c.read16(at + 14);
            for (uint32_t i = 0; i < count && i < 4096; ++i)
                if (any || c.read32(at + 16 + i * 8) == id)
                    return c.read32(at + 20 + i * 8);
            return 0u;
        };
        const uint32_t id = c.arg(1) & 0xffff;
        const uint32_t type = entry(0, 6, false);
        const uint32_t block = type & 0x80000000 ? entry(type, id / 16 + 1, false) : 0;
        const uint32_t language = block & 0x80000000 ? entry(block, 0, true) : 0;
        if (!language || language & 0x80000000)
            return 0u;
        // The data entry: the strings' RVA.
        uint32_t at = m->base + c.read32(resources + language);
        for (uint32_t k = 0; k < id % 16; ++k)
            at += 2 + 2 * uint32_t(c.read16(at));
        const uint32_t length = std::min<uint32_t>(c.read16(at), size - 1);
        for (uint32_t i = 0; i < length; ++i) {
            const uint16_t ch = c.read16(at + 2 + 2 * i);
            c.write8(buffer + i, ch < 256 ? uint8_t(ch) : uint8_t('?'));
        }
        c.write8(buffer + length, 0);
        return length;
    });
    // wsprintfA(buffer, format, ...), cdecl: printf's formatting, which
    // takes all of wsprintf's; at most 1024 bytes are written, as Windows
    // writes. The characters written, without the NUL.
    provideCdecl(u, "wsprintfA", [](Cpu &c) {
        std::string text = formatPrintf(c, c.arg(1), c.reg[Cpu::ESP] + 4 + 4 * 2);
        if (text.size() > 1023)
            text.resize(1023);
        if (!c.writeString(c.arg(0), text, uint32_t(text.size() + 1)))
            return 0u;
        return uint32_t(text.size());
    });
    // InitCommonControls, by its ordinal: the controls' classes are there.
    stdcall("COMCTL32.dll", "#17", 0, [](Cpu &) { return 0u; });
    // MessageBoxA(window, text, caption, type): reported, and answered with
    // the button that lets the program go on: OK, Yes, or Cancel/Abort
    // where there's no OK.
    stdcall(u, "MessageBoxA", 4, [this](Cpu &c) {
        const std::string text = c.arg(1) ? c.string(c.arg(1)) : std::string();
        const std::string caption = c.arg(2) ? c.string(c.arg(2)) : std::string("Error");
        if (messageBox)
            messageBox(caption, text);
        else if (output)
            output("[" + caption + "] " + text + "\n");
        switch (c.arg(3) & 0xf) {
        case 2:
            return 3u; // MB_ABORTRETRYIGNORE: IDABORT
        case 3:        // MB_YESNOCANCEL
        case 4:
            return 6u; // MB_YESNO: IDYES
        case 5:
            return 2u; // MB_RETRYCANCEL: IDCANCEL
        default:
            return 1u; // IDOK
        }
    });

    // Icons, cursors and the mouse pointer.
    stdcall(u, "LoadIconA", 2, [](Cpu &c) { return kResourceHandles + (c.arg(1) & 0xffff); });
    stdcall(u, "LoadCursorA", 2, [](Cpu &c) { return kResourceHandles + (c.arg(1) & 0xffff); });
    stdcall(u, "SetCursor", 1, [](Cpu &) { return 0u; });
    stdcall(u, "GetCursorPos", 1, [this](Cpu &c) {
        if (!c.valid(c.arg(0), 8))
            return 0u;
        c.write32(c.arg(0), uint32_t(cursorX));
        c.write32(c.arg(0) + 4, uint32_t(cursorY));
        return 1u;
    });
    stdcall(u, "SetCursorPos", 2, [this](Cpu &c) {
        cursorX = std::clamp(int(int32_t(c.arg(0))), 0, screenWidth - 1);
        cursorY = std::clamp(int(int32_t(c.arg(1))), 0, screenHeight - 1);
        return 1u;
    });
    // ShowCursor(show): the display count after counting up or down.
    stdcall(u, "ShowCursor", 1, [this](Cpu &c) { return uint32_t(_cursorShown += c.arg(0) ? 1 : -1); });
    // ClipCursor(rect or 0): keeps the pointer within the rectangle.
    stdcall(u, "ClipCursor", 1, [this](Cpu &c) {
        if (c.arg(0) && c.valid(c.arg(0), 16)) {
            const uint32_t p = c.arg(0);
            _clip = std::array<int, 4>{int32_t(c.read32(p)), int32_t(c.read32(p + 4)), int32_t(c.read32(p + 8)),
                                       int32_t(c.read32(p + 12))};
            cursorX = std::clamp(cursorX, (*_clip)[0], std::max((*_clip)[0], (*_clip)[2] - 1));
            cursorY = std::clamp(cursorY, (*_clip)[1], std::max((*_clip)[1], (*_clip)[3] - 1));
        } else {
            _clip.reset();
        }
        return 1u;
    });
    // GetKeyState and GetAsyncKeyState(key): the top bit while it's down.
    auto keyState = [this](Cpu &c) {
        return _keysDown[c.arg(0) & 0xff] ? 0xffff8000u : 0u; // the mouse buttons are VK_LBUTTON and the like
    };
    stdcall(u, "GetKeyState", 1, keyState);
    stdcall(u, "GetAsyncKeyState", 1, keyState);

    // Device contexts.
    stdcall(u, "GetDC", 1, [](Cpu &c) { return kWindowDeviceContexts + (c.arg(0) & 0xffff); });
    stdcall(u, "ReleaseDC", 2, [](Cpu &) { return 1u; });
    // GetDeviceCaps(dc, index): the desktop's.
    stdcall(g, "GetDeviceCaps", 2, [this](Cpu &c) {
        switch (c.arg(1)) {
        case 8:
            return uint32_t(screenWidth); // HORZRES
        case 10:
            return uint32_t(screenHeight); // VERTRES
        case 12:
            return uint32_t(screenBits); // BITSPIXEL
        case 14:
            return 1u; // PLANES
        case 24:
            return screenBits <= 8 ? 256u : ~0u; // NUMCOLORS: -1 above 8 bits
        case 38:
            return screenBits <= 8 ? 0x100u : 0u; // RASTERCAPS: RC_PALETTE
        case 88:
        case 90:
            return 96u; // LOGPIXELSX, LOGPIXELSY
        case 104:
            return screenBits <= 8 ? 256u : 0u; // SIZEPALETTE
        case 116:
            return 60u; // VREFRESH
        default:
            return 0u;
        }
    });
    // GetSystemPaletteEntries: none unless the desktop has a palette, when
    // they're greys.
    stdcall(g, "GetSystemPaletteEntries", 4, [this](Cpu &c) {
        const uint32_t start = c.arg(1);
        const uint32_t count = c.arg(2);
        const uint32_t out = c.arg(3);
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
    stdcall(g, "GetStockObject", 1, [](Cpu &c) { return kStockObjects + c.arg(0); });
}

} // namespace reluva::x86
