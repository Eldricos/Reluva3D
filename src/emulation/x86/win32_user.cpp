// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's windows and messages, as Microsoft documents them. A window is a
// record of its class's procedure, place and size, which the engine shows
// however it likes (win32_gdi.cpp draws on them). Messages the engine posts
// (its input) wait in one queue, which PeekMessageA reads and
// DispatchMessageA hands to the window's procedure.
// The sizes of frames and captions are Windows 98's at 96 dpi.

#include "emulation/x86/win32.h"

#include "common/events.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kWmCreate = 0x1,
    kWmDestroy = 0x2,
    kWmActivate = 0x6,
    kWmSetFocus = 0x7,
    kWmPaint = 0xf,
    kWmClose = 0x10,
    kWmQuit = 0x12,
    kWmShowWindow = 0x18,
    kWmActivateApp = 0x1c,
    kWmNcCreate = 0x81,
    kWmNcDestroy = 0x82,
    kWmKeyDown = 0x100,
    kWmChar = 0x102,
    kWmSysCommand = 0x112,
    kWmTimer = 0x113
};
// The desktop's window handle: never one of the program's, which count up
// from 0x10010 in fours.
constexpr uint32_t kDesktopWindow = 0x0001000e;
constexpr uint32_t kScClose = 0xf060;
constexpr uint32_t kWsBorder = 0x00800000;
constexpr uint32_t kWsDlgFrame = 0x00400000;
constexpr uint32_t kWsCaption = 0x00c00000;
constexpr uint32_t kWsThickFrame = 0x00040000;
constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kWsChild = 0x40000000;
constexpr uint32_t kCwUseDefault = 0x80000000u;
constexpr int kCaption = 19;
constexpr int kMenu = 19;
constexpr int kFixedFrame = 3;
constexpr int kSizeFrame = 4;
constexpr int kBorder = 1;
// Handles that are only ever passed back: icons and cursors
// (win32_gdi.cpp has GDI's).
constexpr uint32_t kResourceHandles = 0x20000;

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
    if (const uint32_t w = inputWindow())
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
    const int key = button == 0 ? 1 : button == 1 ? 2 : 4;
    _keysDown[key] = down;
    _pressedSinceAsked[key] = _pressedSinceAsked[key] || down;
    notify({RawInput::Kind::kButton, button, down});
    const uint32_t w = inputWindow();
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
    if (const uint32_t w = inputWindow())
        postMessage(w, kWmMouseWheel, _buttons | uint32_t(uint16_t(int16_t(delta))) << 16, point(cursorX, cursorY));
}

// WM_KEYDOWN and WM_KEYUP: lParam has the repeat count (1), the scan
// code, and for a release the previous-state and transition bits.
void Win32Process::key(uint32_t virtualKey, uint32_t scanCode, bool down) {
    const bool was = _keysDown[virtualKey & 0xff] != 0;
    _keysDown[virtualKey & 0xff] = down;
    _pressedSinceAsked[virtualKey & 0xff] = _pressedSinceAsked[virtualKey & 0xff] || down;
    notify({RawInput::Kind::kKey, int(scanCode), down});
    const uint32_t w = inputWindow();
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

uint32_t Win32Process::inputWindow() const {
    for (auto it = _windows.rbegin(); it != _windows.rend(); ++it)
        if (it->second.dialogProc && it->second.visible && !it->second.dialogResult)
            return it->first;
    return mainWindow();
}

void Win32Process::postMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam) {
    _messages.push_back({window, message, wParam, lParam});
}

uint32_t Win32Process::sendMessage(uint32_t window, uint32_t message, uint32_t wParam, uint32_t lParam) {
    auto it = _windows.find(window);
    if (it == _windows.end())
        return 0;
    // A dialog's window procedure would be the system's, calling the
    // dialog's own; that's called here, and what it says is returned.
    const bool dialog = !it->second.proc && it->second.dialogProc;
    const uint32_t proc = dialog ? it->second.dialogProc : it->second.proc;
    if (!proc)
        return 0;
    uint32_t result = 0;
    // From inside the program, a fault goes on up through call(); only a
    // call from the engine itself sees it here.
    if (Error e = _cpu.call(proc, {window, message, wParam, lParam}, result)) {
        if (output)
            output("A window procedure failed: " + e.message + "\n");
        return 0;
    }
    // Painting a dialog that doesn't, the system's procedure would mark it
    // painted (as DefWindowProcA does).
    if (dialog && message == kWmPaint && !result)
        if (auto w = _windows.find(window); w != _windows.end())
            w->second.invalid = false;
    return result;
}

bool Win32Process::takeMessage(uint32_t window, uint32_t first, uint32_t last, bool remove, Message &out) {
    if (std::exchange(_screenDrawn, false) && screenPainted)
        screenPainted();
    auto inRange = [&](uint32_t message) { return (first == 0 && last == 0) || (message >= first && message <= last); };
    auto matches = [&](const Message &m) {
        return (window == 0 || m.window == window || m.message == kWmQuit) && inRange(m.message);
    };
    if (auto it = std::find_if(_messages.begin(), _messages.end(), matches); it != _messages.end()) {
        out = *it;
        if (remove)
            _messages.erase(it);
        return true;
    }
    // WM_PAINT stays until the window is painted (BeginPaint, ValidateRect).
    if (inRange(kWmPaint))
        for (const auto &[handle, w] : _windows)
            if (w.invalid && w.visible && (window == 0 || handle == window)) {
                out = {handle, kWmPaint, 0, 0};
                return true;
            }
    if (inRange(kWmTimer)) {
        const uint64_t at = now();
        for (auto &[key, timer] : _windowTimers)
            if (timer.due <= at && (window == 0 || key.first == window)) {
                out = {key.first, kWmTimer, key.second, timer.proc};
                if (remove)
                    timer.due = at + timer.interval;
                return true;
            }
    }
    return false;
}

uint32_t Win32Process::dispatch(const Message &m) {
    if (m.message == kWmTimer && m.lParam) { // TIMERPROC(window, WM_TIMER, id, time)
        uint32_t result = 0;
        if (Error e = _cpu.call(m.lParam, {m.window, kWmTimer, m.wParam, milliseconds()}, result))
            if (output)
                output("A timer's procedure failed: " + e.message + "\n");
        return 0;
    }
    return sendMessage(m.window, m.message, m.wParam, m.lParam);
}

bool Win32Process::awaitMessage(Cpu &c) {
    uint64_t next = UINT64_MAX;
    for (const auto &[key, timer] : _windowTimers)
        next = std::min(next, timer.due);
    if (!hostThreads() || !currentThread()) {
        // Taking turns, the program's other threads run here, and the
        // engine's idle() is its chance to post input.
        runThreads();
        if (idle) {
            idle();
        } else if (next == UINT64_MAX && _messages.empty()) {
            c.abort("the program waits for a message, and nothing could send one");
            return false;
        }
        return true;
    }
    const uint64_t at = now();
    const uint32_t ms =
        next == UINT64_MAX ? 0xffffffffu : uint32_t(next > at ? std::min<uint64_t>(next - at, 1000) : 0);
    const bool queued = wait([this] { return !_messages.empty(); }, ms);
    if (!queued && ending()) {
        c.abort("the process is ending");
        return false;
    }
    return true;
}

void Win32Process::addUserStandIns() {
    const char *u = "USER32.dll";
    auto rect = [](Cpu &c, uint32_t p, int left, int top, int right, int bottom) {
        c.write32(p, uint32_t(left));
        c.write32(p + 4, uint32_t(top));
        c.write32(p + 8, uint32_t(right));
        c.write32(p + 12, uint32_t(bottom));
    };

    // Classes and windows.
    // RegisterClassA(WNDCLASSA*): the procedure is at +4, the name at +36.
    stdcall(u, "RegisterClassA", 1, [this](Cpu &c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 40))
            return 0u;
        const std::string name = lower(c.string(c.read32(p + 36)));
        if (name.empty() || _classes.count(name))
            return 0u;
        _classes[name] = {c.read32(p + 4), c.read32(p)};
        return uint32_t(0xc000 + _classes.size()); // an atom
    });
    // CreateWindowExA: WM_NCCREATE then WM_CREATE go to the procedure, with
    // a CREATESTRUCTA; either saying no destroys the window again.
    stdcall(u, "CreateWindowExA", 12, [this](Cpu &c) {
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
        win.thread = currentThread() ? currentThread()->id : kMainThreadId;

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
        if (style & kWsVisible)
            _windows[handle].visible = true;
        return handle;
    });
    stdcall(u, "DestroyWindow", 1, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        if (!_windows.count(handle))
            return 0u;
        sendMessage(handle, kWmDestroy, 0, 0);
        sendMessage(handle, kWmNcDestroy, 0, 0);
        _windows.erase(handle);
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
    // ShowWindow: the first time it's shown, the window is activated and the
    // application with it (WM_ACTIVATEAPP, WM_ACTIVATE, WM_SETFOCUS), as the
    // foreground window's is. Returns whether it was visible.
    stdcall(u, "ShowWindow", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        const uint32_t handle = it->first;
        const bool was = it->second.visible;
        const bool show = c.arg(1) != 0; // SW_HIDE is 0
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
    stdcall(u, "UpdateWindow", 1, [this](Cpu &c) { return _windows.count(c.arg(0)) ? 1u : 0u; });
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
    auto writeMessage = [this](Cpu &c, uint32_t out, const Message &m) { // MSG
        const uint32_t fields[7] = {m.window,       m.message,         m.wParam,         m.lParam,
                                    milliseconds(), uint32_t(cursorX), uint32_t(cursorY)};
        for (int i = 0; i < 7; ++i)
            c.write32(out + 4 * uint32_t(i), fields[i]);
    };
    // PeekMessageA(msg, window, first, last, remove): the oldest message for
    // the window (0: any) in the range (0, 0: any). WM_QUIT is for any window.
    stdcall(u, "PeekMessageA", 5, [this, writeMessage](Cpu &c) {
        const uint32_t out = c.arg(0);
        const uint32_t hwnd = c.arg(1);
        const uint32_t first = c.arg(2);
        const uint32_t last = c.arg(3);
        const uint32_t remove = c.arg(4);
        if (!c.valid(out, 28))
            return 0u;
        runThreads();
        Message m;
        bool found = takeMessage(hwnd, first, last, remove & 1, m); // PM_REMOVE
        if (!found && idle) {
            idle();
            found = takeMessage(hwnd, first, last, remove & 1, m);
        }
        if (!found)
            return 0u;
        writeMessage(c, out, m);
        return 1u;
    });
    // GetMessageA(msg, window, first, last): waits for one; 0 for WM_QUIT.
    stdcall(u, "GetMessageA", 4, [this, writeMessage](Cpu &c) {
        const uint32_t out = c.arg(0);
        if (!c.valid(out, 28))
            return uint32_t(-1);
        Message m;
        while (true) {
            runThreads();
            if (takeMessage(c.arg(1), c.arg(2), c.arg(3), true, m))
                break;
            if (!awaitMessage(c))
                return uint32_t(-1);
        }
        writeMessage(c, out, m);
        return m.message == kWmQuit ? 0u : 1u;
    });
    stdcall(u, "DispatchMessageA", 1, [this](Cpu &c) {
        const uint32_t m = c.arg(0);
        if (!c.valid(m, 16))
            return 0u;
        return dispatch({c.read32(m), c.read32(m + 4), c.read32(m + 8), c.read32(m + 12)});
    });
    // TranslateMessage: a key that types a character (letters, digits,
    // space, Enter, Backspace, Escape, Tab) queues WM_CHAR for it.
    stdcall(u, "TranslateMessage", 1, [this](Cpu &c) {
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
    stdcall(u, "TranslateAcceleratorA", 3, [](Cpu &) { return 0u; });
    // DefWindowProcA: closing destroys the window (WM_CLOSE, and the close
    // command); WM_NCCREATE goes on; everything else needs nothing.
    stdcall(u, "DefWindowProcA", 4, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        const uint32_t message = c.arg(1);
        const uint32_t wParam = c.arg(2);
        if (message == kWmNcCreate)
            return 1u;
        if (message == kWmPaint) {
            if (auto it = _windows.find(handle); it != _windows.end())
                it->second.invalid = false;
            return 0u;
        }
        if (message == kWmSysCommand && (wParam & 0xfff0) == kScClose)
            return sendMessage(handle, kWmClose, 0, 0);
        if (message == kWmClose && _windows.count(handle)) {
            sendMessage(handle, kWmDestroy, 0, 0);
            sendMessage(handle, kWmNcDestroy, 0, 0);
            _windows.erase(handle);
        }
        return 0u;
    });
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
    // GetKeyState and GetAsyncKeyState(key): the top bit while it's down
    // (the mouse buttons are VK_LBUTTON and the like). GetAsyncKeyState's
    // lowest bit says it was pressed since GetAsyncKeyState last asked,
    // which programs polling for clicks rely on.
    stdcall(u, "GetKeyState", 1, [this](Cpu &c) { return _keysDown[c.arg(0) & 0xff] ? 0xffff8000u : 0u; });
    stdcall(u, "GetAsyncKeyState", 1, [this](Cpu &c) {
        const uint32_t key = c.arg(0) & 0xff;
        const uint32_t state = (_keysDown[key] ? 0xffff8000u : 0u) | (_pressedSinceAsked[key] ? 1u : 0u);
        _pressedSinceAsked[key] = false;
        return state;
    });

    // Device contexts.
    // Rectangles (RECT: left, top, right, bottom).
    stdcall(u, "SetRect", 5, [rect](Cpu &c) {
        if (!c.valid(c.arg(0), 16))
            return 0u;
        rect(c, c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)));
        return 1u;
    });
    stdcall(u, "OffsetRect", 3, [rect](Cpu &c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 16))
            return 0u;
        const int dx = int32_t(c.arg(1));
        const int dy = int32_t(c.arg(2));
        rect(c, p, int32_t(c.read32(p)) + dx, int32_t(c.read32(p + 4)) + dy, int32_t(c.read32(p + 8)) + dx,
             int32_t(c.read32(p + 12)) + dy);
        return 1u;
    });
    // IntersectRect(out, a, b): an empty rectangle (all 0) if they don't meet.
    stdcall(u, "IntersectRect", 3, [rect](Cpu &c) {
        const uint32_t a = c.arg(1);
        const uint32_t b = c.arg(2);
        if (!c.valid(c.arg(0), 16) || !c.valid(a, 16) || !c.valid(b, 16))
            return 0u;
        const int left = std::max(int32_t(c.read32(a)), int32_t(c.read32(b)));
        const int top = std::max(int32_t(c.read32(a + 4)), int32_t(c.read32(b + 4)));
        const int right = std::min(int32_t(c.read32(a + 8)), int32_t(c.read32(b + 8)));
        const int bottom = std::min(int32_t(c.read32(a + 12)), int32_t(c.read32(b + 12)));
        if (left >= right || top >= bottom) {
            rect(c, c.arg(0), 0, 0, 0, 0);
            return 0u;
        }
        rect(c, c.arg(0), left, top, right, bottom);
        return 1u;
    });

    // The windows' tree: a dialog's controls are its children, in the order
    // they were made; the others are on top, in that order too.
    auto siblings = [this](uint32_t parent) {
        std::vector<uint32_t> list;
        for (const auto &[handle, w] : _windows)
            if (w.parent == parent)
                list.push_back(handle);
        return list;
    };
    // GetWindow(window, relation): GW_HWNDFIRST 0, GW_HWNDLAST 1,
    // GW_HWNDNEXT 2, GW_HWNDPREV 3, GW_OWNER 4, GW_CHILD 5.
    stdcall(u, "GetWindow", 2, [this, siblings](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w)
            return 0u;
        if (c.arg(1) == 4)
            return w->owner;
        if (c.arg(1) == 5) {
            const std::vector<uint32_t> children = siblings(c.arg(0));
            return children.empty() ? 0u : children.front();
        }
        const std::vector<uint32_t> list = siblings(w->parent);
        auto at = std::find(list.begin(), list.end(), c.arg(0));
        switch (c.arg(1)) {
        case 0:
            return list.front();
        case 1:
            return list.back();
        case 2:
            return at + 1 < list.end() ? *(at + 1) : 0u;
        case 3:
            return at > list.begin() ? *(at - 1) : 0u;
        default:
            return 0u;
        }
    });
    stdcall(u, "GetTopWindow", 1, [siblings](Cpu &c) {
        const std::vector<uint32_t> list = siblings(c.arg(0) == kDesktopWindow ? 0 : c.arg(0));
        return list.empty() ? 0u : list.front();
    });
    // GetClassLongA: GCL_WNDPROC (-24) and GCL_STYLE (-26) are kept.
    stdcall(u, "GetClassLongA", 2, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w)
            return 0u;
        const int32_t index = int32_t(c.arg(1));
        return index == -24 ? w->proc : index == -26 ? w->classStyle : 0u;
    });
    stdcall(u, "UnregisterClassA", 2,
            [this](Cpu &c) { return c.arg(0) > 0xffff && _classes.erase(lower(c.string(c.arg(0)))) ? 1u : 0u; });
    // GetWindowPlacement: shown normally (SW_SHOWNORMAL), where it is.
    stdcall(u, "GetWindowPlacement", 2, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        const uint32_t p = c.arg(1);
        if (!w || !c.valid(p, 44))
            return 0u;
        const uint32_t fields[11] = {44,
                                     0,
                                     1,
                                     0,
                                     0,
                                     0,
                                     0,
                                     uint32_t(w->x),
                                     uint32_t(w->y),
                                     uint32_t(w->x + w->width),
                                     uint32_t(w->y + w->height)};
        for (int i = 0; i < 11; ++i)
            c.write32(p + 4 * uint32_t(i), fields[i]);
        return 1u;
    });
    stdcall(u, "ScreenToClient", 2, [this](Cpu &c) {
        const auto area = clientArea(c.arg(0));
        if (!area || !c.valid(c.arg(1), 8))
            return 0u;
        c.write32(c.arg(1), c.read32(c.arg(1)) - uint32_t((*area)[0]));
        c.write32(c.arg(1) + 4, c.read32(c.arg(1) + 4) - uint32_t((*area)[1]));
        return 1u;
    });
    stdcall(u, "SetCapture", 1, [this](Cpu &c) { return std::exchange(_capture, c.arg(0)); });
    stdcall(u, "ReleaseCapture", 0, [this](Cpu &) {
        _capture = 0;
        return 1u;
    });

    // InitCommonControls (COMCTL32's 17th export): the common controls are
    // never drawn, so there's nothing to register.
    stdcall("COMCTL32.dll", "#17", 0, [](Cpu &) { return 0u; });

    // The desktop is a window of its own, the screen's size.
    stdcall(u, "GetDesktopWindow", 0, [](Cpu &) { return kDesktopWindow; });
    stdcall(u, "IsWindow", 1,
            [this](Cpu &c) { return uint32_t(c.arg(0) == kDesktopWindow || window(c.arg(0)) != nullptr); });
    stdcall(u, "IsWindowVisible", 1, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        return uint32_t(w && w->visible);
    });
    // GetWindowThreadProcessId(window, process id*): the thread that made
    // it, in this process.
    stdcall(u, "GetWindowThreadProcessId", 2, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w)
            return 0u;
        if (c.arg(1) && c.valid(c.arg(1), 4))
            c.write32(c.arg(1), kProcessId);
        return w->thread;
    });
    stdcall(u, "IsIconic", 1, [](Cpu &) { return 0u; }); // nothing is ever minimised
    stdcall(u, "EnableWindow", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        const bool wasDisabled = !it->second.enabled;
        it->second.enabled = c.arg(1) != 0;
        return uint32_t(wasDisabled);
    });
    // The program's windows are the only ones: the first is active, and has
    // the keyboard until SetFocus gives it to another.
    stdcall(u, "GetActiveWindow", 0, [this](Cpu &) { return inputWindow(); });
    stdcall(u, "GetFocus", 0, [this](Cpu &) { return _focus && window(_focus) ? _focus : inputWindow(); });
    stdcall(u, "SetFocus", 1, [this](Cpu &c) {
        const uint32_t old = _focus && window(_focus) ? _focus : inputWindow();
        _focus = c.arg(0);
        return old;
    });
    // GetWindowLongA(window, index): GWL_WNDPROC (-4), GWL_STYLE (-16),
    // GWL_EXSTYLE (-20), GWL_USERDATA (-21); SetWindowLongA returns the old.
    stdcall(u, "GetWindowLongA", 2, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        if (!w)
            return 0u;
        switch (int32_t(c.arg(1))) {
        case -4:
            return w->proc;
        case -16:
            return w->style;
        case -20:
            return w->exStyle;
        case -21:
            return w->userData;
        default:
            return 0u;
        }
    });
    stdcall(u, "SetWindowLongA", 3, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        Window &w = it->second;
        uint32_t *field = nullptr;
        switch (int32_t(c.arg(1))) {
        case -4:
            field = &w.proc;
            break;
        case -16:
            field = &w.style;
            break;
        case -20:
            field = &w.exStyle;
            break;
        case -21:
            field = &w.userData;
            break;
        default:
            return 0u;
        }
        const uint32_t old = *field;
        *field = c.arg(2);
        return old;
    });
    stdcall(u, "SetWindowTextA", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        it->second.title = c.string(c.arg(1));
        return 1u;
    });
    stdcall(u, "GetWindowTextA", 3, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        const uint32_t size = c.arg(2);
        if (!w || size == 0 || !c.valid(c.arg(1), size))
            return 0u;
        const std::string t = w->title.substr(0, size - 1);
        c.writeString(c.arg(1), t, uint32_t(t.size() + 1));
        return uint32_t(t.size());
    });
    stdcall(u, "MoveWindow", 6, [this](Cpu &c) {
        if (!window(c.arg(0)))
            return 0u;
        moveWindow(c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)));
        return 1u;
    });
    // Nothing is painted through the window's own drawing: the programs here
    // draw with DirectDraw, so there's nothing to mark for painting.
    // Nothing is drawn, so all of a window is painted again or none.
    auto markInvalid = [this](uint32_t handle, bool invalid) {
        auto it = _windows.find(handle);
        if (it == _windows.end())
            return 0u;
        it->second.invalid = invalid;
        return 1u;
    };
    stdcall(u, "InvalidateRect", 3, [markInvalid](Cpu &c) { return markInvalid(c.arg(0), true); });
    stdcall(u, "ValidateRect", 2, [markInvalid](Cpu &c) { return markInvalid(c.arg(0), false); });
    // A 101- or 102-key keyboard (type 4), 12 function keys.
    stdcall(u, "GetKeyboardType", 1, [](Cpu &c) {
        switch (c.arg(0)) {
        case 0:
            return 4u;
        case 2:
            return 12u;
        default:
            return 0u;
        }
    });
    // A program changing the system's own pointer changes nothing here: its
    // pointer is whatever it draws or sets for its own window.
    stdcall(u, "SetSystemCursor", 2, [](Cpu &) { return 1u; });
    // SystemParametersInfoA(action, uiParam, pvParam, winIni): the work
    // area (SPI_GETWORKAREA, 48) is the whole screen; the screen saver
    // (SPI_GETSCREENSAVEACTIVE, 16) is off; other settings are taken as set.
    stdcall(u, "SystemParametersInfoA", 4, [this, rect](Cpu &c) {
        const uint32_t out = c.arg(2);
        switch (c.arg(0)) {
        case 48:
            if (c.valid(out, 16))
                rect(c, out, 0, 0, screenWidth, screenHeight);
            break;
        case 16:
            if (c.valid(out, 4))
                c.write32(out, 0);
            break;
        default:
            break;
        }
        return 1u;
    });
    // Single-byte text: the next and previous characters are a byte away.
    stdcall(u, "CharNextA", 1, [](Cpu &c) { return c.read8(c.arg(0)) ? c.arg(0) + 1 : c.arg(0); });
    stdcall(u, "CharPrevA", 2, [](Cpu &c) { return c.arg(1) > c.arg(0) ? c.arg(1) - 1 : c.arg(0); });
}

} // namespace reluva::x86
