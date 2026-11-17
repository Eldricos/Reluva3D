// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/formats/lexer.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3::jk2 {

/** One line of a menu or item definition: its keyword and arguments, or a
 *  script's tokens (`onOpen { ... }`). */
struct MenuEntry {
    std::string key; // as written
    std::vector<std::string> args;
    std::vector<std::string> block; // between the braces, if it has them
    bool hasBlock = false;
};

/** A definition's entries, looked up by keyword whatever its case. */
struct MenuEntries {
    std::vector<MenuEntry> entries;
    const MenuEntry *find(std::string_view key) const;
    // The keyword's arguments as numbers (missing ones 0).
    std::vector<float> numbers(std::string_view key) const;
};

/** An `itemDef`. */
struct MenuItemDef : MenuEntries {
    std::string name() const;
};

/** A `menuDef`: its own entries and its items. */
struct MenuDef : MenuEntries {
    std::string name;
    std::array<float, 4> rect{}; // x, y, width, height in the 640 x 480 screen
    bool visible = false;
    std::vector<MenuItemDef> items;
};

/**
 * The menus a game's engine has read from Team Arena style menu files
 * (jk2sp.exe's, which keeps them for the client game's HUD and its own
 * menus; docs/engines/idtech3/games/jk2/systems/sp-menus.md).
 */
class MenuDefs {
public:
    static constexpr size_t kMaxMenus = 64; // jk2sp.exe's (0x49fb74)

    void reset() { _menus.clear(); }
    // After "menudef": its { ... } read into a new menu. False (none kept)
    // if the text is malformed or there are kMaxMenus already.
    bool parse(Lexer &lexer);
    // A menu by name, whatever its case; nullptr if there's none.
    const MenuDef *find(std::string_view name) const;
    const std::vector<MenuDef> &menus() const { return _menus; }

private:
    std::vector<MenuDef> _menus;
};

} // namespace reluva::idtech3::jk2
