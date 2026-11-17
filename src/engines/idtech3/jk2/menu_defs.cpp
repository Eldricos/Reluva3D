// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/menu_defs.h"

#include "common/fs.h"

#include <cstdlib>
#include <optional>

namespace reluva::idtech3::jk2 {

namespace {

bool sameWord(std::string_view a, std::string_view b) {
    return fs::toLower(std::string(a)) == fs::toLower(std::string(b));
}

// The tokens up to the "}" matching a "{" already read, nested braces
// kept; false if the text ends first.
bool readBlock(Lexer &lexer, std::vector<std::string> &out) {
    int depth = 1;
    while (const auto t = lexer.next()) {
        if (*t == "{")
            ++depth;
        else if (*t == "}" && --depth == 0)
            return true;
        out.emplace_back(*t);
    }
    return false;
}

// A definition's { ... }: each keyword with the rest of its line, or a
// block in braces (on its line or the next); `items` gets its itemDefs.
bool readEntries(Lexer &lexer, MenuEntries &into, std::vector<MenuItemDef> *items) {
    const auto open = lexer.next();
    if (!open || *open != "{")
        return false;
    while (const auto key = lexer.next()) {
        if (*key == "}")
            return true;
        if (items && sameWord(*key, "itemDef")) {
            MenuItemDef item;
            if (!readEntries(lexer, item, nullptr))
                return false;
            items->push_back(std::move(item));
            continue;
        }
        MenuEntry e;
        e.key = std::string(*key);
        // A block opened on the keyword's line, or on the next line when
        // nothing else followed it; otherwise the rest of the line.
        bool block = false;
        while (const auto arg = lexer.peek(false)) {
            if (*arg == "}")
                break; // the definition's end, left for the loop
            lexer.next(false);
            if (*arg == "{") {
                block = true;
                break;
            }
            e.args.emplace_back(*arg);
        }
        if (!block && e.args.empty() && lexer.peek() == std::optional<std::string_view>("{")) {
            lexer.next();
            block = true;
        }
        if (block) {
            e.hasBlock = true;
            if (!readBlock(lexer, e.block))
                return false;
        }
        into.entries.push_back(std::move(e));
    }
    return false;
}

} // namespace

const MenuEntry *MenuEntries::find(std::string_view key) const {
    for (const MenuEntry &e : entries)
        if (sameWord(e.key, key))
            return &e;
    return nullptr;
}

std::vector<float> MenuEntries::numbers(std::string_view key) const {
    std::vector<float> out;
    if (const MenuEntry *e = find(key))
        for (const std::string &a : e->args)
            out.push_back(std::strtof(a.c_str(), nullptr));
    return out;
}

std::string MenuItemDef::name() const {
    const MenuEntry *e = find("name");
    return e && !e->args.empty() ? e->args[0] : std::string();
}

bool MenuDefs::parse(Lexer &lexer) {
    MenuDef menu;
    if (!readEntries(lexer, menu, &menu.items) || _menus.size() >= kMaxMenus)
        return false;
    if (const MenuEntry *e = menu.find("name"); e && !e->args.empty())
        menu.name = e->args[0];
    const std::vector<float> rect = menu.numbers("rect");
    for (size_t i = 0; i < 4 && i < rect.size(); ++i)
        menu.rect[i] = rect[i];
    const std::vector<float> visible = menu.numbers("visible");
    menu.visible = !visible.empty() && visible[0] != 0;
    _menus.push_back(std::move(menu));
    return true;
}

const MenuDef *MenuDefs::find(std::string_view name) const {
    for (const MenuDef &m : _menus)
        if (sameWord(m.name, name))
            return &m;
    return nullptr;
}

} // namespace reluva::idtech3::jk2
