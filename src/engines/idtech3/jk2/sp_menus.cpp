// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/sp_menus.h"

#include "engines/idtech3/client/bindings.h"
#include "engines/idtech3/formats/lexer.h"
#include "engines/idtech3/formats/md3.h"
#include "engines/idtech3/jk2/ref_entity.h"
#include "engines/idtech3/jk2/sp_savegame.h"
#include "engines/idtech3/raven/savegame.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
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
// A list box's scroll bar: its width, and its arrows' and thumb's size
// (0x4a0a0a).
constexpr float kScrollbarSize = 16;
// Two clicks on a list box this close (ms) are a double click; as Team
// Arena's.
constexpr int32_t kDoubleClickMs = 300;
// The saved games listed at most (0x49c770).
constexpr size_t kMaxSaves = 100;
// A model item's view (0x4a1584 on): 45 degrees each way; the model as
// far off as its height times this, so it fits; turning this many degrees
// a millisecond.
constexpr float kModelFov = 45;
constexpr float kModelDistance = 3.731343283582089f;
constexpr float kModelTurn = 0.05f;
// What the model is drawn with: lit from its own origin, no shadow (0x4a16cd).
constexpr int32_t kModelFlags = 0xc0;
constexpr float kPi = 3.14159265358979f;
// The list box feeder for the saved games (FEEDER_SAVEGAMES).
constexpr float kFeederSaves = 0;

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
    _scrollBar = _shaders.add("gfx/menus/scrollbar.tga");
    _scrollUp = _shaders.add("gfx/menus/scrollbar_arrow_up_a.tga");
    _scrollDown = _shaders.add("gfx/menus/scrollbar_arrow_dwn_a.tga");
    _scrollThumb = _shaders.add("gfx/menus/scrollbar_thumb.tga");
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
        else if (key == "fadeclamp")
            _fadeClamp = number(e.args, 0);
        else if (key == "fadecycle")
            _fadeCycle = int32_t(number(e.args, 0));
        else if (key == "fadeamount")
            _fadeAmount = number(e.args, 0);
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
        } else if (key == "outlinecolor") {
            w.outlineColor = color(e.args);
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
        } else if (key == "maxchars") {
            item.maxChars = int32_t(number(e.args, 0));
        } else if (key == "maxpaintchars") {
            item.maxPaintChars = int32_t(number(e.args, 0));
        } else if (key == "ownerdraw") {
            item.ownerDraw = int32_t(number(e.args, 0));
        } else if (key == "elementheight") {
            item.elementHeight = number(e.args, 0);
        } else if (key == "feeder" && !e.args.empty()) {
            item.feeder = same(e.args[0], "FEEDER_SAVEGAMES") ? kFeederSaves : number(e.args, 0);
        } else if (key == "columns") {
            // How many, then each one's place, width and letters at most.
            const auto n = size_t(std::max(0.0f, number(e.args, 0)));
            for (size_t c = 0; c < n; ++c)
                item.columns.push_back(
                    {number(e.args, 1 + c * 3), number(e.args, 2 + c * 3), int32_t(number(e.args, 3 + c * 3))});
        } else if (key == "doubleclick") {
            item.doubleClick = e.block;
        } else if (key == "appearance_slot") {
            item.appearanceSlot = int32_t(number(e.args, 0));
        } else if (key == "asset_model" && !e.args.empty()) {
            // Framed by its first frame's bounds, as the renderer gives
            // them (0x4a1574).
            std::vector<uint8_t> data;
            Md3Model model;
            if (!_files.read(e.args[0], data) && !parseMd3(data, model) && !model.frames.empty()) {
                item.model = _models.add(e.args[0]);
                item.modelMins = model.frames[0].mins;
                item.modelMaxs = model.frames[0].maxs;
            }
        }
    }
    return item;
}

void SpMenus::readMenu(const MenuDef &def) {
    // A new menu fades as the assets say until it says otherwise
    // (0x49fa6e).
    Menu m;
    m.fadeClamp = _fadeClamp;
    m.fadeCycle = _fadeCycle;
    m.fadeAmount = _fadeAmount;
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
        else if (key == "fadeclamp")
            m.fadeClamp = number(e.args, 0);
        else if (key == "fadecycle")
            m.fadeCycle = int32_t(number(e.args, 0));
        else if (key == "fadeamount")
            m.fadeAmount = number(e.args, 0);
        else if (key == "appearanceincrement")
            m.appearanceIncrement = number(e.args, 0);
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
    // Its items' appearances start again from none (0x49f849).
    target->visible = true;
    target->focus = true;
    runScript(*target, target->onOpen);
    target->appearanceTime = 0;
    target->appearanceCount = 0;
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
            // Shown, and fading from its alpha as it is (0x49d550).
            for (Item *i : items(arg())) {
                i->visible = true;
                i->fadingOut = cmd == "fadeout";
                i->fadingIn = !i->fadingOut;
            }
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
        } else if (cmd == "setitemflag") {
            arg();
            arg();
        } else if (cmd == "activate") {
            // "activate <menu> <item>": that item's action run (0x49dd00).
            const std::string menuName = arg();
            const std::string itemName = arg();
            if (Menu *m = findMenu(menuName))
                for (Item &i : m->items)
                    if (same(i.name, itemName)) {
                        runScript(*m, i.action);
                        break;
                    }
        } else if (cmd == "defer") {
            // "defer VideoSetup <menu>": if the video settings were
            // changed, that menu opens (a warning) and the rest of the
            // script waits for its "rundeferred" (0x49dfd0, 0x499e80).
            if (same(arg(), "VideoSetup")) {
                const std::string warning = arg();
                if (_cvars.integer("ui_r_modified") != 0) {
                    open(warning);
                    _deferredMenu = &menu;
                    _deferred.assign(script.begin() + std::ptrdiff_t(at), script.end());
                    return;
                }
            }
        } else if (cmd == "rundeferred") {
            // The script put off, which stays kept (0x49e020).
            if (_deferredMenu && !_deferred.empty()) {
                const Tokens deferred = _deferred;
                runScript(*_deferredMenu, deferred);
            }
        } else if (cmd == "orbit") {
            // "orbit <item> <x> <y> <cx> <cy> <ms>": shown, placed at x,
            // y, and turning round cx, cy (0x49d9e0).
            const std::string name = arg();
            std::array<float, 4> v{};
            for (float &f : v)
                f = std::strtof(arg().c_str(), nullptr);
            const auto time = int32_t(std::atoi(arg().c_str()));
            for (Item *i : items(name)) {
                i->visible = i->orbiting = true;
                i->offsetTime = time;
                i->orbitCenter = {v[2], v[3]};
                i->rect[0] = v[0];
                i->rect[1] = v[1];
            }
        } else if (cmd == "transition") {
            // "transition <item> <from x y w h> <to x y w h> <ms> <steps>":
            // shown at the first rectangle and moving to the second, each
            // side by its whole distance over the steps (0x49d880).
            const std::string name = arg();
            Rect from{};
            Rect to{};
            for (float &f : from)
                f = std::strtof(arg().c_str(), nullptr);
            for (float &f : to)
                f = std::strtof(arg().c_str(), nullptr);
            const auto time = int32_t(std::atoi(arg().c_str()));
            const float steps = std::strtof(arg().c_str(), nullptr);
            for (Item *i : items(name)) {
                i->visible = i->inTransition = true;
                i->rect = from;
                i->target = to;
                i->offsetTime = time;
                for (size_t k = 0; k < 4; ++k)
                    i->step[k] = float(std::abs(int32_t(to[k] - from[k]))) * (1 / steps);
            }
        } else if (cmd == "opengotomenu") {
            // Nothing to open: no "goto" menu is kept.
        } else if (!runUiScript(menu, script, at)) {
            return;
        }
    }
}

void SpMenus::updateSetting(const std::string &name) {
    // A setting's knock-on changes, as jk2sp.exe's "update" (0x49b140):
    // the value read as a whole number decides them.
    const auto value = int32_t(_cvars.value(name));
    auto set = [&](const char *cvar, const char *v) { _cvars.set(cvar, v); };
    if (same(name, "s_khz")) {
        _commands.emplace_back("snd_restart");
    } else if (same(name, "ui_SetName")) {
        set("name", _cvars.string("ui_Name").c_str());
    } else if (same(name, "ui_setRate")) {
        const float rate = _cvars.value("rate");
        set("cl_maxpackets", rate >= 5000 ? "30" : "15");
        set("cl_packetdup", rate >= 5000 || rate < 4000 ? "1" : "2");
    } else if (same(name, "ui_GetName")) {
        set("ui_Name", _cvars.string("name").c_str());
    } else if (same(name, "ui_r_colorbits")) {
        // The depth buffer's bits to go with the colour's.
        if (value == 0 || value == 16)
            set("ui_r_depthbits", value == 0 ? "0" : "16");
        else if (value == 32)
            set("ui_r_depthbits", "24");
    } else if (same(name, "ui_r_lodbias")) {
        // The curves' subdivisions to go with the detail.
        if (value >= 0 && value <= 2)
            set("ui_r_subdivisions", value == 0 ? "4" : value == 1 ? "12" : "20");
    } else if (same(name, "ui_r_glCustom")) {
        // The quality presets, high to fastest (the table at 0x49b5dc).
        struct Preset {
            const char *subdivisions, *lodbias, *colorbits, *depthbits, *picmip, *mode, *texturebits, *fastSky,
                *inGameVideo, *textureMode;
        };
        static const Preset kPresets[] = {
            {"4", "0", "32", "24", "0", "4", "32", "0", "1", "GL_LINEAR_MIPMAP_LINEAR"},
            {"4", "0", "0", "24", "1", "3", "0", "0", "1", "GL_LINEAR_MIPMAP_LINEAR"},
            {"12", "1", "0", "0", "2", "3", "0", "1", "0", "GL_LINEAR_MIPMAP_NEAREST"},
            {"20", "2", "16", "16", "3", "3", "16", "1", "0", "GL_LINEAR_MIPMAP_NEAREST"},
        };
        if (value >= 0 && value <= 3) {
            const Preset &p = kPresets[value];
            set("ui_r_fullScreen", "1");
            set("ui_r_subdivisions", p.subdivisions);
            set("ui_r_lodbias", p.lodbias);
            set("ui_r_colorbits", p.colorbits);
            set("ui_r_depthbits", p.depthbits);
            set("ui_r_picmip", p.picmip);
            set("ui_r_mode", p.mode);
            set("ui_r_texturebits", p.texturebits);
            set("ui_r_fastSky", p.fastSky);
            set("ui_r_inGameVideo", p.inGameVideo);
            set("ui_r_texturemode", p.textureMode);
        }
    } else if (same(name, "ui_mousePitch")) {
        // Inverted looking up and down, or not.
        set("m_pitch", value == 0 ? "0.022" : "-0.022");
    }
}

bool SpMenus::runUiScript(Menu &menu, const Tokens &script, size_t &at) {
    // The engine's own (0x499ef0): each a name, some with arguments after.
    if (at >= script.size())
        return false;
    auto arg = [&]() -> std::string { return at < script.size() ? script[at++] : std::string(); };
    const std::string name = lower(arg());
    auto closeToGame = [&]() {
        closeAll();
        _catcher &= ~kCatchUi;
    };
    if (name == "startgame") {
        startGame();
    } else if (name == "startmap") {
        closeToGame();
        _commands.push_back("map " + arg());
    } else if (name == "closeingame") {
        closeToGame();
    } else if (name == "closedatapad") {
        // The pages' "updated" marks cleared too (0x49a4f0).
        closeToGame();
        for (const char *mark : {"cg_updatedDataPadForcePower1", "cg_updatedDataPadForcePower2",
                                 "cg_updatedDataPadForcePower3", "cg_updatedDataPadObjective"})
            _cvars.set(mark, "0");
    } else if (name == "loadauto") {
        closeToGame();
        _commands.emplace_back("load auto");
    } else if (name == "loadgame") {
        // The saved game chosen (0x49a029).
        if (_saveChosen >= 0 && size_t(_saveChosen) < saves().size()) {
            closeToGame();
            _commands.push_back("load " + _saves[size_t(_saveChosen)].name);
        }
    } else if (name == "deletegame") {
        if (_saveChosen >= 0 && size_t(_saveChosen) < saves().size()) {
            _commands.push_back("wipe " + _saves[size_t(_saveChosen)].name);
            _savesRead = false;
        }
    } else if (name == "savegame") {
        // Into the first of jkii00 to jkii99 that's free, with the
        // description typed (0x49a112, 0x49a5e0).
        std::string slot;
        for (int n = 0; n < 100 && slot.empty(); ++n) {
            char candidate[16];
            std::snprintf(candidate, sizeof candidate, "jkii%02d", n);
            std::vector<uint8_t> file;
            if (!readSave || !readSave(candidate, file))
                slot = candidate;
        }
        if (!slot.empty()) {
            saveComment = _cvars.string("ui_gameDesc");
            _commands.push_back("save " + slot);
        }
        _savesRead = false;
    } else if (name == "readsavedirectory") {
        _savesRead = false;
    } else if (name == "quit") {
        _commands.emplace_back("quit");
    } else if (name == "leave") {
        // To the main menu (0x49a1f4), which disconnecting opens.
        closeAll();
        _commands.emplace_back("disconnect");
    } else if (name == "controls") {
        // A menu that 1.02a's files don't have (0x49a1ac).
        _cvars.set("cl_paused", "1");
        _catcher = kCatchUi;
        closeAll();
        open("setup_menu2");
    } else if (name == "resetdefaults") {
        // (0x49c5e0)
        _commands.emplace_back("cvar_restart");
        _commands.emplace_back("exec default.cfg");
        _commands.emplace_back("vid_restart");
        _cvars.set("com_introPlayed", "1");
    } else if (name == "clearerror") {
        _cvars.set("com_errorMessage", "");
    } else if (name == "glcustom") {
        _cvars.set("ui_r_glCustom", "4");
    } else if (name == "getvideosetup" || name == "updatevideosetup") {
        // The video settings copied to the menus' own (ui_r_...) to be
        // changed, or back, the video then restarted (0x49c2c0, 0x49c160).
        static const std::pair<const char *, const char *> kVideo[] = {
            {"ui_r_mode", "r_mode"},
            {"ui_r_fullscreen", "r_fullscreen"},
            {"ui_r_colorbits", "r_colorbits"},
            {"ui_r_lodbias", "r_lodbias"},
            {"ui_r_picmip", "r_picmip"},
            {"ui_r_texturebits", "r_texturebits"},
            {"ui_r_texturemode", "r_texturemode"},
            {"ui_r_detailtextures", "r_detailtextures"},
            {"ui_r_ext_compress_textures", "r_ext_compress_textures"},
            {"ui_r_depthbits", "r_depthbits"},
            {"ui_r_subdivisions", "r_subdivisions"},
            {"ui_r_fastSky", "r_fastSky"},
            {"ui_r_inGameVideo", "r_inGameVideo"},
            {"ui_r_allowExtensions", "r_allowExtensions"},
            {"ui_cg_shadows", "cg_shadows"},
        };
        const bool get = name == "getvideosetup";
        for (const auto &[own, real] : kVideo)
            if (get)
                _cvars.set(own, _cvars.string(real));
            else
                _cvars.set(real, _cvars.string(own));
        _cvars.set("ui_r_modified", "0");
        if (!get)
            _commands.emplace_back("vid_restart");
    } else if (name == "nextdatapadforcepower" || name == "prevdatapadforcepower" || name == "nextdatapadweapon" ||
               name == "prevdatapadweapon" || name == "nextdatapadinventory" || name == "prevdatapadinventory") {
        // The client game's own commands (0x49a283 on).
        static const std::pair<const char *, const char *> kPages[] = {
            {"nextdatapadforcepower", "dpforcenext"}, {"prevdatapadforcepower", "dpforceprev"},
            {"nextdatapadweapon", "dpweapnext"},      {"prevdatapadweapon", "dpweapprev"},
            {"nextdatapadinventory", "dpinvnext"},    {"prevdatapadinventory", "dpinvprev"},
        };
        for (const auto &[script, command] : kPages)
            if (name == script)
                _commands.emplace_back(command);
    } else if (name == "checkvid1data") {
        arg();
        arg();
    } else if (name == "update") {
        updateSetting(arg());
    } else if (name == "loadcontrols" || name == "savecontrols") {
        // The bindings are Reluva's own, kept as they're changed.
    } else if (print) {
        // Said, and the script goes on (0x49a5c1).
        print("menus: unknown UI script " + name + " in " + menu.name + "\n");
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

SpMenus::Item *SpMenus::moveFocus(Menu &menu, int step) {
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
            return &menu.items[size_t(i)];
        }
    }
    return nullptr;
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
    case kBind:
        // Then waiting for the key (0x4a396c).
        if (click || enter)
            _bindItem = &item;
        break;
    case kEditField:
    case kNumericField:
        // Typing starts at the field's start, over what's there
        // (0x4a5741, 0x4a579f).
        if (click || enter) {
            item.cursor = 0;
            _editItem = &item;
            setTypingOver(true);
        }
        break;
    default:
        if (click || enter)
            runScript(menu, item.action);
        break;
    }
}

Error SpMenus::keyEvent(int32_t key) {
    // A binding waiting for its key takes every key.
    if (_bindItem) {
        if (!(key & q3key::kCharFlag))
            bindKey(key);
        return {};
    }
    Menu *menu = focusedMenu();
    if (_editItem && menu) {
        // As 0x4a539f: the field takes the key first; one it gives back
        // ends the typing, as does a click (which then goes on as a
        // click); Tab and the arrows up and down go no further; any other
        // key goes on as it would.
        if (!editKey(*menu, key)) {
            _editItem = nullptr;
            return {};
        }
        if (key == q3key::kMouse1 || key == q3key::kMouse2 || key == q3key::kMouse3)
            _editItem = nullptr;
        else if (key == q3key::kTab || key == q3key::kUp || key == q3key::kDown)
            return {};
    }
    if (!menu || (key & q3key::kCharFlag))
        return {};
    if (key == q3key::kEscape) {
        runScript(*menu, menu->onEsc);
        return {};
    }
    Item *focused = nullptr;
    for (Item &item : menu->items)
        if (item.focus && canFocus(item))
            focused = &item;
    if (focused && focused->type == kListBox &&
        (key == q3key::kMouse1 || key == q3key::kUp || key == q3key::kDown || key == q3key::kWheelUp ||
         key == q3key::kWheelDown)) {
        listKey(*menu, *focused, key);
        return {};
    }
    if (key == q3key::kUp || key == q3key::kDown || key == q3key::kTab) {
        moveFocus(*menu, key == q3key::kUp ? -1 : 1);
        return {};
    }
    if (focused)
        itemKey(*menu, *focused, key);
    return {};
}

void SpMenus::bindKey(int32_t key) {
    // As jk2sp.exe's (0x4a3a71 on): Esc gives up, ` is ignored, Backspace
    // unbinds the command's keys; another key is bound to the command,
    // taken from whatever it did, as its first or second key, or in place
    // of both if it has two already.
    Item &item = *_bindItem;
    if (key == q3key::kEscape) {
        _bindItem = nullptr;
        return;
    }
    if (key == '`')
        return;
    _bindItem = nullptr;
    if (!bindings || item.cvar.empty())
        return;
    std::vector<int32_t> keys;
    for (int32_t k = 0; k < q3key::kCount && keys.size() < 2; ++k)
        if (same(bindings->get(k), item.cvar))
            keys.push_back(k);
    if (key == q3key::kBackspace) {
        for (const int32_t k : keys)
            bindings->set(k, "");
        return;
    }
    if (keys.size() == 2 && keys[0] != key && keys[1] != key)
        for (const int32_t k : keys)
            bindings->set(k, "");
    bindings->set(key, item.cvar);
}

bool SpMenus::typingOver() const {
    return getOverstrike ? getOverstrike() : _overstrike;
}

void SpMenus::setTypingOver(bool on) {
    if (setOverstrike)
        setOverstrike(on);
    else
        _overstrike = on;
}

bool SpMenus::editKey(Menu &menu, int32_t key) {
    // As jk2sp.exe's (0x4a3e50): typed letters go in at the cursor, or
    // over the letter there; the cursor keys move it, the first letter
    // shown following it out of a field shown narrower than its text.
    // False ends the typing.
    Item &item = *_editItem;
    std::string value = _cvars.string(item.cvar);
    if (item.maxChars > 0 && int32_t(value.size()) > item.maxChars)
        value.resize(size_t(item.maxChars));
    const auto len = int32_t(value.size());
    auto set = [&] { _cvars.set(item.cvar, value); };
    if (key & q3key::kCharFlag) {
        key &= ~q3key::kCharFlag;
        if (key == 8) {
            // Backspace's character: the letter before the cursor.
            if (item.cursor > 0 && item.cursor <= len) {
                value.erase(size_t(item.cursor - 1), 1);
                if (--item.cursor < item.paintOffset)
                    --item.paintOffset;
            }
            set();
            return true;
        }
        if (key < 32)
            return true;
        if (item.type == kNumericField && (key < '0' || key > '9'))
            return false;
        if (!typingOver()) {
            if (len == 255 || (item.maxChars > 0 && len >= item.maxChars))
                return true;
            value.insert(std::min(size_t(item.cursor), value.size()), 1, char(key));
        } else {
            if (item.maxChars > 0 && item.cursor >= item.maxChars)
                return true;
            // (Past the text's end, as the original's, it isn't seen.)
            if (item.cursor < len)
                value[size_t(item.cursor)] = char(key);
            else if (item.cursor == len)
                value += char(key);
        }
        set();
        if (item.cursor < len + 1) {
            ++item.cursor;
            if (item.maxPaintChars > 0 && item.cursor > item.maxPaintChars)
                ++item.paintOffset;
        }
    } else {
        switch (key) {
        case q3key::kDelete:
        case 171: // KP_DEL
            if (item.cursor < len) {
                value.erase(size_t(item.cursor), 1);
                set();
            }
            return true;
        case q3key::kRight:
        case 165: // KP_RIGHTARROW
            if (item.maxPaintChars > 0 && item.cursor >= item.maxPaintChars && item.cursor < len) {
                ++item.cursor;
                ++item.paintOffset;
            } else if (item.cursor < len) {
                ++item.cursor;
            }
            return true;
        case q3key::kLeft:
        case 163: // KP_LEFTARROW
            if (item.cursor > 0)
                --item.cursor;
            if (item.cursor < item.paintOffset)
                --item.paintOffset;
            return true;
        case q3key::kHome:
        case 160: // KP_HOME
            item.cursor = item.paintOffset = 0;
            return true;
        case q3key::kEnd:
        case 166: // KP_END
            // (Without a limit shown, everything scrolls out of sight.)
            item.cursor = len;
            if (len > item.maxPaintChars)
                item.paintOffset = len - item.maxPaintChars;
            return true;
        case q3key::kInsert:
        case 170: // KP_INS
            setTypingOver(!typingOver());
            return true;
        default:
            break;
        }
    }
    // A character's number too: Tab and down to the next field, up to
    // the one before, if it's one to type in; Enter and Esc end it.
    if (key == q3key::kTab || key == q3key::kDown || key == 167) {
        if (Item *next = moveFocus(menu, 1); next && (next->type == kEditField || next->type == kNumericField))
            _editItem = next;
    } else if (key == q3key::kUp || key == 161) {
        if (Item *prev = moveFocus(menu, -1); prev && (prev->type == kEditField || prev->type == kNumericField))
            _editItem = prev;
    }
    return key != q3key::kEnter && key != q3key::kKpEnter && key != q3key::kEscape;
}

int32_t SpMenus::listCount(const Item &item) {
    return item.feeder == kFeederSaves ? int32_t(saves().size()) : 0;
}

int32_t SpMenus::listRows(const Item &item) const {
    return item.elementHeight > 0 ? std::max(1, int32_t((item.rect[3] - 2) / item.elementHeight)) : 1;
}

std::string SpMenus::listText(const Item &item, int32_t row, size_t column) {
    // The saved games' description, then when they were saved (0x499b60);
    // other feeders have no text in single player.
    if (item.feeder != kFeederSaves || row < 0 || size_t(row) >= saves().size())
        return {};
    const SaveEntry &e = _saves[size_t(row)];
    return column == 0 ? e.comment : column == 1 ? e.date : std::string();
}

void SpMenus::listKey(Menu &menu, Item &item, int32_t key) {
    // Rows chosen by a click or the arrows, a second click soon after a
    // double click; the scroll bar's arrows and the wheel scroll. As Team
    // Arena's.
    const int32_t count = listCount(item);
    const int32_t rows = listRows(item);
    auto scroll = [&](int32_t by) { item.startPos = std::clamp(item.startPos + by, 0, std::max(0, count - rows)); };
    const int32_t chosen = item.feeder == kFeederSaves ? _saveChosen : -1;
    if (key == q3key::kWheelUp || key == q3key::kWheelDown) {
        scroll(key == q3key::kWheelUp ? -1 : 1);
        return;
    }
    if (key == q3key::kUp || key == q3key::kDown) {
        const int32_t row = std::clamp(chosen + (key == q3key::kUp ? -1 : 1), 0, std::max(0, count - 1));
        if (row < item.startPos)
            item.startPos = row;
        else if (row >= item.startPos + rows)
            item.startPos = row - rows + 1;
        if (count > 0 && item.feeder == kFeederSaves)
            listSelect(row);
        return;
    }
    const Rect &r = item.rect;
    if (!inside(r, _cursorX, _cursorY))
        return;
    if (_cursorX >= r[0] + r[2] - kScrollbarSize - 1) {
        if (_cursorY < r[1] + kScrollbarSize + 1)
            scroll(-1);
        else if (_cursorY > r[1] + r[3] - kScrollbarSize - 1)
            scroll(1);
        else
            scroll(_cursorY < r[1] + r[3] / 2 ? -rows : rows);
        return;
    }
    const int32_t row = item.startPos + int32_t((_cursorY - r[1] - 1) / item.elementHeight);
    if (_time < item.lastClickTime + kDoubleClickMs && row == item.lastClickRow)
        runScript(menu, item.doubleClick);
    item.lastClickTime = _time;
    item.lastClickRow = row;
    if (row >= 0 && row < count && item.feeder == kFeederSaves)
        listSelect(row);
}

const std::vector<SpMenus::SaveEntry> &SpMenus::saves() {
    if (!_savesRead)
        readSaves();
    return _saves;
}

void SpMenus::readSaves() {
    // As ReadSaveDirectory (0x49c620): the game's saves/*.sav and Reluva's
    // own, all but "current"; "auto" isn't listed but makes Resume
    // possible. Newest first, 100 at most.
    _savesRead = true;
    _saves.clear();
    _cvars.set("ui_gameDesc", "");
    _cvars.set("ui_SelectionOK", "0");
    _cvars.set("ui_ResumeOK", "0");
    std::vector<std::string> names;
    for (const std::string &n : _files.list()) {
        const std::string key = lower(n);
        if (key.rfind("saves/", 0) == 0 && key.size() > 10 && key.compare(key.size() - 4, 4, ".sav") == 0 &&
            key.find('/', 6) == std::string::npos)
            names.push_back(key.substr(6, key.size() - 10));
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(saveDir, ec), end; !saveDir.empty() && !ec && it != end;
         it.increment(ec)) {
        const std::u8string u8 = it->path().filename().u8string();
        const std::string n = lower(std::string(u8.begin(), u8.end()));
        if (it->is_regular_file(ec) && n.size() > 4 && n.compare(n.size() - 4, 4, ".sav") == 0)
            names.push_back(n.substr(0, n.size() - 4));
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    for (const std::string &n : names) {
        if (n == "current" || (listSave && !listSave(n)))
            continue;
        if (n == "auto") {
            _cvars.set("ui_ResumeOK", "1");
            continue;
        }
        std::vector<uint8_t> file;
        if (!readSave || !readSave(n, file))
            continue;
        raven::SaveReader r(std::move(file));
        CvarStore ignored;
        SaveHeader header;
        if (readSaveHeader(r, ignored, header))
            continue;
        SaveEntry e;
        e.name = n;
        e.comment = header.comment;
        e.time = header.time;
        e.map = header.map;
        const std::time_t t = header.time;
        if (const std::tm *tm = std::localtime(&t)) {
            // asctime's form, without its line break.
            char when[64];
            std::strftime(when, sizeof when, "%a %b %d %H:%M:%S %Y", tm);
            e.date = when;
        }
        _saves.push_back(std::move(e));
        if (_saves.size() == kMaxSaves)
            break;
    }
    std::stable_sort(_saves.begin(), _saves.end(),
                     [](const SaveEntry &a, const SaveEntry &b) { return a.time > b.time; });
    listSelect(_saveChosen);
}

void SpMenus::listSelect(int32_t row) {
    // The saved game chosen: whether there's one, its description for the
    // save menu to start from, its picture (0x49a6b0).
    _saveChosen = row;
    const bool ok = row >= 0 && size_t(row) < _saves.size();
    _cvars.set("ui_SelectionOK", ok ? "1" : "0");
    _cvars.set("ui_gameDesc", ok ? _saves[size_t(row)].comment : std::string());
    _savePicture = {};
    std::vector<uint8_t> file;
    if (!ok || !readSave || !readSave(_saves[size_t(row)].name, file))
        return;
    raven::SaveReader r(std::move(file));
    CvarStore ignored;
    SaveHeader header;
    if (!readSaveHeader(r, ignored, header) && image::decode(header.shot, _savePicture))
        _savePicture = {};
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
    for (Menu &m : _menus)
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

void SpMenus::drawWindow(Window &w, const Menu &menu) {
    // A filled window with a picture draws it in its back colour, which
    // fades (0x4a2909).
    const Rect &r = w.rect;
    if (w.style == kStyleFilled && w.background) {
        fade(w, w.backColor[3], menu);
        drawRect(r, w.backColor, w.background);
    } else if (w.style == kStyleFilled) {
        drawRect(r, w.backColor, _white);
    }
    if (w.style == kStyleShader && w.background)
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
    case kNumericField:
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

void SpMenus::fade(Window &w, float &alpha, const Menu &menu) const {
    // As jk2sp.exe's (0x4a2760): each fade cycle, a fade out takes the
    // fade amount off the alpha, the window hidden once it's down to 0; a
    // fade in adds it up to the menu's clamp.
    if ((!w.fadingIn && !w.fadingOut) || _time <= w.nextTime)
        return;
    w.nextTime = _time + menu.fadeCycle;
    if (w.fadingOut) {
        alpha -= menu.fadeAmount;
        if (alpha <= 0)
            w.fadingOut = w.visible = false;
    } else {
        alpha += menu.fadeAmount;
        if (alpha >= menu.fadeClamp) {
            alpha = menu.fadeClamp;
            w.fadingIn = false;
        }
    }
}

void SpMenus::move(Item &item) const {
    // As the item painter's start (0x4a20d0), a step each offsetTime ms:
    // an orbit turns the item's middle 3 degrees round its centre; a
    // transition takes each side of its rectangle a step nearer the
    // target, ending once all four are there.
    Rect &r = item.rect;
    if (item.orbiting && _time > item.nextTime) {
        item.nextTime = _time + item.offsetTime;
        constexpr double kTurn = 3 * 3.14159265358979323846 / 180;
        const float w = r[2] * 0.5f;
        const float h = r[3] * 0.5f;
        const float rx = r[0] + w - item.orbitCenter[0];
        const float ry = r[1] + h - item.orbitCenter[1];
        const auto c = float(std::cos(kTurn));
        const auto s = float(std::sin(kTurn));
        r[0] = rx * c - ry * s + item.orbitCenter[0] - w;
        r[1] = rx * s + ry * c + item.orbitCenter[1] - h;
    }
    if (item.inTransition && _time > item.nextTime) {
        item.nextTime = _time + item.offsetTime;
        int done = 0;
        for (size_t i = 0; i < 4; ++i) {
            if (r[i] == item.target[i]) {
                ++done;
            } else if (r[i] < item.target[i]) {
                r[i] += item.step[i];
                if (r[i] > item.target[i]) {
                    r[i] = item.target[i];
                    ++done;
                }
            } else {
                r[i] -= item.step[i];
                if (r[i] < item.target[i]) {
                    r[i] = item.target[i];
                    ++done;
                }
            }
        }
        if (done == 4)
            item.inTransition = false;
    }
}

SpMenus::Color SpMenus::textColor(const Menu &menu, Item &item) {
    // As jk2sp.exe's (0x4a02f0): the fore colour, its alpha faded; with
    // the focus, the menu's focus colour pulsing down to 0.8 of itself
    // and back (the time over 75 ms as the sine's angle); the menu's
    // disabled colour if the item's setting disables it; grey while
    // another of its menu's items waits for a key.
    fade(item, item.foreColor[3], menu);
    Color c = item.foreColor;
    if (item.focus) {
        const float t = 0.5f + 0.5f * std::sin(float(_time / 75));
        for (size_t i = 0; i < 4; ++i) {
            const float focus = menu.focusColor[i];
            c[i] = std::clamp(focus + (focus * 0.8f - focus) * t, 0.0f, 1.0f);
        }
    }
    if (!enabled(item))
        c = menu.disableColor;
    if (_bindItem && _bindItem != &item &&
        std::any_of(menu.items.begin(), menu.items.end(), [&](const Item &i) { return &i == _bindItem; }))
        c = {0.5f, 0.5f, 0.5f, 1};
    return c;
}

void SpMenus::drawItem(const Menu &menu, Item &item) {
    move(item);
    drawWindow(item, menu);
    if (item.type == kListBox)
        return drawListBox(item);
    if (item.ownerDraw) {
        fade(item, item.foreColor[3], menu);
        return drawOwnerDraw(item);
    }
    if (item.type == kModel)
        return drawModel(item);
    if (item.text.empty() && item.type != kSlider)
        return;
    // The text placed by its alignment from the rectangle's corner, its
    // top at textaligny.
    const Color c = textColor(menu, item);
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
    std::string value = itemValue(item);
    if (item.type == kEditField || item.type == kNumericField) {
        // As 0x4a08c3: from the first letter shown, as many as it shows;
        // while typed in, the cursor (_ typing over, | inserting, not
        // blinking) at its letter (0x499a60).
        value.erase(0, std::min(value.size(), size_t(std::max(item.paintOffset, 0))));
        if (item.maxPaintChars > 0 && int32_t(value.size()) > item.maxPaintChars)
            value.resize(size_t(item.maxPaintChars));
        drawText(valueX, y, item.textScale, c, value, item.textStyle, item.font);
        if (&item == _editItem) {
            const auto at = size_t(std::clamp(item.cursor - item.paintOffset, 0, int32_t(value.size())));
            const float cx = valueX + textWidth(value.substr(0, at), item.textScale, item.font);
            drawText(cx, y, item.textScale, c, typingOver() ? "_" : "|", item.textStyle | 1, item.font);
        }
        return;
    }
    // A binding waiting for its key shows ??? (as Team Arena's).
    if (&item == _bindItem)
        value = "???";
    if (!value.empty())
        drawText(valueX, y, item.textScale, c, value, item.textStyle, item.font);
}

void SpMenus::drawListBox(const Item &item) {
    // A vertical list as jk2sp.exe draws it (0x4a09c0): the scroll bar
    // down its right (arrows, bar, thumb, 16 wide); each row's columns'
    // text from the rectangle's corner, 1 in, then 4 more and the column's
    // place; the chosen row's highlight 6 below its top, in the outline
    // colour.
    const Rect &r = item.rect;
    const int32_t count = listCount(item);
    const int32_t rows = listRows(item);
    const float barX = r[0] + r[2] - kScrollbarSize - 1;
    drawRect({barX, r[1] + 1, kScrollbarSize, kScrollbarSize}, {1, 1, 1, 1}, _scrollUp);
    drawRect({barX, r[1] + kScrollbarSize, kScrollbarSize, r[3] - kScrollbarSize * 2}, {1, 1, 1, 1}, _scrollBar);
    drawRect({barX, r[1] + r[3] - kScrollbarSize - 1, kScrollbarSize, kScrollbarSize}, {1, 1, 1, 1}, _scrollDown);
    const float travel = r[3] - kScrollbarSize * 3 - 2;
    const float frac = count > rows ? float(item.startPos) / float(count - rows) : 0;
    drawRect({barX, r[1] + kScrollbarSize + frac * travel, kScrollbarSize, kScrollbarSize}, {1, 1, 1, 1}, _scrollThumb);
    const int32_t chosen = item.feeder == kFeederSaves ? _saveChosen : -1;
    for (int32_t i = 0; i < rows && item.startPos + i < count; ++i) {
        const int32_t row = item.startPos + i;
        const float y = r[1] + 1 + float(i) * item.elementHeight;
        for (size_t c = 0; c < item.columns.size(); ++c) {
            std::string t = listText(item, row, c);
            if (item.columns[c].maxChars > 0 && int32_t(t.size()) > item.columns[c].maxChars)
                t.resize(size_t(item.columns[c].maxChars));
            if (!t.empty())
                drawText(r[0] + 1 + 4 + item.columns[c].pos, y, item.textScale, item.foreColor, t, item.textStyle,
                         item.font);
        }
        if (item.columns.empty())
            if (const std::string t = listText(item, row, 0); !t.empty())
                drawText(r[0] + 1 + 4, y, item.textScale, item.foreColor, t, item.textStyle, item.font);
        if (row == chosen)
            drawRect({r[0] + 1 + 2, y + 6, r[2] - kScrollbarSize - 4, item.elementHeight}, item.outlineColor, _white);
    }
}

void SpMenus::drawOwnerDraw(const Item &item) {
    // The engine's own drawing for an item (0x49b7f0).
    const Rect &r = item.rect;
    switch (item.ownerDraw) {
    case 203:
    case 204:
    case 205:
    case 206: {
        // The datapad: the client game draws its frame, then the mission,
        // weapons, inventory or Force powers page (0x49b877 on).
        if (!drawDataPad)
            break;
        for (const int32_t command : {11, 11 + item.ownerDraw - 202}) {
            std::vector<Picture> drawn;
            const HandleTable *table = drawDataPad(command, drawn);
            for (Picture p : drawn) {
                p.shader = table ? _shaders.add(table->name(p.shader)) : 0;
                _pictures.push_back(p);
            }
        }
        break;
    }
    case 236:
        // The chosen saved game's picture, and under it the level it's of
        // (0x49b8d3).
        if (!_savePicture.rgba.empty()) {
            SpMenuHost::RawImage image;
            image.x = r[0];
            image.y = r[1];
            image.w = r[2];
            image.h = r[3];
            image.cols = _savePicture.w;
            image.rows = _savePicture.h;
            image.rgba = _savePicture.rgba;
            _raw.push_back(std::move(image));
        }
        if (_saveChosen >= 0 && size_t(_saveChosen) < saves().size())
            drawText(r[0], r[1] + r[3], item.textScale, item.foreColor, _saves[size_t(_saveChosen)].map, 0, item.font);
        break;
    case 250:
        // While a binding waits for its key (0x49bb00), the string
        // package's MENUS_WAITINGFORKEY, which 1.02a's packages haven't.
        if (_bindItem)
            if (const std::string t = text("@MENUS_WAITINGFORKEY"); !t.empty() && t[0] != '@')
                drawText(r[0], r[1], item.textScale, item.foreColor, t, 0, item.font);
        break;
    case 249:
        // The video driver page's details (0x49bb50): OpenGL's vendor,
        // version, pixel format and extensions, a line each from the top.
        // Reluva's renderer isn't OpenGL: its own description is shown on
        // the first line, and the rest left out.
        if (!rendererName.empty())
            drawText(r[0], r[1], item.textScale, item.foreColor, rendererName, item.textStyle, item.font);
        break;
    default:
        // 200, 201 and 242 no menu uses.
        break;
    }
}

void SpMenus::drawModel(const Item &item) {
    // As jk2sp.exe (0x4a1490): a view 45 degrees each way within the
    // rectangle (1 in), no world; the model as far off as makes its height
    // fit, centred across and up, turning with the clock (its
    // model_angle and model_rotation unused).
    if (!item.model)
        return;
    const Rect &r = item.rect;
    const auto &lo = item.modelMins;
    const auto &hi = item.modelMaxs;
    const std::array<float, 3> origin{0.5f * (hi[2] - lo[2]) * kModelDistance, 0.5f * (lo[1] + hi[1]),
                                      -0.5f * (lo[2] + hi[2])};
    const float yaw = float(_time) * kModelTurn * kPi / 180.0f;
    const RefEntityLayout &l = kSpRefEntityLayout;
    std::vector<uint8_t> ent(l.size, 0);
    auto put = [&ent](size_t at, auto value) { std::memcpy(ent.data() + at, &value, sizeof value); };
    put(l.type, int32_t(l.typeModel));
    put(l.flags, kModelFlags);
    put(l.model, item.model);
    const float axes[9] = {std::cos(yaw), std::sin(yaw), 0, -std::sin(yaw), std::cos(yaw), 0, 0, 0, 1};
    std::memcpy(ent.data() + l.axes, axes, sizeof axes);
    std::memcpy(ent.data() + l.origin, origin.data(), sizeof(float) * 3);
    std::memcpy(ent.data() + l.oldOrigin, origin.data(), sizeof(float) * 3);
    std::memcpy(ent.data() + l.lightingOrigin, origin.data(), sizeof(float) * 3);
    std::array<uint8_t, kRefdefSize> refdef{};
    auto def = [&refdef](size_t at, auto value) { std::memcpy(refdef.data() + at, &value, sizeof value); };
    def(0, int32_t(r[0] + 1));
    def(4, int32_t(r[1] + 1));
    def(8, int32_t(r[2] - 2));
    def(12, int32_t(r[3] - 2));
    def(16, kModelFov);
    def(20, kModelFov);
    def(36, 1.0f);
    def(52, 1.0f);
    def(68, 1.0f);
    def(76, int32_t(1)); // no world
    _scene.addEntity(ent.data(), ent.size(), int32_t(ent.size()));
    _scene.render(refdef.data(), _pictures.size());
}

void SpMenus::drawMenu(Menu &menu) {
    drawWindow(menu, menu);
    const Item *described = nullptr;
    for (Item &item : menu.items) {
        // An item with an appearance slot waits for the menu's count to
        // reach it, the count going up one each appearanceIncrement ms
        // from its opening (0x49ff22).
        if (item.appearanceSlot != 0) {
            if (float(_time) > menu.appearanceTime) {
                ++menu.appearanceCount;
                menu.appearanceTime = float(_time) + menu.appearanceIncrement;
            }
            if (item.appearanceSlot > menu.appearanceCount)
                continue;
        }
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
