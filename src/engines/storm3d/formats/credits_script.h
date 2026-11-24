// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * The credits' script (`Credits.txt`): pages of lines, each shown for a
 * while, and commands setting how the pages after them look:
 *
 *     @colour=(0.0,0.3,0.6)
 *     @scale=(0.8,0.8,0.8)
 *     @hold=6.0
 *     Star Trek: Armada
 *     @cls
 *
 * `@cls` ends a page. See
 * docs/engines/storm3d/games/armada/formats/credits.md.
 */
struct CreditsScript {
    /** A page, with the settings in force where it ends. */
    struct Page {
        std::vector<std::string> lines; // a blank line kept as an empty one
        std::array<float, 3> colour{1, 1, 1};
        std::array<float, 3> scale{1, 1, 1};
        float hold = 0; // seconds
    };
    std::vector<Page> pages;

    /** Reads the script; a command not understood is left out, as are pages with no lines. */
    static CreditsScript parse(std::string_view text);
};

} // namespace reluva::storm3d
