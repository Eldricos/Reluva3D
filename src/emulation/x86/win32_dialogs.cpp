// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's dialogs and window timers, as Microsoft documents them, and the
// resources dialogs are made from. A modal dialog is a window whose
// procedure is the program's dialog procedure, with a record of each
// control; DialogBoxParamA runs the messages until EndDialog. Nothing is
// drawn: a program that shows its dialogs itself (through DirectDraw,
// say) needs only the messages.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace reluva::x86 {

namespace {

enum : uint32_t {
    kWmDestroy = 0x2,
    kWmNcDestroy = 0x82,
    kWmQuit = 0x12,
    kWmShowWindow = 0x18,
    kWmInitDialog = 0x110,
};
constexpr uint32_t kRtDialog = 5;
constexpr uint32_t kWsVisible = 0x10000000;
constexpr uint32_t kWsTabStop = 0x00010000;
constexpr uint32_t kErrorResourceNotFound = 1812; // ERROR_RESOURCE_NAME_NOT_FOUND
constexpr uint32_t kErrorInvalidWindow = 1400;    // ERROR_INVALID_WINDOW_HANDLE
constexpr uint32_t kUserTimerMinimum = 10;
// A dialog unit is a quarter of the font's average character width
// across, an eighth of its height down: for MS Sans Serif 8 at 96 dpi,
// 6 and 13 pixels (guessed from its metrics; dialogs here aren't drawn).
constexpr int kBaseX = 6;
constexpr int kBaseY = 13;

std::string upper(std::string s) {
    for (char &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// The names of the predefined control classes, by their atoms in a template.
std::string controlClass(uint32_t atom) {
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

/** A dialog template (DLGTEMPLATE or DLGTEMPLATEEX), its sizes in dialog units. */
struct DialogTemplate {
    struct Item {
        uint32_t style = 0;
        uint32_t exStyle = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        uint32_t id = 0;
        std::string className;
        std::string title;
    };
    uint32_t style = 0;
    uint32_t exStyle = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::string title;
    std::vector<Item> items;
};

/** Reads templates from memory, each field little-endian, strings in UTF-16. */
class TemplateReader {
public:
    TemplateReader(Cpu &c, uint32_t at, uint32_t end) : _c(c), _at(at), _end(end) {}
    bool ok() const { return _at <= _end; }
    uint16_t u16() {
        const uint16_t v = _at + 2 <= _end ? uint16_t(_c.read16(_at)) : 0;
        _at += 2;
        return v;
    }
    uint32_t u32() {
        const uint32_t lo = u16();
        return lo | uint32_t(u16()) << 16;
    }
    // A string, or 0xffff and a number (returned as "#n" when `atom` is null).
    std::string nameOrNumber(uint32_t *atom = nullptr) {
        if (_at + 2 <= _end && _c.read16(_at) == 0xffff) {
            u16();
            const uint32_t n = u16();
            if (atom)
                *atom = n;
            return "#" + std::to_string(n);
        }
        std::string s;
        for (uint16_t ch = u16(); ch && ok(); ch = u16())
            s += ch < 0x80 ? char(ch) : '?';
        return s;
    }
    void skip(uint32_t n) { _at += n; }
    void alignTo4() { _at = (_at + 3) & ~3u; }

private:
    Cpu &_c;
    uint32_t _at;
    uint32_t _end;
};

bool readTemplate(Cpu &c, uint32_t at, uint32_t size, DialogTemplate &out) {
    TemplateReader r(c, at, at + size);
    const bool extended = c.read16(at) == 1 && c.read16(at + 2) == 0xffff;
    uint32_t count = 0;
    if (extended) {
        r.skip(8); // version, signature, help ID
        out.exStyle = r.u32();
        out.style = r.u32();
    } else {
        out.style = r.u32();
        out.exStyle = r.u32();
    }
    count = r.u16();
    out.x = int16_t(r.u16());
    out.y = int16_t(r.u16());
    out.width = int16_t(r.u16());
    out.height = int16_t(r.u16());
    r.nameOrNumber(); // menu
    r.nameOrNumber(); // class
    out.title = r.nameOrNumber();
    if (out.style & 0x48) { // DS_SETFONT, DS_SHELLFONT: size (and weight, italic, character set), name
        r.skip(extended ? 6 : 2);
        r.nameOrNumber();
    }
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        r.alignTo4();
        DialogTemplate::Item item;
        if (extended) {
            r.skip(4); // help ID
            item.exStyle = r.u32();
            item.style = r.u32();
        } else {
            item.style = r.u32();
            item.exStyle = r.u32();
        }
        item.x = int16_t(r.u16());
        item.y = int16_t(r.u16());
        item.width = int16_t(r.u16());
        item.height = int16_t(r.u16());
        item.id = extended ? r.u32() : r.u16();
        uint32_t atom = 0;
        item.className = r.nameOrNumber(&atom);
        if (atom)
            item.className = controlClass(atom);
        item.title = r.nameOrNumber();
        r.skip(r.u16()); // creation data
        out.items.push_back(item);
    }
    return r.ok();
}

} // namespace

std::optional<std::pair<uint32_t, uint32_t>> Win32Process::findResource(uint32_t instance, uint32_t type,
                                                                        uint32_t name) {
    const Module *m = moduleAt(instance);
    if (!m || !m->image.resourceRva)
        return std::nullopt;
    Cpu &c = _cpu;
    const uint32_t root = m->base + m->image.resourceRva;
    // An entry matching `key`, a number or a name (the address of one): the
    // offset (from the root) it leads to.
    auto find = [&](uint32_t dir, uint32_t key) -> std::optional<uint32_t> {
        const uint32_t at = root + dir;
        if (!c.valid(at, 16))
            return std::nullopt;
        const uint32_t named = c.read16(at + 12);
        const uint32_t numbered = c.read16(at + 14);
        const std::string wanted = key > 0xffff ? upper(c.string(key)) : std::string();
        for (uint32_t i = 0; i < named + numbered; ++i) {
            const uint32_t e = at + 16 + 8 * i;
            const uint32_t id = c.read32(e);
            bool match;
            if (key == 0xffffffffu) {
                match = true; // the first: a language
            } else if (id & 0x80000000u) {
                const uint32_t s = root + (id & 0x7fffffff);
                std::string n;
                for (uint32_t k = 0, len = c.read16(s); k < len; ++k)
                    n += char(c.read16(s + 2 + 2 * k));
                match = key > 0xffff && upper(n) == wanted;
            } else {
                match = key <= 0xffff && id == key;
            }
            if (match)
                return c.read32(e + 4) & 0x7fffffff;
        }
        return std::nullopt;
    };
    // "#123" names the number 123.
    auto number = [&](uint32_t key) {
        if (key > 0xffff && c.read8(key) == '#')
            return uint32_t(std::strtoul(c.string(key + 1).c_str(), nullptr, 10));
        return key;
    };
    const auto byType = find(0, number(type));
    const auto byName = byType ? find(*byType, number(name)) : std::nullopt;
    const auto byLanguage = byName ? find(*byName, 0xffffffffu) : std::nullopt;
    if (!byLanguage || !c.valid(root + *byLanguage, 16))
        return std::nullopt;
    const uint32_t entry = root + *byLanguage;
    return std::make_pair(m->base + c.read32(entry), c.read32(entry + 4));
}

void Win32Process::addDialogStandIns() {
    const char *u = "USER32.dll";

    // DialogBoxParamA(instance, template, owner, procedure, param): makes
    // the dialog and its controls, sends WM_INITDIALOG, shows it, and runs
    // the messages until EndDialog; the owner is disabled meanwhile.
    // Returns what EndDialog was given, or -1.
    stdcall(u, "DialogBoxParamA", 5, [this](Cpu &c) {
        const uint32_t owner = c.arg(2);
        const auto resource = findResource(c.arg(0), kRtDialog, c.arg(1));
        DialogTemplate t;
        if (!resource || !readTemplate(c, resource->first, resource->second, t)) {
            setLastError(kErrorResourceNotFound);
            return uint32_t(-1);
        }
        int ownerX = 0;
        int ownerY = 0;
        if (const Window *o = window(owner)) {
            ownerX = o->x;
            ownerY = o->y;
        }
        const uint32_t handle = _nextWindow;
        _nextWindow += 4;
        Window &d = _windows[handle];
        d.className = "#32770"; // the dialog class
        d.owner = owner;
        d.title = t.title;
        d.dialogProc = c.arg(3);
        d.thread = currentThread() ? currentThread()->id : kMainThreadId;
        d.style = t.style & ~kWsVisible;
        d.exStyle = t.exStyle;
        d.x = ownerX + t.x * kBaseX / 4;
        d.y = ownerY + t.y * kBaseY / 8;
        d.width = t.width * kBaseX / 4;
        d.height = t.height * kBaseY / 8;
        uint32_t focus = 0;
        for (const DialogTemplate::Item &item : t.items) {
            const uint32_t child = _nextWindow;
            _nextWindow += 4;
            Window &w = _windows[child];
            w.className = item.className;
            w.title = item.title;
            w.style = item.style;
            w.exStyle = item.exStyle;
            w.thread = d.thread;
            w.x = item.x * kBaseX / 4;
            w.y = item.y * kBaseY / 8;
            w.width = item.width * kBaseX / 4;
            w.height = item.height * kBaseY / 8;
            w.visible = item.style & kWsVisible;
            w.parent = handle;
            w.id = item.id;
            if (!focus && (item.style & kWsTabStop))
                focus = child;
        }
        const bool ownerWasEnabled = _windows.count(owner) && _windows[owner].enabled;
        if (_windows.count(owner))
            _windows[owner].enabled = false;

        sendMessage(handle, kWmInitDialog, focus, c.arg(4));
        if (_windows.count(handle) && !_windows[handle].dialogResult) {
            _windows[handle].visible = true;
            _windows[handle].invalid = true;
            sendMessage(handle, kWmShowWindow, 1, 0);
        }
        while (_windows.count(handle) && !_windows[handle].dialogResult) {
            if (ending()) {
                c.abort("the process is ending");
                return uint32_t(-1);
            }
            runThreads();
            Message m;
            if (!takeMessage(0, 0, 0, true, m)) {
                if (!awaitMessage(c))
                    return uint32_t(-1);
                continue;
            }
            if (m.message == kWmQuit) { // ends the dialog, and is left for the program's own loop
                _messages.push_back(m);
                break;
            }
            dispatch(m);
        }

        uint32_t result = 0;
        if (_windows.count(handle)) {
            result = _windows[handle].dialogResult.value_or(0);
            sendMessage(handle, kWmDestroy, 0, 0);
            sendMessage(handle, kWmNcDestroy, 0, 0);
        }
        for (auto it = _windows.begin(); it != _windows.end();)
            it = it->first == handle || it->second.parent == handle ? _windows.erase(it) : std::next(it);
        for (auto it = _windowTimers.begin(); it != _windowTimers.end();)
            it = it->first.first == handle ? _windowTimers.erase(it) : std::next(it);
        if (_windows.count(owner))
            _windows[owner].enabled = ownerWasEnabled;
        return result;
    });
    stdcall(u, "EndDialog", 2, [this](Cpu &c) {
        auto it = _windows.find(c.arg(0));
        if (it == _windows.end() || !it->second.dialogProc) {
            setLastError(kErrorInvalidWindow);
            return 0u;
        }
        it->second.dialogResult = c.arg(1);
        return 1u;
    });
    stdcall(u, "GetDlgItem", 2, [this](Cpu &c) {
        for (const auto &[handle, w] : _windows)
            if (w.parent == c.arg(0) && w.id == c.arg(1))
                return handle;
        setLastError(1421); // ERROR_CONTROL_ID_NOT_FOUND
        return 0u;
    });
    // The controls: their text, numbers as text, and buttons' checks. They
    // have no procedures, so a message sent to one gets 0.
    auto control = [this](uint32_t dialog, uint32_t id) -> Window * {
        for (auto &[handle, w] : _windows)
            if (w.parent == dialog && w.id == id)
                return &w;
        return nullptr;
    };
    stdcall(u, "SetDlgItemTextA", 3, [this, control](Cpu &c) {
        Window *w = control(c.arg(0), c.arg(1));
        if (!w)
            return 0u;
        w->title = c.arg(2) ? c.string(c.arg(2)) : std::string();
        return 1u;
    });
    // GetDlgItemTextA(dialog, id, buffer, size): the characters copied.
    stdcall(u, "GetDlgItemTextA", 4, [control](Cpu &c) {
        const Window *w = control(c.arg(0), c.arg(1));
        const uint32_t size = c.arg(3);
        if (!size || !c.valid(c.arg(2), size))
            return 0u;
        const std::string text = w ? w->title.substr(0, size - 1) : std::string();
        c.writeString(c.arg(2), text, size);
        return uint32_t(text.size());
    });
    stdcall(u, "SetDlgItemInt", 4, [control](Cpu &c) {
        Window *w = control(c.arg(0), c.arg(1));
        if (!w)
            return 0u;
        w->title = c.arg(3) ? std::to_string(int32_t(c.arg(2))) : std::to_string(c.arg(2));
        return 1u;
    });
    // GetDlgItemInt(dialog, id, ok*, signed): the number its text starts
    // with (after spaces); ok says whether there was one.
    stdcall(u, "GetDlgItemInt", 4, [control](Cpu &c) {
        const Window *w = control(c.arg(0), c.arg(1));
        const std::string text = w ? w->title : std::string();
        const char *start = text.c_str();
        char *end = nullptr;
        const long long n = std::strtoll(start, &end, 10);
        const bool ok = end != start && (c.arg(3) || n >= 0);
        if (c.arg(2))
            c.write32(c.arg(2), ok ? 1 : 0);
        return ok ? uint32_t(n) : 0u;
    });
    stdcall(u, "CheckDlgButton", 3, [control](Cpu &c) {
        Window *w = control(c.arg(0), c.arg(1));
        if (!w)
            return 0u;
        w->checked = c.arg(2);
        return 1u;
    });
    stdcall(u, "IsDlgButtonChecked", 2, [control](Cpu &c) {
        const Window *w = control(c.arg(0), c.arg(1));
        return w ? w->checked : 0u;
    });
    // CheckRadioButton(dialog, first, last, id): that one checked, the
    // others in the range not.
    stdcall(u, "CheckRadioButton", 4, [this](Cpu &c) {
        for (auto &[handle, w] : _windows)
            if (w.parent == c.arg(0) && w.id >= c.arg(1) && w.id <= c.arg(2))
                w.checked = w.id == c.arg(3) ? 1 : 0;
        return 1u;
    });
    stdcall(u, "SendDlgItemMessageA", 5, [this](Cpu &c) {
        for (const auto &[handle, w] : _windows)
            if (w.parent == c.arg(0) && w.id == c.arg(1))
                return sendMessage(handle, c.arg(2), c.arg(3), c.arg(4));
        return 0u;
    });

    // SetTimer(window, id, ms, procedure): WM_TIMER for the window, or the
    // procedure called, every `ms` (at least 10). With no window, the
    // identifier is made here.
    stdcall(u, "SetTimer", 4, [this](Cpu &c) {
        const uint32_t handle = c.arg(0);
        if (handle && !_windows.count(handle)) {
            setLastError(kErrorInvalidWindow);
            return 0u;
        }
        uint32_t id = c.arg(1);
        if (!handle)
            id = _nextWindowTimer++;
        WindowTimer &t = _windowTimers[{handle, id}];
        t.interval = std::max(c.arg(2), kUserTimerMinimum);
        t.due = now() + t.interval;
        t.proc = c.arg(3);
        return id ? id : 1u;
    });
    stdcall(u, "KillTimer", 2, [this](Cpu &c) { return _windowTimers.erase({c.arg(0), c.arg(1)}) ? 1u : 0u; });
}

} // namespace reluva::x86
