// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/sp_menus.h"

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/formats/lexer.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <utility>

namespace reluva::idtech3::jk2 {

namespace {

constexpr float kScreenW = 640;
constexpr float kScreenH = 480;
// The cursor's size (0x499998).
constexpr float kCursorSize = 48;
// The font handle's high bits: a drop shadow, or blinking (0x4999f3).
constexpr int32_t kShadowFlag = int32_t(0x80000000u);
constexpr int32_t kBlinkFlag = 0x40000000;
// A slider's bar and thumb, after the item's text; as Team Arena's.
constexpr float kSliderWidth = 96;
constexpr float kSliderHeight = 16;
constexpr float kSliderThumbWidth = 12;
constexpr float kSliderThumbHeight = 20;
// The gap between an item's text and its value; as Team Arena's.
constexpr float kValueGap = 8;

std::string lower(std::string_view s) {
    return fs::toLower(std::string(s));
}

bool same(std::string_view a, std::string_view b) {
    return lower(a) == lower(b);
}

bool isOpenBrace(const std::optional<std::string_view> &t) {
    return t && *t == "{";
}

float number(const std::vector<std::string> &args, size_t i, float fallback = 0) {
    return i < args.size() ? std::strtof(args[i].c_str(), nullptr) : fallback;
}

// A word's value: a symbol by its name (jk2sp.exe's tables from
// 0x4eabc4, each name's value its place), else the number written.
int32_t symbol(const std::vector<std::string> &args, size_t i) {
    if (i >= args.size())
        return 0;
    static const std::vector<std::vector<std::string_view>> kTables = {
        {"window_style_empty", "window_style_filled", "window_style_gradient", "window_style_shader",
         "window_style_teamcolor", "window_style_cinematic"},
        {"item_type_text", "item_type_button", "item_type_radiobutton", "item_type_checkbox", "item_type_editfield",
         "item_type_combo", "item_type_listbox", "item_type_model", "item_type_ownerdraw", "item_type_numericfield",
         "item_type_slider", "item_type_yesno", "item_type_multi", "item_type_bind"},
        {"item_align_left", "item_align_center", "item_align_right"},
    };
    const std::string word = lower(args[i]);
    for (const auto &table : kTables)
        for (size_t v = 0; v < table.size(); ++v)
            if (table[v] == word)
                return int32_t(v);
    return std::atoi(word.c_str());
}

std::array<float, 4> color(const std::vector<std::string> &args) {
    return {number(args, 0), number(args, 1), number(args, 2), number(args, 3)};
}

// A list's words, its ";"s left out.
std::vector<std::string> words(const std::vector<std::string> &block) {
    std::vector<std::string> out;
    for (const std::string &w : block)
        if (w != ";")
            out.push_back(w);
    return out;
}

bool inside(const std::array<float, 4> &r, float x, float y) {
    return x >= r[0] && x <= r[0] + r[2] && y >= r[1] && y <= r[1] + r[3];
}

} // namespace

SpMenus::SpMenus(const SearchSet &files, CvarStore &cvars) : _files(files), _cvars(cvars) {
}

SpMenus::~SpMenus() = default;

std::string SpMenus::text(std::string_view text) const {
    // "@PACKAGE_KEY": an entry in the string packages, in the language set.
    if (text.empty() || text[0] != '@')
        return std::string(text);
    const int language = std::clamp(_cvars.integer("sp_language"), 0, 2);
    const std::string *found = _strings.find(text.substr(1), raven::StringTable::Language(language));
    return found ? *found : std::string(text);
}

Error SpMenus::load() {
    _strings.load(_files);
    _fonts = std::make_unique<FontSet>(_files, _shaders);
    _white = _shaders.add("white");
    _sliderBar = _shaders.add("menu/new/slider");
    _sliderThumb = _shaders.add("menu/new/sliderthumb");
    auto readText = [&](const std::string &name, std::string &out) {
        std::vector<uint8_t> data;
        Error err = _files.read(name, data);
        out.assign(data.begin(), data.end());
        return err;
    };
    std::string list;
    if (Error err = readText("ui/menus.txt", list))
        return Error::fail("ui/menus.txt: " + err.message);
    Lexer files(list);
    if (!isOpenBrace(files.next()))
        return Error::fail("ui/menus.txt: no { at its start");
    while (const auto t = files.next()) {
        if (*t == "}")
            break;
        if (!same(*t, "loadMenu"))
            continue;
        // loadMenu { "file" }
        if (!isOpenBrace(files.next()))
            return Error::fail("ui/menus.txt: loadMenu without {");
        std::string name;
        while (const auto f = files.next()) {
            if (*f == "}")
                break;
            name = std::string(*f);
        }
        std::string menuText;
        if (Error err = readText(name, menuText)) {
            if (print)
                print("menus: " + name + ": " + err.message + "\n");
            continue;
        }
        // { assetGlobalDef { ... } menuDef { ... } ... }
        Lexer lexer(menuText);
        if (!isOpenBrace(lexer.next()))
            return Error::fail(name + ": no { at its start");
        while (const auto k = lexer.next()) {
            if (*k == "}")
                break;
            const std::string keyword(*k);
            MenuDefs defs;
            if (!defs.parse(lexer))
                return Error::fail(name + ": " + keyword + " near line " + std::to_string(lexer.line()) +
                                   " can't be read");
            if (same(keyword, "assetGlobalDef"))
                readAssets(defs.menus()[0]);
            else if (same(keyword, "menuDef"))
                readMenu(defs.menus()[0]);
        }
    }
    if (_menus.empty())
        return Error::fail("ui/menus.txt names no menus");
    return {};
}

void SpMenus::readAssets(const MenuEntries &entries) {
    for (const MenuEntry &e : entries.entries) {
        const std::string key = lower(e.key);
        if (e.args.empty())
            continue;
        // The fonts' handles are the numbers items name them by (an item's
        // font goes to the renderer as it is, 0x4a017d), registered in this
        // order.
        if (key == "smallfont")
            _fontHandles[0] = _fonts->registerFont(e.args[0]);
        else if (key == "mediumfont")
            _fontHandles[1] = _fonts->registerFont(e.args[0]);
        else if (key == "bigfont")
            _fontHandles[2] = _fonts->registerFont(e.args[0]);
        else if (key == "cursor")
            _cursorShader = _shaders.add(e.args[0]);
        else if (key == "itemfocussound")
            _focusSound = e.args[0];
    }
}

void SpMenus::readWindow(const MenuEntries &entries, Window &w) {
    for (const MenuEntry &e : entries.entries) {
        const std::string key = lower(e.key);
        if (key == "name" && !e.args.empty()) {
            w.name = e.args[0];
        } else if (key == "group" && !e.args.empty()) {
            w.group = e.args[0];
        } else if (key == "rect") {
            w.rect = {number(e.args, 0), number(e.args, 1), number(e.args, 2), number(e.args, 3)};
        } else if (key == "style") {
            w.style = symbol(e.args, 0);
        } else if (key == "border") {
            w.border = symbol(e.args, 0);
        } else if (key == "bordersize") {
            w.borderSize = number(e.args, 0);
        } else if (key == "forecolor") {
            w.foreColor = color(e.args);
            w.foreColorSet = true;
        } else if (key == "backcolor") {
            w.backColor = color(e.args);
        } else if (key == "bordercolor") {
            w.borderColor = color(e.args);
        } else if (key == "background" && !e.args.empty()) {
            w.background = _shaders.add(e.args[0]);
        } else if (key == "visible") {
            w.visible = number(e.args, 0) != 0;
        } else if (key == "decoration") {
            w.decoration = true;
        }
    }
}

SpMenus::Item SpMenus::readItem(const MenuItemDef &def) {
    Item item;
    readWindow(def, item);
    for (const MenuEntry &e : def.entries) {
        const std::string key = lower(e.key);
        if (key == "type") {
            item.type = symbol(e.args, 0);
        } else if (key == "text" && !e.args.empty()) {
            item.text = text(e.args[0]);
        } else if (key == "desctext" && !e.args.empty()) {
            item.descText = text(e.args[0]);
        } else if (key == "textalign") {
            item.textAlign = symbol(e.args, 0);
        } else if (key == "textalignx") {
            item.textAlignX = number(e.args, 0);
        } else if (key == "textaligny") {
            item.textAlignY = number(e.args, 0);
        } else if (key == "textscale") {
            item.textScale = number(e.args, 0);
        } else if (key == "textstyle") {
            item.textStyle = symbol(e.args, 0);
        } else if (key == "font") {
            item.font = symbol(e.args, 0);
        } else if (key == "cvar" && !e.args.empty()) {
            item.cvar = e.args[0];
        } else if (key == "cvarfloat" && !e.args.empty()) {
            item.cvar = e.args[0];
            item.sliderDefault = number(e.args, 1);
            item.sliderMin = number(e.args, 2);
            item.sliderMax = number(e.args, 3, 1);
        } else if (key == "cvarstrlist" || key == "cvarfloatlist") {
            // Pairs of text and value.
            const std::vector<std::string> w = words(e.block);
            for (size_t i = 0; i + 1 < w.size(); i += 2)
                item.choices.emplace_back(text(w[i]), w[i + 1]);
            item.numberChoices = key == "cvarfloatlist";
        } else if (key == "action") {
            item.action = e.block;
        } else if (key == "mouseenter") {
            item.mouseEnter = e.block;
        } else if (key == "mouseexit") {
            item.mouseExit = e.block;
        } else if (key == "onfocus") {
            item.onFocus = e.block;
        } else if (key == "leavefocus") {
            item.leaveFocus = e.block;
        } else if (key == "cvartest" && !e.args.empty()) {
            item.cvarTest = e.args[0];
        } else if (key == "showcvar") {
            item.showCvar = words(e.block);
        } else if (key == "hidecvar") {
            item.hideCvar = words(e.block);
        } else if (key == "enablecvar") {
            item.enableCvar = words(e.block);
        } else if (key == "disablecvar") {
            item.disableCvar = words(e.block);
        }
    }
    return item;
}

void SpMenus::readMenu(const MenuDef &def) {
    Menu m;
    readWindow(def, m);
    for (const MenuEntry &e : def.entries) {
        const std::string key = lower(e.key);
        if (key == "fullscreen")
            m.fullscreen = number(e.args, 0) != 0;
        else if (key == "focuscolor")
            m.focusColor = color(e.args);
        else if (key == "disablecolor")
            m.disableColor = color(e.args);
        else if (key == "descx")
            m.descX = number(e.args, 0);
        else if (key == "descy")
            m.descY = number(e.args, 0);
        else if (key == "descscale")
            m.descScale = number(e.args, 0);
        else if (key == "desccolor")
            m.descColor = color(e.args);
        else if (key == "descalignment")
            m.descAlign = symbol(e.args, 0);
        else if (key == "onopen")
            m.onOpen = e.block;
        else if (key == "onclose")
            m.onClose = e.block;
        else if (key == "onesc")
            m.onEsc = e.block;
    }
    // Every menu starts closed, whatever its "visible" says: they're opened
    // by name.
    m.visible = false;
    for (const MenuItemDef &d : def.items)
        m.items.push_back(readItem(d));
    _menus.push_back(std::move(m));
}

std::vector<std::string> SpMenus::menuNames() const {
    std::vector<std::string> out;
    for (const Menu &m : _menus)
        out.push_back(m.name);
    return out;
}

std::vector<std::string> SpMenus::openMenus() const {
    std::vector<std::string> out;
    for (const Menu &m : _menus)
        if (m.visible)
            out.push_back(m.name);
    return out;
}

std::string SpMenus::focusedItem() const {
    for (const Menu &m : _menus)
        if (m.focus)
            for (const Item &i : m.items)
                if (i.focus)
                    return i.name;
    return {};
}

SpMenus::Menu *SpMenus::findMenu(std::string_view name) {
    for (Menu &m : _menus)
        if (same(m.name, name))
            return &m;
    return nullptr;
}

SpMenus::Menu *SpMenus::focusedMenu() {
    for (Menu &m : _menus)
        if (m.focus && m.visible)
            return &m;
    return nullptr;
}

void SpMenus::open(std::string_view name) {
    // The menu with the focus is kept on a stack for when this one closes
    // (0x49d710 gives it back the focus).
    Menu *target = findMenu(name);
    if (!target)
        return;
    for (size_t i = 0; i < _menus.size(); ++i)
        if (_menus[i].focus && &_menus[i] != target) {
            _menus[i].focus = false;
            _openStack.push_back(i);
        }
    target->visible = true;
    target->focus = true;
    runScript(*target, target->onOpen);
}

void SpMenus::close(Menu &m) {
    runScript(m, m.onClose);
    const bool hadFocus = m.focus;
    m.visible = false;
    m.focus = false;
    if (hadFocus && !_openStack.empty()) {
        _menus[_openStack.back()].focus = true;
        _openStack.pop_back();
    }
}

void SpMenus::closeAll() {
    for (Menu &m : _menus)
        if (m.visible) {
            runScript(m, m.onClose);
            m.visible = false;
            m.focus = false;
        }
    _openStack.clear();
}

void SpMenus::runScript(Menu &menu, const Tokens &script) {
    // Each command takes the words it needs, as jk2sp.exe's table of them
    // (0x4eadd8) does; ";" between them is skipped. A word that isn't one
    // goes to the engine's own scripts (uiScript...), which take the next
    // word as theirs; one they don't know ends the script (0x49f6e9).
    auto items = [&](const std::string &name) {
        std::vector<Item *> out;
        for (Item &i : menu.items)
            if (same(i.name, name) || same(i.group, name))
                out.push_back(&i);
        return out;
    };
    size_t at = 0;
    auto arg = [&]() -> std::string { return at < script.size() ? script[at++] : std::string(); };
    auto readColor = [&]() {
        Color c{};
        for (float &v : c)
            v = std::strtof(arg().c_str(), nullptr);
        return c;
    };
    while (at < script.size()) {
        const std::string cmd = lower(script[at++]);
        if (cmd == ";")
            continue;
        if (cmd == "show" || cmd == "hide") {
            for (Item *i : items(arg())) {
                i->visible = cmd == "show";
                if (!i->visible)
                    i->focus = i->mouseOver = false;
            }
        } else if (cmd == "fadein" || cmd == "fadeout") {
            // Shown or hidden at once: Reluva doesn't fade them.
            for (Item *i : items(arg()))
                i->visible = cmd == "fadein";
        } else if (cmd == "open") {
            open(arg());
        } else if (cmd == "close") {
            const std::string name = arg();
            if (same(name, "all")) {
                closeAll();
            } else if (Menu *m = findMenu(name); m && m->visible) {
                close(*m);
            }
        } else if (cmd == "setfocus") {
            for (Item *i : items(arg()))
                if (!i->decoration && !i->focus) {
                    setFocus(menu, *i);
                    break;
                }
        } else if (cmd == "setcvar") {
            const std::string name = arg();
            const std::string value = arg();
            _cvars.set(name, value);
        } else if (cmd == "exec") {
            _commands.push_back(arg());
        } else if (cmd == "play") {
            // On channel 0 (0x49e1e0).
            const std::string sound = arg();
            if (startLocalSound)
                startLocalSound(sound, 0);
        } else if (cmd == "playlooped" || cmd == "setbackground" || cmd == "setasset" || cmd == "showmenu") {
            arg();
        } else if (cmd == "setitemcolor") {
            const std::string name = arg();
            const std::string which = lower(arg());
            const Color c = readColor();
            for (Item *i : items(name)) {
                if (which == "forecolor") {
                    i->foreColor = c;
                    i->foreColorSet = true;
                } else if (which == "backcolor") {
                    i->backColor = c;
                } else if (which == "bordercolor") {
                    i->borderColor = c;
                }
            }
        } else if (cmd == "setcolor") {
            const std::string which = lower(arg());
            const Color c = readColor();
            if (which == "forecolor")
                menu.foreColor = c;
            else if (which == "backcolor")
                menu.backColor = c;
            else if (which == "bordercolor")
                menu.borderColor = c;
        } else if (cmd == "setitemflag" || cmd == "activate") {
            arg();
            arg();
        } else if (cmd == "defer") {
            // "defer VideoSetup <menu>": a warning menu for when the video
            // settings changed, which Reluva doesn't ask for.
            arg();
            arg();
        } else if (cmd == "orbit") {
            at = std::min(at + 6, script.size());
        } else if (cmd == "transition") {
            at = std::min(at + 11, script.size());
        } else if (cmd == "opengotomenu" || cmd == "rundeferred") {
            // Nothing to open or run: no "goto" menu nor deferred script is kept.
        } else if (!runUiScript(menu, script, at)) {
            return;
        }
    }
}

bool SpMenus::runUiScript(Menu &menu, const Tokens &script, size_t &at) {
    if (at >= script.size())
        return false;
    const std::string name = lower(script[at++]);
    if (name == "startgame") {
        startGame();
    } else if (name == "closeingame" || name == "closedatapad") {
        closeAll();
        _catcher &= ~kCatchUi;
    } else if (name == "loadauto") {
        closeAll();
        _catcher &= ~kCatchUi;
        _commands.emplace_back("load auto");
    } else if (name == "quit") {
        _commands.emplace_back("quit");
    } else if (name == "leave") {
        closeAll();
        _commands.emplace_back("disconnect");
    } else if (name == "update" || name == "setvid1data") {
        if (at < script.size())
            ++at;
    } else if (name == "glcustom" || name == "getvideosetup" || name == "getvid1data" || name == "updatevideosetup" ||
               name == "loadcontrols" || name == "savecontrols" || name == "resetdefaults" ||
               name == "readsavedirectory" || name == "loadgame" || name == "savegame" || name == "deletegame" ||
               name == "clearerror" || name == "updateforcestatus" || name == "nextdatapadweapon" ||
               name == "prevdatapadweapon" || name == "nextdatapadforcepower" || name == "prevdatapadforcepower" ||
               name == "nextdatapadinventory" || name == "prevdatapadinventory" || name == "setforce" ||
               name == "savetemplate") {
        // Known, but not done yet (sp-menus.md, "Not yet").
        if (print)
            print("menus: uiScript " + name + " isn't done yet\n");
    } else {
        if (print)
            print("menus: unknown script command " + name + " in " + menu.name + "\n");
        return false;
    }
    return true;
}

void SpMenus::startGame() {
    // The menus closed and the first level started (0x49a3eb).
    closeAll();
    _catcher &= ~kCatchUi;
    _commands.emplace_back("map kejim_post");
}

bool SpMenus::shown(const Item &item) const {
    if (!item.visible)
        return false;
    if (item.cvarTest.empty())
        return true;
    const std::string value = _cvars.string(item.cvarTest);
    auto listed = [&](const Tokens &list) {
        return std::any_of(list.begin(), list.end(), [&](const std::string &v) { return same(v, value); });
    };
    if (!item.showCvar.empty() && !listed(item.showCvar))
        return false;
    return item.hideCvar.empty() || !listed(item.hideCvar);
}

bool SpMenus::enabled(const Item &item) const {
    if (item.cvarTest.empty())
        return true;
    const std::string value = _cvars.string(item.cvarTest);
    auto listed = [&](const Tokens &list) {
        return std::any_of(list.begin(), list.end(), [&](const std::string &v) { return same(v, value); });
    };
    if (!item.enableCvar.empty() && !listed(item.enableCvar))
        return false;
    return item.disableCvar.empty() || !listed(item.disableCvar);
}

bool SpMenus::canFocus(const Item &item) const {
    return shown(item) && !item.decoration && enabled(item) && item.type != kText;
}

void SpMenus::setFocus(Menu &menu, Item &item) {
    for (Item &other : menu.items)
        if (other.focus && &other != &item) {
            other.focus = false;
            runScript(menu, other.leaveFocus);
        }
    item.focus = true;
    runScript(menu, item.onFocus);
    if (!_focusSound.empty() && startLocalSound)
        startLocalSound(_focusSound, 0);
}

void SpMenus::mouseMove(Menu &menu) {
    // Items the cursor comes onto run their mouseEnter and take the focus;
    // those it leaves run their mouseExit, and keep the focus.
    for (Item &item : menu.items) {
        if (!shown(item) || item.decoration)
            continue;
        const bool over = inside(item.rect, _cursorX, _cursorY);
        if (over && !item.mouseOver) {
            item.mouseOver = true;
            runScript(menu, item.mouseEnter);
            if (canFocus(item) && !item.focus)
                setFocus(menu, item);
        } else if (!over && item.mouseOver) {
            item.mouseOver = false;
            runScript(menu, item.mouseExit);
        }
    }
}

void SpMenus::moveFocus(Menu &menu, int step) {
    // The next item (or the one before) that can take the focus, round the
    // menu's ends.
    const int n = int(menu.items.size());
    int from = step > 0 ? -1 : n;
    for (int i = 0; i < n; ++i)
        if (menu.items[size_t(i)].focus)
            from = i;
    for (int k = 1; k <= n; ++k) {
        const int i = ((from + step * k) % n + n) % n;
        if (canFocus(menu.items[size_t(i)])) {
            setFocus(menu, menu.items[size_t(i)]);
            return;
        }
    }
}

void SpMenus::itemKey(Menu &menu, Item &item, int32_t key) {
    const bool click = key == q3key::kMouse1;
    const bool enter = key == q3key::kEnter || key == q3key::kKpEnter;
    if (click && !inside(item.rect, _cursorX, _cursorY))
        return;
    switch (item.type) {
    case kYesNo:
        if (click || enter || key == q3key::kLeft || key == q3key::kRight) {
            _cvars.set(item.cvar, _cvars.value(item.cvar) != 0 ? "0" : "1");
            runScript(menu, item.action);
        }
        break;
    case kMulti:
        if (!item.choices.empty() &&
            (click || enter || key == q3key::kMouse2 || key == q3key::kLeft || key == q3key::kRight)) {
            // The choice after the setting's (before it, going back).
            const int n = int(item.choices.size());
            int current = -1;
            for (int i = 0; i < n; ++i) {
                const std::string &v = item.choices[size_t(i)].second;
                if (item.numberChoices ? std::strtof(v.c_str(), nullptr) == _cvars.value(item.cvar)
                                       : same(v, _cvars.string(item.cvar)))
                    current = i;
            }
            const int step = key == q3key::kMouse2 || key == q3key::kLeft ? -1 : 1;
            const int next = ((current + step) % n + n) % n;
            _cvars.set(item.cvar, item.choices[size_t(next)].second);
            runScript(menu, item.action);
        }
        break;
    case kSlider: {
        const float range = item.sliderMax - item.sliderMin;
        float value = _cvars.find(item.cvar) ? _cvars.value(item.cvar) : item.sliderDefault;
        if (click) {
            const float x = item.rect[0] + item.textAlignX + kValueGap;
            if (_cursorX < x || _cursorX > x + kSliderWidth)
                return;
            value = item.sliderMin + (_cursorX - x) / kSliderWidth * range;
        } else if (key == q3key::kLeft || key == q3key::kRight) {
            value += (key == q3key::kLeft ? -range : range) / 20;
        } else {
            return;
        }
        value = std::clamp(value, item.sliderMin, item.sliderMax);
        _cvars.set(item.cvar, std::to_string(value));
        runScript(menu, item.action);
        break;
    }
    default:
        if (click || enter)
            runScript(menu, item.action);
        break;
    }
}

Error SpMenus::keyEvent(int32_t key) {
    Menu *menu = focusedMenu();
    if (!menu || (key & q3key::kCharFlag))
        return {};
    if (key == q3key::kEscape) {
        runScript(*menu, menu->onEsc);
        return {};
    }
    if (key == q3key::kUp || key == q3key::kDown || key == q3key::kTab) {
        moveFocus(*menu, key == q3key::kUp ? -1 : 1);
        return {};
    }
    for (Item &item : menu->items)
        if (item.focus && canFocus(item)) {
            itemKey(*menu, item, key);
            break;
        }
    return {};
}

Error SpMenus::mouseEvent(int32_t dx, int32_t dy) {
    _cursorX = std::clamp(_cursorX + float(dx), 0.0f, kScreenW);
    _cursorY = std::clamp(_cursorY + float(dy), 0.0f, kScreenH);
    if (Menu *menu = focusedMenu())
        mouseMove(*menu);
    return {};
}

bool SpMenus::fullscreen() {
    return std::any_of(_menus.begin(), _menus.end(), [](const Menu &m) { return m.visible && m.fullscreen; });
}

Error SpMenus::setActiveMenu(const std::string &name) {
    const std::string which = lower(name);
    closeAll();
    if (which.empty()) {
        // (0x4992e0)
        _catcher &= ~kCatchUi;
        return {};
    }
    _catcher |= kCatchUi;
    if (which == "main" || which == "mainmenu") {
        open("mainMenu"); // (0x49c0b0)
    } else if (which == "ingame") {
        open("ingameMainMenu"); // (0x49c060)
    } else if (which == "datapad") {
        // The Force powers page if a power was just gained, else the
        // mission's (0x49bff0).
        open(_cvars.integer("cg_updatedDataPadForcePower1") ? "datapadForcePowersMenu" : "datapadMissionMenu");
    } else {
        // "uimenu <menu>" (0x40bbd0).
        open(name);
    }
    return {};
}

Error SpMenus::consoleCommand(const std::string &text, bool *handled) {
    Lexer lexer(text);
    const auto first = lexer.next(false);
    const std::string cmd = first ? lower(*first) : std::string();
    bool known = true;
    if (cmd == "datapad") {
        // Only in a game (0x40bc22).
        if (connectionState == 7)
            setActiveMenu("datapad");
    } else if (cmd == "uimenu") {
        const auto menu = lexer.next(false);
        if (connectionState == 7)
            setActiveMenu(menu ? std::string(*menu) : std::string("ingame"));
    } else {
        known = false;
    }
    if (handled)
        *handled = known;
    return {};
}

Error SpMenus::drawLoadingScreen(const std::string & /*server*/, const std::string & /*info*/) {
    // The client game draws Jedi Outcast's.
    _pictures.clear();
    _raw.clear();
    _scene = Scene{};
    return {};
}

Error SpMenus::drawConnectText(const std::string & /*server*/, const std::string & /*info*/) {
    _pictures.clear();
    return {};
}

void SpMenus::setCapture(const std::vector<uint8_t> &rgba) {
    _capture = rgba.size() == kShotSize ? rgba : std::vector<uint8_t>();
}

std::vector<uint8_t> SpMenus::capture() const {
    return _capture.empty() ? std::vector<uint8_t>(kShotSize, 0) : _capture;
}

Error SpMenus::refresh(int32_t time) {
    _time = time;
    _pictures.clear();
    _raw.clear();
    _scene = Scene{};
    // Every menu open, in the order read; then the cursor over them.
    bool any = false;
    for (const Menu &m : _menus)
        if (m.visible) {
            drawMenu(m);
            any = true;
        }
    if (any && _cursorShader)
        drawRect({_cursorX, _cursorY, kCursorSize, kCursorSize}, {1, 1, 1, 1}, _cursorShader);
    return {};
}

void SpMenus::drawRect(const Rect &r, const Color &c, int32_t shader) {
    _pictures.push_back({r[0], r[1], r[2], r[3], 0, 0, 1, 1, shader, c});
}

void SpMenus::drawWindow(const Window &w) {
    const Rect &r = w.rect;
    if (w.style == kStyleFilled)
        drawRect(r, w.backColor, _white);
    else if (w.style == kStyleShader && w.background)
        drawRect(r, w.foreColorSet ? w.foreColor : Color{1, 1, 1, 1}, w.background);
    // Borders: 1 all round, 2 top and bottom, 3 the sides.
    const float b = w.borderSize;
    if (w.border == 1 || w.border == 2) {
        drawRect({r[0], r[1], r[2], b}, w.borderColor, _white);
        drawRect({r[0], r[1] + r[3] - b, r[2], b}, w.borderColor, _white);
    }
    if (w.border == 1 || w.border == 3) {
        drawRect({r[0], r[1], b, r[3]}, w.borderColor, _white);
        drawRect({r[0] + r[2] - b, r[1], b, r[3]}, w.borderColor, _white);
    }
}

int32_t SpMenus::fontHandle(int32_t font) const {
    // Font 0 is the medium one (0x4999d8).
    return font > 0 ? font : _fontHandles[1];
}

float SpMenus::textWidth(const std::string &s, float scale, int32_t font) const {
    const raven::Font *f = _fonts->font(fontHandle(font));
    return f ? float(f->textWidth(s, scale)) : 0;
}

void SpMenus::drawText(float x, float y, float scale, const Color &c, const std::string &s, int32_t style,
                       int32_t font) {
    // Styles 3 to 6 shadowed, 2 blinking (0x4999e2).
    int32_t handle = fontHandle(font);
    if (style == 2)
        handle |= kBlinkFlag;
    else if (style >= 3 && style <= 6)
        handle |= kShadowFlag;
    _fonts->drawString(int32_t(x), int32_t(y), s, c, handle, -1, scale, _time,
                       [this](float px, float py, float w, float h, float s1, float t1, float s2, float t2,
                              int32_t shader,
                              const Color &col) { _pictures.push_back({px, py, w, h, s1, t1, s2, t2, shader, col}); });
}

std::string SpMenus::itemValue(const Item &item) const {
    switch (item.type) {
    case kYesNo:
        return text(_cvars.value(item.cvar) != 0 ? "@MENUS0_YES" : "@MENUS0_NO");
    case kMulti:
        for (const auto &[label, value] : item.choices)
            if (item.numberChoices ? std::strtof(value.c_str(), nullptr) == _cvars.value(item.cvar)
                                   : same(value, _cvars.string(item.cvar)))
                return label;
        return item.choices.empty() ? std::string() : item.choices[0].first;
    case kEditField:
        return _cvars.string(item.cvar);
    case kBind: {
        // The keys bound to the command, as "A or B"; "???" for none.
        std::vector<std::string> keys;
        if (bindings)
            for (int32_t k = 0; k < 256 && keys.size() < 2; ++k)
                if (!item.cvar.empty() && same(bindings->get(k), item.cvar))
                    keys.push_back(KeyBindings::keyName(k));
        if (keys.empty())
            return "???";
        return keys.size() == 1 ? keys[0] : keys[0] + " " + text("@MENUS3_KEYBIND_OR") + " " + keys[1];
    }
    default:
        return {};
    }
}

void SpMenus::drawItem(const Menu &menu, const Item &item) {
    drawWindow(item);
    if (item.text.empty() && item.type != kSlider)
        return;
    // The text placed by its alignment from the rectangle's corner, its
    // top at textaligny; the focus's colour while it has it.
    const Color c = !enabled(item) ? menu.disableColor : item.focus ? menu.focusColor : item.foreColor;
    const float w = textWidth(item.text, item.textScale, item.font);
    float x = item.rect[0] + item.textAlignX;
    if (item.textAlign == kCenter)
        x -= w / 2;
    else if (item.textAlign == kRight)
        x -= w;
    const float y = item.rect[1] + item.textAlignY;
    drawText(x, y, item.textScale, c, item.text, item.textStyle, item.font);
    const float valueX = x + w + kValueGap;
    if (item.type == kSlider) {
        const float value = _cvars.find(item.cvar) ? _cvars.value(item.cvar) : item.sliderDefault;
        const float range = item.sliderMax - item.sliderMin;
        const float frac = range > 0 ? std::clamp((value - item.sliderMin) / range, 0.0f, 1.0f) : 0;
        drawRect({valueX, y, kSliderWidth, kSliderHeight}, c, _sliderBar);
        drawRect({valueX + frac * kSliderWidth - kSliderThumbWidth / 2, y - 2, kSliderThumbWidth, kSliderThumbHeight},
                 c, _sliderThumb);
        return;
    }
    const std::string value = itemValue(item);
    if (!value.empty())
        drawText(valueX, y, item.textScale, c, value, item.textStyle, item.font);
}

void SpMenus::drawMenu(const Menu &menu) {
    drawWindow(menu);
    const Item *described = nullptr;
    for (const Item &item : menu.items) {
        if (!shown(item))
            continue;
        drawItem(menu, item);
        if (item.focus && !item.descText.empty())
            described = &item;
    }
    // The focused item's description, at the menu's place for it.
    if (described) {
        const float w = textWidth(described->descText, menu.descScale, 0);
        float x = menu.descX;
        if (menu.descAlign == kCenter)
            x -= w / 2;
        else if (menu.descAlign == kRight)
            x -= w;
        drawText(x, menu.descY, menu.descScale, menu.descColor, described->descText, 0, 0);
    }
}

} // namespace reluva::idtech3::jk2
