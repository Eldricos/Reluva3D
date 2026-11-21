// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/ui/layout_screen.h"

#include <set>
#include <utility>
#include <vector>

// The galaxy screen (?maingal.wdf, the empire's letter first), where a
// game is played: the galaxy's map with the sectors the player can see, and
// around it the turn, the empire's figures and the selected system. Laid
// out from its layout and a screenshot of the game's
// (docs/engines/botf/systems/galaxy-screen.md).

namespace reluva::botf {

/** The galaxy screen: its frame and its map. */
class GalaxyScreen : LayoutScreen {
public:
    /** Draws with `assets` and `lexicon`, which must outlive it. */
    GalaxyScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    /**
     * Reads the layout and pictures of `empire`'s screen (0 Cardassians to
     * 4 Romulans); fails if the layout lacks a widget the screen needs.
     */
    Error load(int empire);
    /**
     * Shows a saved game's galaxy as `empire` sees it at `turn` (0 the
     * first), with its task forces.
     */
    void open(SavedGalaxy galaxy, std::vector<SavedTaskForce> taskForces, int empire, int turn);
    /** Draws the frame, then the map. */
    void draw(Surface &screen) const;

    /** Whether the sector at column `x` and row `y` is in the player's scanners' range. */
    bool scanned(int x, int y) const { return _scanned.count({x, y}) > 0; }

private:
    void drawMap(Surface &screen) const;

    SavedGalaxy _galaxy;
    std::vector<SavedTaskForce> _taskForces;
    int _empire = 1;
    int _turn = 0;
    char _letter = 'h';                     // the empire's letter in its files' names
    std::set<std::pair<int, int>> _scanned; // column, row
};

} // namespace reluva::botf
