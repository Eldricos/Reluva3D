// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's windows and messages, and the little of GDI32 that's asked for,
// as Microsoft documents them. Nothing is drawn: a window is a record of its
// class's procedure, place and size, which the engine shows however it
// likes. Messages the engine posts (its input) wait in one queue, which
// PeekMessageA reads and DispatchMessageA hands to the window's procedure.
// The sizes of frames and captions are Windows 98's at 96 dpi.

#include "emulation/x86/win32.h"

#include "emulation/x86/format.h"
#include "emulation/x86/gdi.h"

#include "common/events.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kWmCreate = 0x1,
    kWmDestroy = 0x2,
    kWmActivate = 0x6,
    kWmSetFocus = 0x7,
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
constexpr uint32_t kWsBorder = 0x00800000;
constexpr uint32_t kWsDlgFrame = 0x00400000;
constexpr uint32_t kWsCaption = 0x00c00000;
constexpr uint32_t kWsThickFrame = 0x00040000;
constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kWsChild = 0x40000000;
constexpr uint32_t kWsMaximize = 0x01000000;
constexpr uint32_t kWmNcHitTest = 0x0084;
constexpr uint32_t kWmMove = 0x0003;
constexpr uint32_t kWmSize = 0x0005;
constexpr uint32_t kWmSetCursor = 0x0020;
constexpr uint32_t kHtClient = 1;
constexpr uint32_t kCwUseDefault = 0x80000000u;
constexpr int kCaption = 19;
constexpr int kMenu = 19;
constexpr int kFixedFrame = 3;
constexpr int kSizeFrame = 4;
constexpr int kBorder = 1;
constexpr int kScrollBar = 16; // SM_CXVSCROLL, SM_CYHSCROLL
constexpr uint32_t kWmHScroll = 0x0114;
constexpr uint32_t kWmVScroll = 0x0115;
constexpr uint32_t kWmNcLButtonDown = 0x00a1;
constexpr uint32_t kHtHScroll = 6;
constexpr uint32_t kHtVScroll = 7;
// The scroll messages' requests.
constexpr uint32_t kSbLineUp = 0;
constexpr uint32_t kSbLineDown = 1;
constexpr uint32_t kSbPageUp = 2;
constexpr uint32_t kSbPageDown = 3;
constexpr uint32_t kSbThumbPosition = 4;
constexpr uint32_t kSbThumbTrack = 5;
constexpr uint32_t kSbEndScroll = 8;
constexpr int kScrollThumb = 4; // scrollPart's answer for the thumb
// A held arrow or track scrolls again after a pause, then steadily.
constexpr uint64_t kScrollFirstRepeatMs = 400;
constexpr uint64_t kScrollRepeatMs = 50;
constexpr uint32_t kWsVScroll = 0x00200000;
constexpr uint32_t kWsHScroll = 0x00100000;
// Handles that are only ever passed back: icons and cursors, GDI's stock
// objects, device contexts.
constexpr uint32_t kResourceHandles = 0x20000;
constexpr uint32_t kStockObjects = 0x28000;
constexpr uint32_t kDeviceContext = 0x30000;
constexpr uint32_t kFileCursors = 0x2c000; // LoadCursorFromFileA's handles, in the order made

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
    // Scroll bars lie outside the client area, on its right and bottom.
    return {edge, top, edge + ((style & kWsVScroll) ? kScrollBar : 0), edge + ((style & kWsHScroll) ? kScrollBar : 0)};
}

} // namespace

const Win32Process::Window *Win32Process::window(uint32_t handle) const {
    auto it = _windows.find(handle);
    return it == _windows.end() ? nullptr : &it->second;
}

std::optional<std::array<int, 4>> Win32Process::windowArea(uint32_t handle) const {
    const Window *w = window(handle);
    if (!w)
        return std::nullopt;
    std::array<int, 4> a{w->x, w->y, w->width, w->height};
    // A child's place is in its parent's client area. (A parent past
    // eight deep is taken as none: no program nests so far, and a loop
    // would otherwise never end.)
    for (int depth = 0; (w->style & kWsChild) && w->parent && depth < 8; ++depth) {
        const Window *parent = window(w->parent);
        if (!parent)
            break;
        const Insets f = frameOf(parent->style, false);
        a[0] += parent->x + f.left;
        a[1] += parent->y + f.top;
        w = parent;
    }
    return a;
}

std::optional<std::array<int, 4>> Win32Process::clientArea(uint32_t handle) const {
    const Window *w = window(handle);
    const auto a = windowArea(handle);
    if (!w || !a)
        return std::nullopt;
    const Insets f = frameOf(w->style, false);
    return std::array<int, 4>{(*a)[0] + f.left, (*a)[1] + f.top, std::max(0, w->width - f.left - f.right),
                              std::max(0, w->height - f.top - f.bottom)};
}

bool Win32Process::shown(uint32_t handle) const {
    for (int depth = 0; handle && depth < 9; ++depth) {
        const Window *w = window(handle);
        if (!w || !w->visible)
            return false;
        if (!(w->style & kWsChild))
            return true;
        handle = w->parent;
    }
    return true;
}

uint32_t Win32Process::windowAt(int x, int y) const {
    auto inside = [&](uint32_t h) {
        const auto a = windowArea(h);
        return a && x >= (*a)[0] && x < (*a)[0] + (*a)[2] && y >= (*a)[1] && y < (*a)[1] + (*a)[3];
    };
    uint32_t found = 0;
    for (const auto &[h, w] : _windows)
        if (!(w.style & kWsChild) && w.visible && inside(h))
            found = h; // the handles go up as they're made: the last is on top
    for (bool deeper = found != 0; deeper;) {
        deeper = false;
        for (const auto &[h, w] : _windows)
            if ((w.style & kWsChild) && w.parent == found && w.visible && w.enabled && inside(h)) {
                found = h;
                deeper = true;
            }
    }
    return found;
}

uint32_t Win32Process::mouseTarget(int &x, int &y) const {
    uint32_t target = _capture && window(_capture) ? _capture : windowAt(cursorX, cursorY);
    if (!target)
        target = mainWindow();
    const Window *w = window(target);
    const auto area = clientArea(target);
    if (!w || !area || !w->enabled)
        return 0;
    x = cursorX - (*area)[0];
    y = cursorY - (*area)[1];
    return target;
}

void Win32Process::destroyWindow(uint32_t handle) {
    // WM_DESTROY to the window, then to its children and the windows it
    // owns (a popup's parent is its owner), as Windows sends it. Its timers
    // go with it.
    sendMessage(handle, kWmDestroy, 0, 0);
    std::vector<uint32_t> children;
    for (const auto &[h, w] : _windows)
        if (w.parent == handle)
            children.push_back(h);
    for (uint32_t child : children)
        if (_windows.count(child))
            destroyWindow(child);
    sendMessage(handle, kWmNcDestroy, 0, 0);
    _windows.erase(handle);
    _windowTimers.erase(std::remove_if(_windowTimers.begin(), _windowTimers.end(),
                                       [&](const WindowTimer &t) { return t.window == handle; }),
                        _windowTimers.end());
    if (_focus == handle)
        _focus = 0;
    if (_capture == handle)
        _capture = 0;
}

void Win32Process::uncover(const std::array<int, 4> &area) {
    _gdi->paintDesktop(area);
    for (const auto &[h, w] : _windows) {
        const auto a = windowArea(h);
        if (!a || !shown(h) || (*a)[0] >= area[0] + area[2] || area[0] >= (*a)[0] + (*a)[2] ||
            (*a)[1] >= area[1] + area[3] || area[1] >= (*a)[1] + (*a)[3])
            continue;
        // Its frame now, as Windows paints it; its client area when the
        // program has painted it. The oldest first, so those above come last.
        _gdi->paintFrame(h, area);
        const bool queued = std::any_of(_messages.begin(), _messages.end(),
                                        [&](const Message &m) { return m.window == h && m.message == 0x000f; });
        if (!queued)
            postMessage(h, 0x000f, 0, 0); // WM_PAINT
    }
}

std::optional<std::array<int, 4>> Win32Process::scrollBarArea(uint32_t handle, int bar) const {
    const Window *w = window(handle);
    const auto inside = clientArea(handle);
    if (!w || !inside || !(w->style & (bar ? kWsVScroll : kWsHScroll)))
        return std::nullopt;
    const auto [x, y, width, height] = *inside;
    if (bar)
        return std::array<int, 4>{x + width, y, kScrollBar, height};
    return std::array<int, 4>{x, y + height, width, kScrollBar};
}

int Win32Process::scrollThumb(uint32_t handle, int bar) const {
    const Window *w = window(handle);
    const auto area = scrollBarArea(handle, bar);
    if (!w || !area)
        return -1;
    const Window::ScrollBar &b = w->scroll[size_t(bar)];
    // Between the two arrows, less the thumb itself.
    const int track = (bar ? (*area)[3] : (*area)[2]) - 3 * kScrollBar;
    if (track < 0 || b.max <= b.min)
        return -1;
    const int pos = std::clamp(b.pos, b.min, b.max);
    return kScrollBar + int(int64_t(pos - b.min) * track / (b.max - b.min));
}

int Win32Process::scrollPart(uint32_t handle, int bar, int x, int y) const {
    const auto area = scrollBarArea(handle, bar);
    if (!area || x < (*area)[0] || x >= (*area)[0] + (*area)[2] || y < (*area)[1] || y >= (*area)[1] + (*area)[3])
        return -1;
    const int along = bar ? y - (*area)[1] : x - (*area)[0];
    const int length = bar ? (*area)[3] : (*area)[2];
    const int thumb = scrollThumb(handle, bar);
    if (along < kScrollBar)
        return int(kSbLineUp);
    if (along >= length - kScrollBar)
        return int(kSbLineDown);
    if (thumb >= 0 && along >= thumb && along < thumb + kScrollBar)
        return kScrollThumb;
    return int(thumb >= 0 && along < thumb ? kSbPageUp : kSbPageDown);
}

// Due while the pointer stays on the part pressed: a page goes on until
// the thumb reaches the pointer.
bool Win32Process::scrollRepeatDue() const {
    if (!_scrollHold || _scrollHold->thumb || milliseconds() < _scrollHold->nextMs)
        return false;
    return scrollPart(_scrollHold->window, _scrollHold->bar, cursorX, cursorY) == int(_scrollHold->request);
}

// Posted, where Windows' own loop would send it: the program takes it in
// its own loop, as its window procedure would have.
void Win32Process::repeatScroll() {
    if (!scrollRepeatDue())
        return;
    postMessage(_scrollHold->window, _scrollHold->bar ? kWmVScroll : kWmHScroll, _scrollHold->request, 0);
    _scrollHold->nextMs = milliseconds() + kScrollRepeatMs;
}

// A thumb dragged to the pointer: the position it stands for, told as it
// changes.
void Win32Process::scrollDrag() {
    ScrollHold &h = *_scrollHold;
    const Window *w = window(h.window);
    const auto area = scrollBarArea(h.window, h.bar);
    if (!w || !area) {
        _scrollHold.reset();
        return;
    }
    const Window::ScrollBar &b = w->scroll[size_t(h.bar)];
    const int track = (h.bar ? (*area)[3] : (*area)[2]) - 3 * kScrollBar;
    const int along = (h.bar ? cursorY - (*area)[1] : cursorX - (*area)[0]) - kScrollBar - h.grab;
    const int pos = track > 0 ? std::clamp(b.min + int(int64_t(along) * (b.max - b.min) / track), b.min, b.max) : b.min;
    if (pos == h.pos)
        return;
    h.pos = pos;
    sendMessage(h.window, h.bar ? kWmVScroll : kWmHScroll, kSbThumbTrack | uint32_t(uint16_t(pos)) << 16, 0);
}

void Win32Process::scrollRelease() {
    const ScrollHold h = *_scrollHold;
    _scrollHold.reset();
    if (!window(h.window))
        return;
    const uint32_t message = h.bar ? kWmVScroll : kWmHScroll;
    if (h.thumb)
        sendMessage(h.window, message, kSbThumbPosition | uint32_t(uint16_t(h.pos)) << 16, 0);
    sendMessage(h.window, message, kSbEndScroll, 0);
}

void Win32Process::paintShown(uint32_t handle) {
    if (!shown(handle))
        return;
    _gdi->paintFrame(handle);
    const bool queued = std::any_of(_messages.begin(), _messages.end(),
                                    [&](const Message &m) { return m.window == handle && m.message == 0x000f; });
    if (!queued)
        postMessage(handle, 0x000f, 0, 0); // WM_PAINT
    for (const auto &[h, w] : _windows)
        if ((w.style & kWsChild) && w.parent == handle && w.visible)
            paintShown(h);
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
constexpr uint32_t kWmTimer = 0x113;
constexpr uint32_t kWmMouseMove = 0x200;
constexpr uint32_t kWmNcMouseMove = 0x00a0;
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
    // A scroll bar held takes the mouse, as Windows' own loop does.
    if (_scrollHold) {
        if (_scrollHold->thumb)
            scrollDrag();
        return;
    }
    int cx = 0;
    int cy = 0;
    const uint32_t w = mouseTarget(cx, cy);
    if (!w)
        return;
    const uint32_t hit = hitTest(w, kWmMouseMove);
    if (hit == kHtClient)
        postMessage(w, kWmMouseMove, _buttons, point(cx, cy));
    else
        postMessage(w, kWmNcMouseMove, hit, point(cursorX, cursorY));
}

// As Windows does before each of the mouse's messages, without a capture:
// WM_NCHITTEST asks the window what's at the pointer, and WM_SETCURSOR
// lets it set its pointer for that and the message to come. Captured, the
// window gets its messages as on its client area.
uint32_t Win32Process::hitTest(uint32_t window, uint32_t message) {
    if (_capture && window == _capture)
        return kHtClient;
    const uint32_t hit = sendMessage(window, kWmNcHitTest, 0, point(cursorX, cursorY));
    // The window may be gone by now.
    if (_windows.count(window))
        sendMessage(window, kWmSetCursor, window, (hit & 0xffff) | message << 16);
    return hit;
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
    if (_scrollHold) {
        if (button == 0 && !down)
            scrollRelease();
        return;
    }
    int cx = 0;
    int cy = 0;
    const uint32_t w = mouseTarget(cx, cy);
    if (!w)
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
    const uint32_t hit = hitTest(w, message);
    if (hit == kHtClient)
        postMessage(w, message, _buttons, point(cx, cy));
    else // WM_NCLBUTTONDOWN and the others, with the hit-test code
        postMessage(w, message - kWmMouseMove + kWmNcMouseMove, hit, point(cursorX, cursorY));
}

void Win32Process::mouseWheel(int delta) {
    notify({RawInput::Kind::kWheel, delta, 0});
    // To the focus window, as Windows sends it; its point is on the screen.
    if (const uint32_t w = _focus && window(_focus) ? _focus : mainWindow())
        postMessage(w, kWmMouseWheel, _buttons | uint32_t(uint16_t(int16_t(delta))) << 16, point(cursorX, cursorY));
}

// WM_KEYDOWN and WM_KEYUP: lParam has the repeat count (1), the scan
// code, and for a release the previous-state and transition bits.
void Win32Process::key(uint32_t virtualKey, uint32_t scanCode, bool down) {
    const bool was = _keysDown[virtualKey & 0xff] != 0;
    _keysDown[virtualKey & 0xff] = down;
    notify({RawInput::Kind::kKey, int(scanCode), down});
    const uint32_t w = _focus && window(_focus) ? _focus : mainWindow();
    if (!w)
        return;
    uint32_t lParam = 1 | (scanCode & 0xff) << 16 | (scanCode & 0x100 ? 1u << 24 : 0); // 0x100: an extended key
    if (down && was)
        lParam |= 1u << 30;
    if (!down)
        lParam |= 3u << 30;
    postMessage(w, down ? kWmKeyDown : kWmKeyUp, virtualKey, lParam);
}

// As Windows does: WM_MOVE with the client area's new corner (on the
// screen, or a child's in its parent's client area), WM_SIZE with its new
// size, and, shown, what it left uncovered and the window itself painted.
void Win32Process::moveWindow(uint32_t handle, int x, int y, int width, int height) {
    auto it = _windows.find(handle);
    if (it == _windows.end())
        return;
    Window &w = it->second;
    const bool moved = x != w.x || y != w.y;
    const bool sized = width != w.width || height != w.height;
    if (!moved && !sized)
        return;
    const auto before = windowArea(handle);
    w.x = x, w.y = y, w.width = width, w.height = height;
    const Insets f = frameOf(w.style, false);
    if (moved)
        sendMessage(handle, kWmMove, 0, point(x + f.left, y + f.top));
    if (sized)
        sendMessage(handle, kWmSize, 0, // SIZE_RESTORED
                    point(std::max(0, width - f.left - f.right), std::max(0, height - f.top - f.bottom)));
    if (shown(handle)) {
        if (before)
            uncover(*before);
        paintShown(handle);
    }
}

std::vector<uint32_t> Win32Process::windows() const {
    std::vector<uint32_t> out;
    for (const auto &[h, w] : _windows)
        out.push_back(h);
    return out;
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
        // Made maximized, the place given is ignored: a window fills the
        // screen (its frame past the edges), a child its parent's client area.
        if (style & kWsMaximize) {
            const auto inside = (style & kWsChild) ? clientArea(parent) : std::nullopt;
            const Insets f = frameOf(style, false);
            x = -f.left;
            y = -f.top;
            w = (inside ? (*inside)[2] : screenWidth) + f.left + f.right;
            h = (inside ? (*inside)[3] : screenHeight) + f.top + f.bottom;
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
        win.id = (style & kWsChild) ? menu : 0;

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
        if (style & kWsVisible) {
            _windows[handle].visible = true;
            paintShown(handle);
        }
        return handle;
    });
    // DestroyWindow: its children go with it; what it covered is painted again.
    stdcall(u, "DestroyWindow", 1, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        if (!_windows.count(handle))
            return 0u;
        const bool wasShown = shown(handle);
        const auto area = windowArea(handle);
        destroyWindow(handle);
        if (wasShown && area)
            uncover(*area);
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
        if (c.arg(1) == 3) { // SW_MAXIMIZE
            Window &w = it->second;
            const auto inside = (w.style & kWsChild) ? clientArea(w.parent) : std::nullopt;
            const Insets f = frameOf(w.style, false);
            w.x = -f.left;
            w.y = -f.top;
            w.width = (inside ? (*inside)[2] : screenWidth) + f.left + f.right;
            w.height = (inside ? (*inside)[3] : screenHeight) + f.top + f.bottom;
            w.style |= kWsMaximize;
        }
        if (show != was)
            sendMessage(handle, kWmShowWindow, show, 0);
        const Window *w = window(handle);
        if (w && show && !was && !(w->style & kWsChild)) {
            _focus = handle;
            sendMessage(handle, kWmActivateApp, 1, 0);
            sendMessage(handle, kWmActivate, 1, 0); // WA_ACTIVE
            sendMessage(handle, kWmSetFocus, 0, 0);
        }
        if (show && !was)
            paintShown(handle);
        if (!show && was)
            if (const auto area = windowArea(handle))
                uncover(*area);
        return was ? 1u : 0u;
    });
    stdcall(u, "UpdateWindow", 1, [this](Cpu &c) { return _windows.count(c.arg(0)) ? 1u : 0u; });
    // SetWindowPos(window, after, x, y, cx, cy, flags): SWP_NOSIZE 1,
    // SWP_NOMOVE 2, SWP_SHOWWINDOW 0x40, SWP_HIDEWINDOW 0x80.
    stdcall(u, "SetWindowPos", 7, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        const uint32_t flags = c.arg(6);
        {
            const Window &w = it->second;
            moveWindow(c.arg(0), (flags & 2) ? w.x : int32_t(c.arg(2)), (flags & 2) ? w.y : int32_t(c.arg(3)),
                       (flags & 1) ? w.width : int32_t(c.arg(4)), (flags & 1) ? w.height : int32_t(c.arg(5)));
        }
        it = _windows.find(c.arg(0)); // its procedure may have destroyed it
        if (it == _windows.end())
            return 1u;
        Window &w = it->second;
        const bool was = w.visible;
        if (flags & 0x40)
            w.visible = true;
        if (flags & 0x80)
            w.visible = false;
        if (w.visible && !was)
            paintShown(c.arg(0));
        if (!w.visible && was)
            if (const auto area = windowArea(c.arg(0)))
                uncover(*area);
        return 1u;
    });
    stdcall(u, "GetWindowRect", 2, [this, rect](Cpu &c) {
        const auto a = windowArea(c.arg(0));
        if (!a || !c.valid(c.arg(1), 16))
            return 0u;
        rect(c, c.arg(1), (*a)[0], (*a)[1], (*a)[0] + (*a)[2], (*a)[1] + (*a)[3]);
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
        const auto area = clientArea(c.arg(0));
        if (!area || !c.valid(c.arg(1), 8))
            return 0u;
        c.write32(c.arg(1), c.read32(c.arg(1)) + uint32_t((*area)[0]));
        c.write32(c.arg(1) + 4, c.read32(c.arg(1) + 4) + uint32_t((*area)[1]));
        return 1u;
    });
    // AdjustWindowRect(rect, style, menu): the window's rectangle for a
    // client rectangle. As documented, scroll bars aren't counted.
    stdcall(u, "AdjustWindowRect", 3, [](Cpu &c) {
        const uint32_t p = c.arg(0);
        if (!c.valid(p, 16))
            return 0u;
        const Insets f = frameOf(c.arg(1) & ~(kWsVScroll | kWsHScroll), c.arg(2) != 0);
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
        case 11:
        case 12:
        case 13:
        case 14:
            return 32u; // SM_CXICON, SM_CYICON, SM_CXCURSOR, SM_CYCURSOR
        case 19:
            return 1u; // SM_MOUSEPRESENT
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
        repeatScroll();
        auto it = std::find_if(_messages.begin(), _messages.end(), matches);
        if (it == _messages.end() && idle) {
            idle();
            it = std::find_if(_messages.begin(), _messages.end(), matches);
        }
        // With nothing else queued, a timer that's due (SetTimer's) gives
        // WM_TIMER, made as it's taken, never queued; peeked without
        // removing, the timer stays due.
        std::optional<Message> timer;
        if (it == _messages.end() && (first == 0 || (first <= kWmTimer && last >= kWmTimer)) && timerDue()) {
            if (const auto t = takeDueTimer(hwnd)) {
                timer = t;
                if (!(remove & 1))
                    for (WindowTimer &w : _windowTimers)
                        if (w.window == t->window && w.id == t->wParam)
                            w.dueMs = milliseconds();
            }
        }
        if ((it == _messages.end() && !timer) || !c.valid(out, 28))
            return 0u;
        const Message m = timer ? *timer : *it;
        const uint32_t fields[7] = {m.window,       m.message,         m.wParam,         m.lParam,
                                    milliseconds(), uint32_t(cursorX), uint32_t(cursorY)};
        for (int i = 0; i < 7; ++i)
            c.write32(out + 4 * uint32_t(i), fields[i]);
        if ((remove & 1) && !timer) // PM_REMOVE
            _messages.erase(it);
        return 1u;
    });
    stdcall(u, "DispatchMessageA", 1, [this](Cpu &c) {
        const uint32_t m = c.arg(0);
        if (!c.valid(m, 16))
            return 0u;
        // WM_TIMER with a procedure (lParam) calls it, not the window's.
        if (c.read32(m + 4) == kWmTimer && c.read32(m + 12)) {
            uint32_t result = 0;
            if (Error e = _cpu.call(c.read32(m + 12), {c.read32(m), kWmTimer, c.read32(m + 8), milliseconds()}, result);
                e && output)
                output("A timer procedure failed: " + e.message + "\n");
            return 0u;
        }
        return sendMessage(c.read32(m), c.read32(m + 4), c.read32(m + 8), c.read32(m + 12));
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
    // command); WM_NCCREATE goes on; the frame's hit-tested and its scroll
    // bars work; everything else needs nothing.
    stdcall(u, "DefWindowProcA", 4, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        const uint32_t message = c.arg(1);
        const uint32_t wParam = c.arg(2);
        if (message == kWmNcCreate)
            return 1u;
        // WM_NCHITTEST: what's at a point on the screen, by the window's
        // frame: its client area, caption, border, or nowhere of it.
        if (message == kWmNcHitTest) {
            const int x = int16_t(c.arg(3) & 0xffff);
            const int y = int16_t(c.arg(3) >> 16);
            const auto whole = windowArea(handle);
            const auto inside = clientArea(handle);
            auto within = [&](const std::array<int, 4> &a) {
                return x >= a[0] && x < a[0] + a[2] && y >= a[1] && y < a[1] + a[3];
            };
            if (!whole || !inside || !within(*whole))
                return 0u; // HTNOWHERE
            if (within(*inside))
                return kHtClient;
            for (int bar = 0; bar < 2; ++bar)
                if (const auto a = scrollBarArea(handle, bar); a && within(*a))
                    return bar ? kHtVScroll : kHtHScroll;
            const Window *w = window(handle);
            const Insets f = frameOf(w->style, false);
            const bool caption = (w->style & kWsCaption) == kWsCaption && y >= (*whole)[1] + f.left &&
                                 y < (*inside)[1] && x >= (*inside)[0] && x < (*inside)[0] + (*inside)[2];
            return caption ? 2u : 18u; // HTCAPTION, HTBORDER
        }
        // WM_SETCURSOR: a child's parent is asked first; on the client area
        // the class's pointer is set (and the answer says so).
        if (message == kWmSetCursor) {
            const Window *w = window(handle);
            if (w && (w->style & kWsChild) && w->parent)
                if (const uint32_t r = sendMessage(w->parent, kWmSetCursor, wParam, c.arg(3)))
                    return r;
            return (c.arg(3) & 0xffff) == kHtClient ? 1u : 0u;
        }
        // WM_NCLBUTTONDOWN on a scroll bar: an arrow scrolls a line, the
        // track a page, again and again while it's held; the thumb is
        // dragged. The bar is held until the button's let go.
        if (message == kWmNcLButtonDown && (wParam == kHtHScroll || wParam == kHtVScroll)) {
            const int bar = wParam == kHtVScroll ? 1 : 0;
            const auto area = scrollBarArea(handle, bar);
            if (!area)
                return 0u;
            const int x = int16_t(c.arg(3) & 0xffff);
            const int y = int16_t(c.arg(3) >> 16);
            const int part = scrollPart(handle, bar, x, y);
            if (part < 0)
                return 0u;
            ScrollHold h;
            h.window = handle;
            h.bar = bar;
            h.pos = window(handle)->scroll[size_t(bar)].pos;
            h.thumb = part == kScrollThumb;
            if (h.thumb) {
                const int along = bar ? y - (*area)[1] : x - (*area)[0];
                h.grab = along - scrollThumb(handle, bar);
            }
            h.request = h.thumb ? 0 : uint32_t(part);
            h.nextMs = milliseconds() + kScrollFirstRepeatMs;
            _scrollHold = h;
            if (!h.thumb)
                sendMessage(handle, bar ? kWmVScroll : kWmHScroll, h.request, 0);
            return 0u;
        }
        if (message == kWmSysCommand && (wParam & 0xfff0) == kScClose)
            return sendMessage(handle, kWmClose, 0, 0);
        if (message == kWmClose && _windows.count(handle)) {
            const bool wasShown = shown(handle);
            const auto area = windowArea(handle);
            destroyWindow(handle);
            if (wasShown && area)
                uncover(*area);
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
    // LoadCursorFromFileA(file): a cursor (.cur) or animated cursor (.ani)
    // file, a handle of its own while the file's there, else NULL. Its
    // pictures aren't read: Reluva doesn't draw Windows' pointer.
    stdcall(u, "LoadCursorFromFileA", 1, [this](Cpu &c) {
        const std::string name = c.string(c.arg(0));
        const auto host = mapPath ? mapPath(fullPath(name), false) : std::nullopt;
        std::error_code ec;
        if (!host || !std::filesystem::is_regular_file(*host, ec)) {
            setLastError(2); // ERROR_FILE_NOT_FOUND
            return 0u;
        }
        _fileCursors.push_back(*host);
        return kFileCursors + uint32_t(_fileCursors.size() - 1);
    });
    // SetCursor(cursor): the pointer's shape, kept; the one before.
    stdcall(u, "SetCursor", 1, [this](Cpu &c) {
        const uint32_t before = _cursor;
        _cursor = c.arg(0);
        return before;
    });
    stdcall(u, "GetCursor", 0, [this](Cpu &) { return _cursor; });
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
    stdcall(u, "GetDC", 1, [](Cpu &c) { return kDeviceContext + (c.arg(0) & 0xffff); });
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

    // Rectangles (left, top, right, bottom; the right and bottom outside).
    // An empty one has no width or no height; what's left empty is set to 0s.
    struct R {
        int32_t l;
        int32_t t;
        int32_t r;
        int32_t b;
    };
    auto readR = [](Cpu &c, uint32_t p) {
        return R{int32_t(c.read32(p)), int32_t(c.read32(p + 4)), int32_t(c.read32(p + 8)), int32_t(c.read32(p + 12))};
    };
    auto writeR = [rect](Cpu &c, uint32_t p, R x) { rect(c, p, x.l, x.t, x.r, x.b); };
    auto empty = [](R x) { return x.r <= x.l || x.b <= x.t; };
    stdcall(u, "SetRect", 5, [rect](Cpu &c) {
        rect(c, c.arg(0), int32_t(c.arg(1)), int32_t(c.arg(2)), int32_t(c.arg(3)), int32_t(c.arg(4)));
        return 1u;
    });
    stdcall(u, "SetRectEmpty", 1, [rect](Cpu &c) {
        rect(c, c.arg(0), 0, 0, 0, 0);
        return 1u;
    });
    stdcall(u, "CopyRect", 2, [readR, writeR](Cpu &c) {
        writeR(c, c.arg(0), readR(c, c.arg(1)));
        return 1u;
    });
    stdcall(u, "IsRectEmpty", 1, [readR, empty](Cpu &c) { return empty(readR(c, c.arg(0))) ? 1u : 0u; });
    stdcall(u, "OffsetRect", 3, [readR, writeR](Cpu &c) {
        R x = readR(c, c.arg(0));
        const int32_t dx = int32_t(c.arg(1));
        const int32_t dy = int32_t(c.arg(2));
        writeR(c, c.arg(0), {x.l + dx, x.t + dy, x.r + dx, x.b + dy});
        return 1u;
    });
    stdcall(u, "InflateRect", 3, [readR, writeR](Cpu &c) {
        R x = readR(c, c.arg(0));
        const int32_t dx = int32_t(c.arg(1));
        const int32_t dy = int32_t(c.arg(2));
        writeR(c, c.arg(0), {x.l - dx, x.t - dy, x.r + dx, x.b + dy});
        return 1u;
    });
    stdcall(u, "IntersectRect", 3, [readR, writeR, empty](Cpu &c) {
        const R a = readR(c, c.arg(1));
        const R b = readR(c, c.arg(2));
        const R x{std::max(a.l, b.l), std::max(a.t, b.t), std::min(a.r, b.r), std::min(a.b, b.b)};
        if (empty(a) || empty(b) || empty(x)) {
            writeR(c, c.arg(0), {0, 0, 0, 0});
            return 0u;
        }
        writeR(c, c.arg(0), x);
        return 1u;
    });
    stdcall(u, "UnionRect", 3, [readR, writeR, empty](Cpu &c) {
        const R a = readR(c, c.arg(1));
        const R b = readR(c, c.arg(2));
        if (empty(a) && empty(b)) {
            writeR(c, c.arg(0), {0, 0, 0, 0});
            return 0u;
        }
        writeR(c, c.arg(0),
               empty(a)   ? b
               : empty(b) ? a
                          : R{std::min(a.l, b.l), std::min(a.t, b.t), std::max(a.r, b.r), std::max(a.b, b.b)});
        return 1u;
    });
    // PtInRect(rect, point) takes the POINT by value: x and y as two arguments.
    stdcall(u, "PtInRect", 3, [readR](Cpu &c) {
        const R x = readR(c, c.arg(0));
        const int32_t px = int32_t(c.arg(1));
        const int32_t py = int32_t(c.arg(2));
        return px >= x.l && px < x.r && py >= x.t && py < x.b ? 1u : 0u;
    });

    // Text: wsprintfA(buffer, format, ...) is cdecl, its output at most 1,024 characters.
    provideCdecl(u, "wsprintfA", [](Cpu &c) {
        const uint32_t sp = c.reg[Cpu::ESP];
        std::string text = formatPrintf(c, c.read32(sp + 8), sp + 12);
        if (text.size() > 1024)
            text.resize(1024);
        c.writeString(c.read32(sp + 4), text, uint32_t(text.size() + 1));
        return uint32_t(text.size());
    });
    stdcall(u, "IsCharAlphaNumericA", 1, [](Cpu &c) {
        const uint8_t ch = uint8_t(c.arg(0));
        // Code page 1252's letters above ASCII: 0xc0 to 0xff but for the
        // multiplication and division signs, and Š, Œ, Ž, š, œ, ž, Ÿ.
        const bool latin = (ch >= 0xc0 && ch != 0xd7 && ch != 0xf7) || ch == 0x8a || ch == 0x8c || ch == 0x8e ||
                           ch == 0x9a || ch == 0x9c || ch == 0x9e || ch == 0x9f;
        return std::isalnum(ch) || latin ? 1u : 0u;
    });
    stdcall(u, "MessageBeep", 1, [](Cpu &) { return 1u; });
}

} // namespace reluva::x86
