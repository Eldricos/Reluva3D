// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's dialogs, as Microsoft documents them: a dialog made from its
// template in the program's resources (DLGTEMPLATE or DLGTEMPLATEEX), its
// items as child windows of the standard classes, its procedure called
// through DefDlgProcA, modal ones run until EndDialog. The controls keep
// their state and answer their messages (buttons' checks, list boxes'
// items, every window's text); they aren't drawn here.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace reluva::x86 {

namespace {

constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kWsChild = 0x40000000;
constexpr uint32_t kWmSetText = 0x000c;
constexpr uint32_t kWmGetText = 0x000d;
constexpr uint32_t kWmGetTextLength = 0x000e;
constexpr uint32_t kWmClose = 0x0010;
constexpr uint32_t kWmQuit = 0x0012;
constexpr uint32_t kWmInitDialog = 0x0110;
constexpr uint32_t kWmCommand = 0x0111;
constexpr int32_t kDwlMsgResult = 0;
constexpr int32_t kDwlDlgProc = 4;
constexpr uint32_t kIdCancel = 2;
constexpr uint32_t kLbErr = 0xffffffffu;
// MS Sans Serif 8's average character size, Windows' dialog base units
// at 96 dots an inch: a dialog unit is a quarter of the width and an eighth
// of the height.
constexpr int kBaseX = 6;
constexpr int kBaseY = 13;

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(uint8_t(c)));
    return s;
}

// A resource's data in a module's image in memory: by type and by id (or
// name), the first language; its address, 0 if none.
uint32_t findResource(Cpu &c, const Win32Process::Module &m, uint32_t type, uint32_t name, uint32_t &size) {
    const uint32_t root = m.image.resourceRva ? m.base + m.image.resourceRva : 0;
    if (!root)
        return 0;
    auto entries = [&](uint32_t dir) {
        std::vector<std::pair<uint32_t, uint32_t>> out; // (name or id, offset)
        const uint32_t n = uint32_t(c.read16(dir + 12)) + c.read16(dir + 14);
        for (uint32_t i = 0; i < n; ++i)
            out.emplace_back(c.read32(dir + 16 + 8 * i), c.read32(dir + 20 + 8 * i));
        return out;
    };
    auto matches = [&](uint32_t key, uint32_t wanted) {
        if (wanted < 0x10000)
            return !(key & 0x80000000u) && key == wanted;
        if (!(key & 0x80000000u))
            return false;
        // A named entry: a counted UTF-16 string, matched ignoring case.
        const uint32_t at = root + (key & 0x7fffffff);
        const uint16_t length = c.read16(at);
        std::string text;
        for (uint16_t i = 0; i < length; ++i)
            text += char(c.read16(at + 2 + 2 * i));
        return lower(text) == lower(c.string(wanted));
    };
    for (const auto &[t, typeOffset] : entries(root)) {
        if (!matches(t, type) || !(typeOffset & 0x80000000u))
            continue;
        for (const auto &[n, nameOffset] : entries(root + (typeOffset & 0x7fffffff))) {
            if (!matches(n, name) || !(nameOffset & 0x80000000u))
                continue;
            const auto languages = entries(root + (nameOffset & 0x7fffffff));
            if (languages.empty())
                return 0;
            const uint32_t entry = root + (languages[0].second & 0x7fffffff);
            size = c.read32(entry + 4);
            return m.base + c.read32(entry);
        }
    }
    return 0;
}

// A template's string or ordinal: 0xffff and a number, or UTF-16 text;
// `at` moves past it.
std::string sz(Cpu &c, uint32_t &at, uint32_t *ordinal = nullptr) {
    if (c.read16(at) == 0xffff) {
        if (ordinal)
            *ordinal = c.read16(at + 2);
        at += 4;
        return "#" + std::to_string(c.read16(at - 2));
    }
    std::string text;
    for (uint16_t ch; (ch = c.read16(at)) != 0; at += 2)
        text += ch < 0x100 ? char(ch) : '?';
    at += 2;
    return text;
}

} // namespace

uint32_t Win32Process::createDialog(Cpu &c, uint32_t instance, uint32_t name, uint32_t parent, uint32_t proc,
                                    uint32_t param) {
    const Module *m = moduleAt(instance);
    if (!m && !modules().empty())
        m = modules().back();
    uint32_t size = 0;
    const uint32_t tpl = m ? findResource(c, *m, 5, name, size) : 0;
    if (!tpl) {
        setLastError(1814); // ERROR_RESOURCE_NAME_NOT_FOUND
        return 0;
    }
    // DLGTEMPLATEEX starts with its version, 1, and 0xffff.
    const bool ex = c.read16(tpl) == 1 && c.read16(tpl + 2) == 0xffff;
    uint32_t at = tpl;
    uint32_t style = 0;
    uint32_t exStyle = 0;
    uint16_t items = 0;
    if (ex) {
        exStyle = c.read32(tpl + 12);
        style = c.read32(tpl + 16);
        items = c.read16(tpl + 20);
        at = tpl + 22;
    } else {
        style = c.read32(tpl);
        exStyle = c.read32(tpl + 4);
        items = c.read16(tpl + 8);
        at = tpl + 10;
    }
    const int x = int16_t(c.read16(at));
    const int y = int16_t(c.read16(at + 2));
    const int cx = int16_t(c.read16(at + 4));
    const int cy = int16_t(c.read16(at + 6));
    at += 8;
    sz(c, at); // menu
    sz(c, at); // class
    const std::string title = sz(c, at);
    if (style & 0x40) { // DS_SETFONT: the size (and, in EX, weight, italic, charset), then the face
        at += ex ? 6 : 2;
        sz(c, at);
    }
    auto px = [](int du) { return du * kBaseX / 4; };
    auto py = [](int du) { return du * kBaseY / 8; };

    const uint32_t dialog = _nextWindow;
    _nextWindow += 4;
    Window &w = _windows[dialog];
    w.className = "#32770";
    w.title = title;
    w.proc = provided("USER32.dll", "DefDlgProcA");
    w.style = style & ~kWsVisible;
    w.exStyle = exStyle;
    w.parent = parent;
    const auto origin = parent ? clientArea(parent) : std::nullopt;
    w.x = px(x) + (origin ? (*origin)[0] : 0);
    w.y = py(y) + (origin ? (*origin)[1] : 0);
    w.width = px(cx);
    w.height = py(cy);
    w.longs[kDwlDlgProc] = proc;

    for (uint16_t i = 0; i < items; ++i) {
        at = (at + 3) & ~3u;
        uint32_t itemStyle = 0;
        uint32_t itemExStyle = 0;
        uint32_t id = 0;
        if (ex) {
            itemExStyle = c.read32(at + 4);
            itemStyle = c.read32(at + 8);
            at += 12;
        } else {
            itemStyle = c.read32(at);
            itemExStyle = c.read32(at + 4);
            at += 8;
        }
        const int ix = int16_t(c.read16(at));
        const int iy = int16_t(c.read16(at + 2));
        const int icx = int16_t(c.read16(at + 4));
        const int icy = int16_t(c.read16(at + 6));
        at += 8;
        if (ex) {
            id = c.read32(at);
            at += 4;
        } else {
            id = c.read16(at);
            at += 2;
        }
        uint32_t classOrdinal = 0;
        std::string className = sz(c, at, &classOrdinal);
        if (classOrdinal) {
            static const char *const kClasses[] = {"BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX"};
            className = classOrdinal >= 0x80 && classOrdinal <= 0x85 ? kClasses[classOrdinal - 0x80] : className;
        }
        const std::string text = sz(c, at);
        at += 2 + c.read16(at); // the creation data
        const uint32_t child = _nextWindow;
        _nextWindow += 4;
        Window &item = _windows[child];
        item.className = className;
        item.title = text;
        item.proc = provided("RELUVA", "ControlProc");
        item.style = itemStyle | kWsChild;
        item.exStyle = itemExStyle;
        item.parent = dialog;
        item.id = id;
        item.x = _windows[dialog].x + px(ix);
        item.y = _windows[dialog].y + py(iy);
        item.width = px(icx);
        item.height = py(icy);
        item.visible = (itemStyle & kWsVisible) != 0;
        _controls[child] = Control();
    }
    // WM_INITDIALOG to the dialog's own procedure: the first item to get
    // the focus, and the caller's parameter.
    if (proc) {
        uint32_t result = 0;
        if (Error e = _cpu.call(proc, {dialog, kWmInitDialog, 0, param}, result); e && output)
            output("A dialog procedure failed: " + e.message + "\n");
    }
    if ((style & kWsVisible) && _windows.count(dialog))
        _windows[dialog].visible = true;
    return dialog;
}

void Win32Process::addDialogStandIns() {
    const char *u = "USER32.dll";
    auto childWithId = [this](uint32_t dialog, uint32_t id) -> uint32_t {
        for (const auto &[h, w] : _windows)
            if (w.parent == dialog && w.id == id)
                return h;
        return 0;
    };

    // DefDlgProcA: the dialog's procedure first; what it doesn't handle,
    // as Windows: WM_CLOSE is IDCANCEL's WM_COMMAND.
    stdcall(u, "DefDlgProcA", 4, [this](Cpu &c) {
        const uint32_t h = c.arg(0);
        const uint32_t message = c.arg(1);
        auto it = _windows.find(h);
        if (it == _windows.end())
            return 0u;
        const uint32_t proc = it->second.longs[kDwlDlgProc];
        uint32_t handled = 0;
        if (proc)
            if (Error e = _cpu.call(proc, {h, message, c.arg(2), c.arg(3)}, handled); e && output)
                output("A dialog procedure failed: " + e.message + "\n");
        if (handled) {
            const auto w = _windows.find(h);
            return message == kWmInitDialog || w == _windows.end() ? handled : w->second.longs[kDwlMsgResult];
        }
        if (message == kWmClose)
            postMessage(h, kWmCommand, kIdCancel, 0);
        return 0u;
    });
    // The standard controls' procedure: their text, a button's check
    // (BM_GETCHECK 0xf0, BM_SETCHECK 0xf1, BM_CLICK 0xf5, an auto radio
    // button unchecking its group), a list box's items (LB_*).
    stdcall("RELUVA", "ControlProc", 4, [this](Cpu &c) -> uint32_t {
        const uint32_t h = c.arg(0);
        const uint32_t message = c.arg(1);
        const uint32_t wp = c.arg(2);
        const uint32_t lp = c.arg(3);
        auto it = _windows.find(h);
        if (it == _windows.end())
            return 0;
        Window &w = it->second;
        Control &ctl = _controls[h];
        switch (message) {
        case kWmSetText:
            w.title = lp ? c.string(lp) : "";
            return 1;
        case kWmGetText: {
            if (!wp)
                return 0;
            const std::string text = w.title.substr(0, wp - 1);
            c.writeString(lp, text, uint32_t(text.size() + 1));
            return uint32_t(text.size());
        }
        case kWmGetTextLength:
            return uint32_t(w.title.size());
        case 0xf0: // BM_GETCHECK
            return ctl.check;
        case 0xf1: // BM_SETCHECK
            ctl.check = wp;
            return 0;
        case 0xf5: { // BM_CLICK: an auto radio button (9) checks itself and unchecks the others
            if ((w.style & 0xf) == 9) {
                for (auto &[other, ow] : _windows)
                    if (ow.parent == w.parent && (ow.style & 0xf) == 9)
                        _controls[other].check = 0;
                ctl.check = 1;
            } else if ((w.style & 0xf) == 3) { // BS_AUTOCHECKBOX
                ctl.check = !ctl.check;
            }
            sendMessage(w.parent, kWmCommand, w.id & 0xffff, h);
            return 0;
        }
        case 0x180: // LB_ADDSTRING
            ctl.items.push_back(c.string(lp));
            ctl.itemData.push_back(0);
            return uint32_t(ctl.items.size() - 1);
        case 0x181: { // LB_INSERTSTRING (wParam -1: at the end)
            const size_t at = int32_t(wp) < 0 || wp > ctl.items.size() ? ctl.items.size() : wp;
            ctl.items.insert(ctl.items.begin() + std::ptrdiff_t(at), c.string(lp));
            ctl.itemData.insert(ctl.itemData.begin() + std::ptrdiff_t(at), 0);
            return uint32_t(at);
        }
        case 0x182: // LB_DELETESTRING
            if (wp >= ctl.items.size())
                return kLbErr;
            ctl.items.erase(ctl.items.begin() + std::ptrdiff_t(wp));
            ctl.itemData.erase(ctl.itemData.begin() + std::ptrdiff_t(wp));
            if (ctl.selection >= int32_t(ctl.items.size()))
                ctl.selection = -1;
            return uint32_t(ctl.items.size());
        case 0x184: // LB_RESETCONTENT
            ctl.items.clear();
            ctl.itemData.clear();
            ctl.selection = -1;
            return 0;
        case 0x186: // LB_SETCURSEL
            if (int32_t(wp) >= int32_t(ctl.items.size()))
                return kLbErr;
            ctl.selection = int32_t(wp);
            return int32_t(wp) < 0 ? kLbErr : wp;
        case 0x188: // LB_GETCURSEL
            return ctl.selection < 0 ? kLbErr : uint32_t(ctl.selection);
        case 0x189: // LB_GETTEXT
            if (wp >= ctl.items.size())
                return kLbErr;
            c.writeString(lp, ctl.items[wp], uint32_t(ctl.items[wp].size() + 1));
            return uint32_t(ctl.items[wp].size());
        case 0x18a: // LB_GETTEXTLEN
            return wp < ctl.items.size() ? uint32_t(ctl.items[wp].size()) : kLbErr;
        case 0x18b: // LB_GETCOUNT
            return uint32_t(ctl.items.size());
        case 0x199: // LB_GETITEMDATA
            return wp < ctl.itemData.size() ? ctl.itemData[wp] : kLbErr;
        case 0x19a: // LB_SETITEMDATA
            if (wp >= ctl.itemData.size())
                return kLbErr;
            ctl.itemData[wp] = lp;
            return 1;
        default:
            return 0;
        }
    });

    // CreateDialogParamA(instance, template, parent, procedure, parameter): modeless.
    stdcall(u, "CreateDialogParamA", 5,
            [this](Cpu &c) { return createDialog(c, c.arg(0), c.arg(1), c.arg(2), c.arg(3), c.arg(4)); });
    // DialogBoxParamA: modal; its messages (and the program's others) are
    // handled here until EndDialog gives its result, the parent disabled
    // meanwhile.
    stdcall(u, "DialogBoxParamA", 5, [this](Cpu &c) {
        const uint32_t parent = c.arg(2);
        const uint32_t dialog = createDialog(c, c.arg(0), c.arg(1), parent, c.arg(3), c.arg(4));
        if (!dialog)
            return 0xffffffffu;
        _windows[dialog].visible = true;
        if (_windows.count(parent))
            _windows[parent].enabled = false;
        while (!_dialogResults.count(dialog) && _windows.count(dialog)) {
            auto it = _messages.begin();
            if (it != _messages.end()) {
                const Message m = *it;
                _messages.erase(it);
                if (m.message == kWmQuit) {
                    postMessage(0, kWmQuit, m.wParam, 0); // left for the program's own loop
                    break;
                }
                sendMessage(m.window, m.message, m.wParam, m.lParam);
                continue;
            }
            if (idle)
                idle();
            if (hostThreads()) {
                if (!wait([&] { return !_messages.empty() || _dialogResults.count(dialog) > 0; }, 10) && ending())
                    break;
            } else {
                runThreads();
            }
        }
        const auto result = _dialogResults.find(dialog);
        const uint32_t value = result == _dialogResults.end() ? 0xffffffffu : result->second;
        _dialogResults.erase(dialog);
        if (_windows.count(parent))
            _windows[parent].enabled = true;
        for (auto it = _windows.begin(); it != _windows.end();)
            it = it->first == dialog || it->second.parent == dialog ? _windows.erase(it) : std::next(it);
        return value;
    });
    // EndDialog(dialog, result): a modal one's loop returns the result; a
    // modeless one is hidden.
    stdcall(u, "EndDialog", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        it->second.visible = false;
        _dialogResults[c.arg(0)] = c.arg(1);
        return 1u;
    });
    stdcall(u, "GetDlgItem", 2, [childWithId](Cpu &c) { return childWithId(c.arg(0), c.arg(1)); });
    stdcall(u, "SendDlgItemMessageA", 5, [this, childWithId](Cpu &c) {
        const uint32_t child = childWithId(c.arg(0), c.arg(1));
        return child ? sendMessage(child, c.arg(2), c.arg(3), c.arg(4)) : 0u;
    });
}

} // namespace reluva::x86
