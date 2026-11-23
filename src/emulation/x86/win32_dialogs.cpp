// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's dialogs and window timers, as Microsoft documents them, and the
// resources dialogs are made from. A modal dialog is a window whose
// procedure is the program's dialog procedure, with a record of each
// control; DialogBoxParamA runs the messages until EndDialog. Nothing is
// drawn: a program that shows its dialogs itself (through DirectDraw,
// say) needs only the messages.

#include "emulation/x86/win32.h"

#include "common/text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

namespace reluva::x86 {

namespace {

// List boxes: their messages, styles and notifications, and what owner
// drawing passes.
enum : uint32_t {
    kLbAddString = 0x180,
    kLbInsertString = 0x181,
    kLbDeleteString = 0x182,
    kLbResetContent = 0x184,
    kLbSetCurSel = 0x186,
    kLbGetSel = 0x187,
    kLbGetCurSel = 0x188,
    kLbGetText = 0x189,
    kLbGetTextLen = 0x18a,
    kLbGetCount = 0x18b,
    kLbSelectString = 0x18c,
    kLbGetTopIndex = 0x18e,
    kLbFindString = 0x18f,
    kLbSetTopIndex = 0x197,
    kLbGetItemRect = 0x198,
    kLbGetItemData = 0x199,
    kLbSetItemData = 0x19a,
    kLbSetCaretIndex = 0x19e,
    kLbGetCaretIndex = 0x19f,
    kLbSetItemHeight = 0x1a0,
    kLbGetItemHeight = 0x1a1,
    kLbFindStringExact = 0x1a2,
    kLbErr = 0xffffffffu,
    kLbsNotify = 0x1,
    kLbsSort = 0x2,
    kLbsOwnerDrawFixed = 0x10,
    kLbsOwnerDrawVariable = 0x20,
    kLbsHasStrings = 0x40,
    kLbnSelChange = 1,
    kLbnDblClk = 2,
    kWmCommand = 0x111,
    kWmDrawItem = 0x2b,
    kWmMeasureItem = 0x2c,
    kWmLButtonDownMsg = 0x201,
    kWmLButtonDblClkMsg = 0x203,
    kOdtListBox = 2,
    kOdaDrawEntire = 1,
    kOdsSelected = 1,
    kWsDisabled = 0x08000000,
    kCbsSort = 0x100,
    kWmGetText = 0xd,
    kWmGetTextLength = 0xe,
    kListBoxDoubleClickMs = 500,
};

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
            w.enabled = !(item.style & kWsDisabled);
            // A combo box keeps its list as a list box does.
            if (text::equalsIgnoreCase(item.className, "#131") || text::equalsIgnoreCase(item.className, "listbox") ||
                text::equalsIgnoreCase(item.className, "#133") || text::equalsIgnoreCase(item.className, "combobox"))
                w.listBox = Window::ListBox();
            if (!focus && (item.style & kWsTabStop))
                focus = child;
        }
        // An owner-drawn list box of fixed height asks its dialog, once, how
        // high its items are (WM_MEASUREITEM), before WM_INITDIALOG.
        for (auto &[child, w] : _windows) {
            if (w.parent != handle || !w.listBox || !(w.style & kLbsOwnerDrawFixed))
                continue;
            const uint32_t mis = memory().alloc(24); // MEASUREITEMSTRUCT
            const uint32_t fields[6] = {kOdtListBox, w.id, 0, uint32_t(w.width), w.listBox->itemHeight, 0};
            for (uint32_t k = 0; k < 6; ++k)
                c.write32(mis + 4 * k, fields[k]);
            sendMessage(handle, kWmMeasureItem, w.id, mis);
            if (_windows.count(child) && c.read32(mis + 16))
                _windows[child].listBox->itemHeight = c.read32(mis + 16);
            memory().free(mis);
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

uint32_t Win32Process::listBoxMessage(uint32_t handle, uint32_t message, uint32_t wParam, uint32_t lParam) {
    auto it = _windows.find(handle);
    if (it == _windows.end() || !it->second.listBox)
        return 0;
    Window &w = it->second;
    Window::ListBox &box = *w.listBox;
    // A combo box's CB_ messages are its list's LB_ ones; its text is the
    // item selected.
    const bool combo = text::equalsIgnoreCase(w.className, "#133") || text::equalsIgnoreCase(w.className, "combobox");
    if (combo) {
        static const std::pair<uint32_t, uint32_t> kComboToList[] = {
            {0x143, kLbAddString},   {0x144, kLbDeleteString},  {0x146, kLbGetCount},      {0x147, kLbGetCurSel},
            {0x148, kLbGetText},     {0x149, kLbGetTextLen},    {0x14a, kLbInsertString},  {0x14b, kLbResetContent},
            {0x14c, kLbFindString},  {0x14d, kLbSelectString},  {0x14e, kLbSetCurSel},     {0x150, kLbGetItemData},
            {0x151, kLbSetItemData}, {0x153, kLbSetItemHeight}, {0x154, kLbGetItemHeight}, {0x158, kLbFindStringExact}};
        bool known = false;
        for (const auto &[cb, lb] : kComboToList)
            if (message == cb) {
                message = lb;
                known = true;
                break;
            }
        if (!known && (message == kWmGetText || message == kWmGetTextLength)) {
            const std::string text =
                box.selected >= 0 && size_t(box.selected) < box.text.size() ? box.text[size_t(box.selected)] : "";
            if (message == kWmGetTextLength)
                return uint32_t(text.size());
            if (!wParam)
                return 0;
            const std::string part = text.substr(0, wParam - 1);
            _cpu.writeString(lParam, part, uint32_t(part.size() + 1));
            return uint32_t(part.size());
        }
        if (!known)
            return 0;
    }
    const bool strings = (w.style & kLbsHasStrings) || !(w.style & (kLbsOwnerDrawFixed | kLbsOwnerDrawVariable));
    const int count = int(box.data.size());
    auto changed = [&] {
        if (auto parent = _windows.find(w.parent); parent != _windows.end())
            parent->second.invalid = true;
    };
    auto valid = [&](uint32_t index) { return int(index) >= 0 && int(index) < count; };
    auto insert = [&](int at, uint32_t value) {
        std::string text = strings && value ? _cpu.string(value) : std::string();
        if (at < 0 || at > count)
            at = count;
        box.text.insert(box.text.begin() + at, text);
        box.data.insert(box.data.begin() + at, strings ? 0 : value);
        if (box.selected >= at)
            ++box.selected;
        changed();
        return uint32_t(at);
    };
    // By prefix, or whole, ignoring case, after the item given (-1: all).
    auto find = [&](uint32_t after, uint32_t value, bool exact) -> uint32_t {
        for (int k = 0; k < count; ++k) {
            const int i = (int(after) + 1 + k) % std::max(count, 1);
            const bool hit =
                strings ? (exact ? text::equalsIgnoreCase(box.text[size_t(i)], _cpu.string(value))
                                 : text::toLower(box.text[size_t(i)]).rfind(text::toLower(_cpu.string(value)), 0) == 0)
                        : box.data[size_t(i)] == value;
            if (hit)
                return uint32_t(i);
        }
        return kLbErr;
    };
    switch (message) {
    case kLbAddString: {
        if (!(w.style & (combo ? kCbsSort : kLbsSort)) || !strings)
            return insert(count, lParam);
        // Sorted: before the first that comes after it, ignoring case.
        const std::string text = _cpu.string(lParam);
        int at = 0;
        while (at < count && text::toLower(box.text[size_t(at)]) <= text::toLower(text))
            ++at;
        return insert(at, lParam);
    }
    case kLbInsertString:
        return insert(int(wParam), lParam);
    case kLbDeleteString:
        if (!valid(wParam))
            return kLbErr;
        box.text.erase(box.text.begin() + wParam);
        box.data.erase(box.data.begin() + wParam);
        if (box.selected == int(wParam))
            box.selected = -1;
        else if (box.selected > int(wParam))
            --box.selected;
        changed();
        return uint32_t(count - 1);
    case kLbResetContent:
        box = Window::ListBox{{}, {}, -1, 0, box.itemHeight};
        changed();
        return 0;
    case kLbSetCurSel:
        box.selected = valid(wParam) ? int(wParam) : -1;
        changed();
        return valid(wParam) ? wParam : kLbErr;
    case kLbGetSel:
        return valid(wParam) ? uint32_t(box.selected == int(wParam)) : kLbErr;
    case kLbGetCurSel:
    case kLbGetCaretIndex:
        return box.selected < 0 ? kLbErr : uint32_t(box.selected);
    case kLbSetCaretIndex:
        return 0;
    case kLbGetText:
        if (!valid(wParam))
            return kLbErr;
        if (strings)
            _cpu.writeString(lParam, box.text[wParam], uint32_t(box.text[wParam].size() + 1));
        else
            _cpu.write32(lParam, box.data[wParam]);
        return strings ? uint32_t(box.text[wParam].size()) : 4;
    case kLbGetTextLen:
        return valid(wParam) ? (strings ? uint32_t(box.text[wParam].size()) : 4) : kLbErr;
    case kLbGetCount:
        return uint32_t(count);
    case kLbFindString:
    case kLbFindStringExact:
        return find(wParam, lParam, message == kLbFindStringExact);
    case kLbSelectString: {
        const uint32_t i = find(wParam, lParam, false);
        if (i != kLbErr) {
            box.selected = int(i);
            changed();
        }
        return i;
    }
    case kLbGetTopIndex:
        return uint32_t(box.top);
    case kLbSetTopIndex:
        if (!valid(wParam) && wParam != 0)
            return kLbErr;
        box.top = int(wParam);
        changed();
        return 0;
    case kLbGetItemRect: {
        if (!valid(wParam) || !_cpu.valid(lParam, 16))
            return kLbErr;
        const int y = (int(wParam) - box.top) * int(box.itemHeight);
        const uint32_t r[4] = {0, uint32_t(y), uint32_t(w.width), uint32_t(y + int(box.itemHeight))};
        for (uint32_t k = 0; k < 4; ++k)
            _cpu.write32(lParam + 4 * k, r[k]);
        return 1;
    }
    case kLbGetItemData:
        return valid(wParam) ? box.data[wParam] : kLbErr;
    case kLbSetItemData:
        if (!valid(wParam))
            return kLbErr;
        box.data[wParam] = lParam;
        return 0;
    case kLbSetItemHeight:
        if (lParam & 0xffff)
            box.itemHeight = lParam & 0xffff;
        changed();
        return 0;
    case kLbGetItemHeight:
        return box.itemHeight;
    case kWmLButtonDownMsg:
    case kWmLButtonDblClkMsg: {
        // The item under the click, selected; the dialog told, with a
        // double click (the same item again, soon) told as one.
        const int y = int(int16_t(lParam >> 16));
        const int item = box.top + y / int(std::max<uint32_t>(box.itemHeight, 1));
        if (item < 0 || item >= count)
            return 0;
        const uint32_t now = milliseconds();
        const bool twice = box.lastClicked == item && now - box.lastClickTime <= kListBoxDoubleClickMs;
        box.lastClicked = twice ? -1 : item;
        box.lastClickTime = now;
        const bool moved = box.selected != item;
        box.selected = item;
        changed();
        const uint32_t parent = w.parent;
        const uint32_t id = w.id;
        if (w.style & kLbsNotify) {
            if (moved)
                sendMessage(parent, kWmCommand, id | kLbnSelChange << 16, handle);
            if (twice)
                sendMessage(parent, kWmCommand, id | kLbnDblClk << 16, handle);
        }
        return 0;
    }
    default:
        return 0;
    }
}

void Win32Process::paintOwnerDrawn(uint32_t dialog) {
    std::vector<uint32_t> boxes;
    for (const auto &[handle, w] : _windows)
        if (w.parent == dialog && w.listBox && w.visible && (w.style & (kLbsOwnerDrawFixed | kLbsOwnerDrawVariable)))
            boxes.push_back(handle);
    const uint32_t dis = memory().alloc(48); // DRAWITEMSTRUCT
    for (const uint32_t handle : boxes) {
        auto it = _windows.find(handle);
        if (it == _windows.end())
            continue;
        const Window::ListBox box = *it->second.listBox;
        const int height = it->second.height;
        const int width = it->second.width;
        const uint32_t id = it->second.id;
        const int step = int(std::max<uint32_t>(box.itemHeight, 1));
        for (int item = box.top; item < int(box.data.size()) && (item - box.top) * step < height; ++item) {
            const int y = (item - box.top) * step;
            const uint32_t fields[12] = {kOdtListBox,
                                         id,
                                         uint32_t(item),
                                         kOdaDrawEntire,
                                         item == box.selected ? kOdsSelected : 0u,
                                         handle,
                                         windowDeviceContext(handle),
                                         0,
                                         uint32_t(y),
                                         uint32_t(width),
                                         uint32_t(y + step),
                                         box.data[size_t(item)]};
            for (uint32_t k = 0; k < 12; ++k)
                _cpu.write32(dis + 4 * k, fields[k]);
            sendMessage(dialog, kWmDrawItem, id, dis);
        }
    }
    memory().free(dis);
}

} // namespace reluva::x86
