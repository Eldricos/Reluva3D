// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * `Input.map`: the game's actions and the keys and mouse buttons that
 * trigger them, as named blocks of conditions. See
 * docs/engines/storm3d/games/armada/formats/input-map.md.
 */
struct InputMap {
    /** A control that must be down (`+`) or must not be (`-`) for an action. */
    struct Condition {
        bool down = true;
        std::string device;  // "keyboard" or "mouse"
        std::string control; // the device's name for it: "A", "Control", "LeftBtnDblClk", "HorizPos"
    };

    /** An action and what triggers it. */
    struct Binding {
        std::string action; // the block's name: "attack", "left_click"
        std::vector<Condition> conditions;
    };

    std::vector<Binding> bindings;     // in the file's order
    std::vector<std::string> problems; // lines of no known form, with their line numbers

    /**
     * Reads the file's text.
     * @return a failure if a block isn't closed
     */
    Error parse(std::string_view text);

    /** The binding for an action, matched exactly, or nullptr. */
    const Binding *find(std::string_view action) const;
};

} // namespace reluva::storm3d
