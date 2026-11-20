// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

// The .mnu menus in stbof.res (docs/engines/botf/formats/wdf.md#menus): the items of
// a pop-up menu that a .wdf menu widget names, each a number the game acts on,
// a picture and a label.

namespace reluva::botf {

/** One item of a pop-up menu. */
struct MenuItem {
    int32_t id = 0;
    std::string picture; // a .tga, or empty for none
    std::string label;   // UTF-8
};

/** A .mnu file: a pop-up menu's items, in order. */
class MenuFile {
public:
    /** Reads the file, replacing the items; fails if they don't fit in it. */
    Error load(const std::vector<uint8_t> &data);
    /** The items, in the menu's order. */
    const std::vector<MenuItem> &items() const { return _items; }

private:
    std::vector<MenuItem> _items;
};

} // namespace reluva::botf
