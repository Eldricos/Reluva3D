// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's dialog boxes as Microsoft's documentation describes them:
// DLGTEMPLATE and DLGTEMPLATEEX, the dialog functions and messages, and
// the controls' messages a dialog's procedure sends (winuser.h). Nothing
// is drawn: the engine is told what a dialog holds, and a modal one is
// answered (docs/common/x86/windows.md).

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kWmSetText = 0xc,
    kWmGetText = 0xd,
    kWmGetTextLength = 0xe,
    kWmClose = 0x10,
    kWmKeyDown = 0x100,
    kWmInitDialog = 0x110,
    kWmCommand = 0x111,
    kBmGetCheck = 0xf0,
    kBmSetCheck = 0xf1,
    kCbAddString = 0x143,
    kCbGetCount = 0x146,
    kCbGetCurSel = 0x147,
    kCbGetLbText = 0x148,
    kCbGetLbTextLen = 0x149,
    kCbResetContent = 0x14b,
    kCbSetCurSel = 0x14e,
    kLbAddString = 0x180,
    kLbResetContent = 0x184,
    kLbSetCurSel = 0x186,
    kLbGetCurSel = 0x188,
    kLbGetText = 0x189,
    kLbGetTextLen = 0x18a,
    kLbGetCount = 0x18b,
};
constexpr uint32_t kRtDialog = 5;
constexpr uint32_t kIdOk = 1;
constexpr uint32_t kIdCancel = 2;
constexpr uint32_t kVkReturn = 0x0d;
constexpr uint32_t kVkEscape = 0x1b;
constexpr uint32_t kListError = 0xffffffffu; // LB_ERR and CB_ERR
constexpr uint32_t kWsChild = 0x40000000;
constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kDsSetFont = 0x40;
constexpr uint32_t kDsCenter = 0x800;
constexpr uint32_t kBsTypeMask = 0xf;
constexpr uint32_t kBsDefPushButton = 0x1;
constexpr uint32_t kErrorResourceNotFound = 1814; // ERROR_RESOURCE_NAME_NOT_FOUND
constexpr uint32_t kErrorInvalidWindowHandle = 1400;
constexpr uint32_t kErrorControlIdNotFound = 1421;

// The classes a template names by atom (0xffff then the atom).
std::string atomClass(uint32_t atom) {
    switch (atom) {
    case 0x80:
        return "Button";
    case 0x81:
        return "Edit";
    case 0x82:
        return "Static";
    case 0x83:
        return "ListBox";
    case 0x84:
        return "ScrollBar";
    case 0x85:
        return "ComboBox";
    default:
        return "#" + std::to_string(atom);
    }
}

std::string lowerText(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// A template's sz_Or_Ord at `at`: nothing (0), an ordinal (0xffff and a
// word, as "#n" or by atomClass), or a string of 16-bit characters, taken
// as Latin-1. Moves `at` past it.
std::string sizeOrOrdinal(Cpu &c, uint32_t &at, bool isClass) {
    const uint16_t first = c.read16(at);
    if (first == 0) {
        at += 2;
        return {};
    }
    if (first == 0xffff) {
        const uint16_t ordinal = c.read16(at + 2);
        at += 4;
        return isClass ? atomClass(ordinal) : "#" + std::to_string(ordinal);
    }
    std::string s;
    for (uint16_t ch; (ch = c.read16(at)) != 0 && s.size() < 4096; at += 2)
        s += ch < 256 ? char(ch) : '?';
    at += 2;
    return s;
}

} // namespace

uint32_t Win32Process::findResource(uint32_t module, uint32_t type, uint32_t name, uint32_t *size) {
    const Module *m = moduleAt(module ? module : exeBase());
    if (!m || !m->image.resourceRva)
        return 0;
    Cpu &c = _cpu;
    const uint32_t resources = m->base + m->image.resourceRva;
    // A directory's entry for an id, or a name matched ignoring case (an
    // entry's id with its top bit set is the offset of the name: a length,
    // then its 16-bit characters); or its first, for `any`.
    const std::string wanted = name >= 0x10000 ? lowerText(c.string(name)) : std::string();
    auto entry = [&](uint32_t directory, uint32_t id, bool byName, bool any) {
        const uint32_t at = resources + (directory & 0x7fffffff);
        const uint32_t count = uint32_t(c.read16(at + 12)) + c.read16(at + 14);
        for (uint32_t i = 0; i < count && i < 4096; ++i) {
            const uint32_t key = c.read32(at + 16 + i * 8);
            bool match = any || (!byName && key == id);
            if (byName && (key & 0x80000000)) {
                const uint32_t s = resources + (key & 0x7fffffff);
                std::string text;
                for (uint32_t k = 0; k < c.read16(s); ++k)
                    text += char(std::tolower(c.read16(s + 2 + 2 * k) & 0xff));
                match = text == wanted;
            }
            if (match)
                return c.read32(at + 20 + i * 8);
        }
        return 0u;
    };
    const uint32_t byType = entry(0, type, false, false);
    const uint32_t byName = byType & 0x80000000 ? entry(byType, name, name >= 0x10000, false) : 0;
    const uint32_t language = byName & 0x80000000 ? entry(byName, 0, false, true) : 0;
    if (!language || language & 0x80000000)
        return 0;
    if (size)
        *size = c.read32(resources + language + 4);
    return m->base + c.read32(resources + language);
}

uint32_t Win32Process::makeDialog(uint32_t dialogTemplate, uint32_t parent, uint32_t proc, uint32_t param) {
    Cpu &c = _cpu;
    if (!c.valid(dialogTemplate, 18))
        return 0;
    // DLGTEMPLATEEX starts with its version, 1, and 0xffff.
    const bool extended = c.read16(dialogTemplate) == 1 && c.read16(dialogTemplate + 2) == 0xffff;
    uint32_t at = dialogTemplate;
    const uint32_t style = c.read32(at + (extended ? 12 : 0));
    const uint32_t exStyle = c.read32(at + (extended ? 8 : 4));
    at += extended ? 16 : 8;
    const uint32_t count = c.read16(at);
    const int dx = int16_t(c.read16(at + 2));
    const int dy = int16_t(c.read16(at + 4));
    const int dw = int16_t(c.read16(at + 6));
    const int dh = int16_t(c.read16(at + 8));
    at += 10;
    sizeOrOrdinal(c, at, false); // its menu
    sizeOrOrdinal(c, at, true);  // its class: dialogs' own unless named
    const std::string caption = sizeOrOrdinal(c, at, false);
    Dialog d;
    d.proc = proc;
    if (style & kDsSetFont) {
        at += extended ? 6 : 2; // the point size; the weight, italic and charset
        sizeOrOrdinal(c, at, false);
        // MS Sans Serif's average size, which the dialogs of the time named.
        d.baseX = 6;
        d.baseY = 13;
    }
    auto px = [&d](int units) { return units * d.baseX / 4; };
    auto py = [&d](int units) { return units * d.baseY / 8; };

    const uint32_t handle = _nextWindow;
    _nextWindow += 4;
    Window &win = _windows[handle];
    win.className = "#32770";
    win.title = caption;
    win.proc = _dialogProc;
    win.style = style & ~kWsVisible;
    win.exStyle = exStyle;
    win.width = px(dw);
    win.height = py(dh);
    win.x = px(dx);
    win.y = py(dy);
    if (style & kDsCenter) {
        win.x = (screenWidth - win.width) / 2;
        win.y = (screenHeight - win.height) / 2;
    }
    win.parent = parent;
    _dialogs[handle] = d;

    // The controls, each on a DWORD boundary.
    uint32_t firstControl = 0;
    for (uint32_t i = 0; i < count && i < 1024; ++i) {
        at = (at + 3) & ~3u;
        if (!c.valid(at, 24))
            break;
        const uint32_t cStyle = c.read32(at + (extended ? 8 : 0));
        const uint32_t cExStyle = c.read32(at + 4);
        at += extended ? 12 : 8;
        const int x = int16_t(c.read16(at));
        const int y = int16_t(c.read16(at + 2));
        const int w = int16_t(c.read16(at + 4));
        const int h = int16_t(c.read16(at + 6));
        const uint32_t id = extended ? c.read32(at + 8) : c.read16(at + 8);
        at += extended ? 12 : 10;
        Window control;
        control.className = sizeOrOrdinal(c, at, true);
        control.title = sizeOrOrdinal(c, at, false);
        at += 2 + c.read16(at); // creation data: its size, then it
        control.proc = _controlProc;
        control.style = (cStyle | kWsChild) & ~kWsVisible;
        control.exStyle = cExStyle;
        control.x = px(x), control.y = py(y), control.width = px(w), control.height = py(h);
        control.visible = cStyle & kWsVisible;
        control.parent = handle;
        control.id = id;
        if (lowerText(control.className) == "button" && (cStyle & kBsTypeMask) == kBsDefPushButton)
            _dialogs[handle].defaultButton = id;
        const uint32_t ch = _nextWindow;
        _nextWindow += 4;
        _windows[ch] = control;
        if (!firstControl)
            firstControl = ch;
    }
    _windows[handle].visible = style & kWsVisible;
    sendMessage(handle, kWmInitDialog, firstControl, param);
    return _windows.count(handle) ? handle : 0;
}

uint32_t Win32Process::showDialog(uint32_t dialog, bool modal) {
    DialogShown shown;
    shown.window = dialog;
    shown.modal = modal;
    if (const Window *w = window(dialog))
        shown.caption = w->title;
    if (auto d = _dialogs.find(dialog); d != _dialogs.end())
        shown.defaultButton = d->second.defaultButton;
    for (const auto &[h, w] : _windows)
        if (w.parent == dialog && (w.style & kWsChild))
            shown.controls.push_back({w.id, w.className, w.title});
    if (dialogBox)
        return dialogBox(shown);
    if (output) {
        std::string text = "dialog box \"" + shown.caption + "\":";
        for (const auto &control : shown.controls)
            if (!control.text.empty())
                text += " [" + control.text + "]";
        output(text + (modal ? ", answered " + std::to_string(shown.defaultButton) : std::string()) + "\n");
    }
    return 0;
}

void Win32Process::addDialogStandIns() {
    const char *u = "USER32.dll";
    // A dialog's window procedure: its DLGPROC first; WM_CLOSE, which the
    // procedure didn't take, is IDCANCEL pressed.
    _dialogProc = _cpu.addNative("USER32.dll!DefDlgProc", [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        const uint32_t message = c.arg(1);
        uint32_t result = 0;
        auto d = _dialogs.find(handle);
        if (d != _dialogs.end() && d->second.proc)
            if (Error e = c.call(d->second.proc, {handle, message, c.arg(2), c.arg(3)}, result); e && output)
                output("A dialog procedure failed: " + e.message + "\n");
        if (!result && message == kWmClose)
            sendMessage(handle, kWmCommand, kIdCancel, 0);
        c.reg[Cpu::EAX] = result;
        return 16u;
    });
    // The controls: their text, a button's check, a list's items.
    _controlProc = _cpu.addNative("USER32.dll!ControlProc", [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        uint32_t result = 0;
        if (it != _windows.end()) {
            Window &w = it->second;
            const uint32_t wp = c.arg(2);
            const uint32_t lp = c.arg(3);
            auto text = [&](const std::string &s, uint32_t buffer, uint32_t size) {
                if (!size || !c.valid(buffer, size))
                    return 0u;
                const std::string cut = s.substr(0, size - 1);
                c.writeString(buffer, cut, size);
                return uint32_t(cut.size());
            };
            auto item = [&](uint32_t index) -> const std::string * {
                return index < w.items.size() ? &w.items[index] : nullptr;
            };
            switch (c.arg(1)) {
            case kWmSetText:
                w.title = lp ? c.string(lp) : std::string();
                result = 1;
                break;
            case kWmGetText:
                result = text(w.title, lp, wp);
                break;
            case kWmGetTextLength:
                result = uint32_t(w.title.size());
                break;
            case kBmGetCheck:
                result = w.checked;
                break;
            case kBmSetCheck:
                w.checked = wp;
                break;
            case kLbAddString:
            case kCbAddString:
                w.items.push_back(lp ? c.string(lp) : std::string());
                result = uint32_t(w.items.size() - 1);
                break;
            case kLbResetContent:
            case kCbResetContent:
                w.items.clear();
                w.selection = -1;
                break;
            case kLbGetCount:
            case kCbGetCount:
                result = uint32_t(w.items.size());
                break;
            case kLbSetCurSel:
            case kCbSetCurSel:
                w.selection = int32_t(wp) >= 0 && wp < w.items.size() ? int(wp) : -1;
                result = w.selection < 0 && int32_t(wp) != -1 ? kListError : uint32_t(w.selection);
                break;
            case kLbGetCurSel:
            case kCbGetCurSel:
                result = w.selection < 0 ? kListError : uint32_t(w.selection);
                break;
            case kLbGetText:
            case kCbGetLbText:
                // The item and its NUL into a buffer the program sized.
                if (const std::string *s = item(wp); s && c.valid(lp, uint32_t(s->size() + 1))) {
                    c.writeString(lp, *s, uint32_t(s->size() + 1));
                    result = uint32_t(s->size());
                } else {
                    result = kListError;
                }
                break;
            case kLbGetTextLen:
            case kCbGetLbTextLen:
                result = item(wp) ? uint32_t(item(wp)->size()) : kListError;
                break;
            default:
                break;
            }
        }
        c.reg[Cpu::EAX] = result;
        return 16u;
    });

    // The control `id` of `dialog`, 0 if there's none.
    auto control = [this](uint32_t dialog, uint32_t id) {
        for (const auto &[h, w] : _windows)
            if (w.parent == dialog && (w.style & kWsChild) && w.id == id)
                return h;
        return 0u;
    };
    auto missing = [this](uint32_t dialog) {
        setLastError(_windows.count(dialog) ? kErrorControlIdNotFound : kErrorInvalidWindowHandle);
        return 0u;
    };

    // CreateDialogParamA(instance, template, parent, procedure, parameter),
    // the template a resource by name or id; CreateDialogIndirectParamA's
    // in memory. Modeless: the program runs it.
    stdcall(u, "CreateDialogParamA", 5, [this](Cpu &c) {
        const uint32_t t = findResource(c.arg(0), kRtDialog, c.arg(1), nullptr);
        if (!t) {
            setLastError(kErrorResourceNotFound);
            return 0u;
        }
        const uint32_t dialog = makeDialog(t, c.arg(2), c.arg(3), c.arg(4));
        if (dialog)
            showDialog(dialog, false);
        return dialog;
    });
    stdcall(u, "CreateDialogIndirectParamA", 5, [this](Cpu &c) {
        const uint32_t dialog = makeDialog(c.arg(1), c.arg(2), c.arg(3), c.arg(4));
        if (dialog)
            showDialog(dialog, false);
        return dialog;
    });
    // DialogBoxParamA, and its Indirect form: modal. No one sees it, so the
    // engine (dialogBox) answers it, by default with its default button,
    // pressed as WM_COMMAND; if that doesn't end it, IDCANCEL; if nor
    // that, it ends as cancelled. Returns EndDialog's result, -1 if the
    // dialog can't be made.
    auto modal = [this, control](uint32_t t, uint32_t parent, uint32_t proc, uint32_t param) {
        const uint32_t dialog = t ? makeDialog(t, parent, proc, param) : 0;
        if (!dialog)
            return 0xffffffffu;
        auto ended = [&] {
            auto d = _dialogs.find(dialog);
            return d == _dialogs.end() || d->second.ended;
        };
        if (!ended()) {
            uint32_t press = showDialog(dialog, true);
            if (!press && _dialogs.count(dialog))
                press = _dialogs[dialog].defaultButton;
            for (const uint32_t id : {press, kIdCancel})
                if (!ended())
                    sendMessage(dialog, kWmCommand, id, control(dialog, id));
        }
        auto d = _dialogs.find(dialog);
        const uint32_t result = d != _dialogs.end() && d->second.ended ? d->second.result : kIdCancel;
        if (_windows.count(dialog)) {
            const uint32_t destroy = provided("USER32.dll", "DestroyWindow");
            uint32_t ignored = 0;
            _cpu.call(destroy, {dialog}, ignored);
        }
        return result;
    };
    stdcall(u, "DialogBoxParamA", 5, [this, modal](Cpu &c) {
        const uint32_t t = findResource(c.arg(0), kRtDialog, c.arg(1), nullptr);
        if (!t)
            setLastError(kErrorResourceNotFound);
        return modal(t, c.arg(2), c.arg(3), c.arg(4));
    });
    stdcall(u, "DialogBoxIndirectParamA", 5, [modal](Cpu &c) { return modal(c.arg(1), c.arg(2), c.arg(3), c.arg(4)); });
    // EndDialog(dialog, result): DialogBoxParamA returns `result`; a
    // modeless dialog is only hidden, as Windows does.
    stdcall(u, "EndDialog", 2, [this](Cpu &c) {
        auto d = _dialogs.find(c.arg(0));
        if (d == _dialogs.end()) {
            setLastError(kErrorInvalidWindowHandle);
            return 0u;
        }
        d->second.ended = true;
        d->second.result = c.arg(1);
        _windows[c.arg(0)].visible = false;
        return 1u;
    });
    // DefDlgProcA(dialog, message, wParam, lParam): what's left after the
    // procedure; nothing.
    stdcall(u, "DefDlgProcA", 4, [](Cpu &) { return 0u; });
    stdcall(u, "GetDlgItem", 2, [control, missing](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        return h ? h : missing(c.arg(0));
    });
    stdcall(u, "GetDlgCtrlID", 1, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        return w ? w->id : 0u;
    });
    // SendDlgItemMessageA(dialog, id, message, wParam, lParam).
    stdcall(u, "SendDlgItemMessageA", 5, [this, control](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        return h ? sendMessage(h, c.arg(2), c.arg(3), c.arg(4)) : 0u;
    });
    stdcall(u, "SetDlgItemTextA", 3, [this, control, missing](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        return h ? sendMessage(h, kWmSetText, 0, c.arg(2)) : missing(c.arg(0));
    });
    // GetDlgItemTextA(dialog, id, buffer, size): the characters copied.
    stdcall(u, "GetDlgItemTextA", 4, [this, control, missing](Cpu &c) {
        if (c.arg(3) && c.valid(c.arg(2), 1))
            c.write8(c.arg(2), 0);
        const uint32_t h = control(c.arg(0), c.arg(1));
        return h ? sendMessage(h, kWmGetText, c.arg(3), c.arg(2)) : missing(c.arg(0));
    });
    // SetDlgItemInt(dialog, id, value, signed), GetDlgItemInt(dialog, id,
    // BOOL *translated, signed).
    stdcall(u, "SetDlgItemInt", 4, [this, control, missing](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        if (!h)
            return missing(c.arg(0));
        _windows[h].title = c.arg(3) ? std::to_string(int32_t(c.arg(2))) : std::to_string(c.arg(2));
        return 1u;
    });
    stdcall(u, "GetDlgItemInt", 4, [this, control](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        const std::string s = h ? _windows[h].title : std::string();
        char *end = nullptr;
        const long long v = std::strtoll(s.c_str(), &end, 10);
        const bool ok = h && !s.empty() && end && *end == 0 && (c.arg(3) || v >= 0);
        if (c.arg(2) && c.valid(c.arg(2), 4))
            c.write32(c.arg(2), ok ? 1 : 0);
        return ok ? uint32_t(v) : 0u;
    });
    stdcall(u, "CheckDlgButton", 3, [this, control, missing](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        if (!h)
            return missing(c.arg(0));
        _windows[h].checked = c.arg(2);
        return 1u;
    });
    stdcall(u, "IsDlgButtonChecked", 2, [this, control](Cpu &c) {
        const uint32_t h = control(c.arg(0), c.arg(1));
        return h ? _windows[h].checked : 0u;
    });
    // CheckRadioButton(dialog, first, last, id): `id` alone checked of them.
    stdcall(u, "CheckRadioButton", 4, [this](Cpu &c) {
        for (auto &[h, w] : _windows)
            if (w.parent == c.arg(0) && (w.style & kWsChild) && w.id >= c.arg(1) && w.id <= c.arg(2))
                w.checked = w.id == c.arg(3) ? 1 : 0;
        return 1u;
    });
    // IsDialogMessageA(dialog, message): a message for the dialog or its
    // controls is the dialog's: Enter presses its default button, Escape
    // IDCANCEL, and the rest go to their windows.
    stdcall(u, "IsDialogMessageA", 2, [this, control](Cpu &c) {
        const uint32_t dialog = c.arg(0);
        const uint32_t m = c.arg(1);
        if (!_dialogs.count(dialog) || !c.valid(m, 16))
            return 0u;
        const uint32_t target = c.read32(m);
        const Window *w = window(target);
        if (target != dialog && (!w || w->parent != dialog))
            return 0u;
        const uint32_t message = c.read32(m + 4);
        const uint32_t key = c.read32(m + 8);
        if (message == kWmKeyDown && (key == kVkReturn || key == kVkEscape)) {
            const uint32_t id = key == kVkReturn ? _dialogs[dialog].defaultButton : kIdCancel;
            sendMessage(dialog, kWmCommand, id, control(dialog, id));
        } else {
            sendMessage(target, message, key, c.read32(m + 12));
        }
        return 1u;
    });
    // MapDialogRect(dialog, RECT *): dialog units to pixels.
    stdcall(u, "MapDialogRect", 2, [this](Cpu &c) {
        auto d = _dialogs.find(c.arg(0));
        if (d == _dialogs.end() || !c.valid(c.arg(1), 16))
            return 0u;
        for (uint32_t k = 0; k < 4; ++k) {
            const int v = int32_t(c.read32(c.arg(1) + 4 * k));
            c.write32(c.arg(1) + 4 * k, uint32_t(k % 2 ? v * d->second.baseY / 8 : v * d->second.baseX / 4));
        }
        return 1u;
    });
    // GetDialogBaseUnits: the system font's, 8 across and 16 down.
    stdcall(u, "GetDialogBaseUnits", 0, [](Cpu &) { return 16u << 16 | 8u; });

    // Any window's text: its title, or a control's.
    stdcall(u, "SetWindowTextA", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end())
            return 0u;
        it->second.title = c.arg(1) ? c.string(c.arg(1)) : std::string();
        return 1u;
    });
    stdcall(u, "GetWindowTextA", 3, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (!c.arg(2) || !c.valid(c.arg(1), c.arg(2)))
            return 0u;
        const std::string s = it == _windows.end() ? std::string() : it->second.title.substr(0, c.arg(2) - 1);
        c.writeString(c.arg(1), s, c.arg(2));
        return uint32_t(s.size());
    });
    stdcall(u, "GetWindowTextLengthA", 1, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        return it == _windows.end() ? 0u : uint32_t(it->second.title.size());
    });
    stdcall(u, "GetParent", 1, [this](Cpu &c) {
        const Window *w = window(c.arg(0));
        return w ? w->parent : 0u;
    });
    // EnableWindow(window, enable): whether it was disabled; nothing is.
    stdcall(u, "EnableWindow", 2, [](Cpu &) { return 0u; });
}

} // namespace reluva::x86
