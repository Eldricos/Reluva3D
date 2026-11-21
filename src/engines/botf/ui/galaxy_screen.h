// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/botf/formats/data_table.h"
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

/** The galaxy screen: its frame, its map and the empire's panel. */
class GalaxyScreen : LayoutScreen {
public:
    /** Draws with `assets` and `lexicon`, which must outlive it. */
    GalaxyScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    /**
     * Reads the layout and pictures of `empire`'s screen (0 Cardassians to
     * 4 Romulans); fails if the layout lacks a widget the screen needs.
     */
    Error load(int empire);
    /** Shows a saved game as its player sees it (`game.summary.empire`, which load() was given). */
    void open(SavedGame game);
    /** Draws the frame, the map and the empire's panel. */
    void draw(Surface &screen) const;

    /** Whether the sector at column `x` and row `y` is in the player's scanners' range. */
    bool scanned(int x, int y) const { return _scanned.count({x, y}) > 0; }

private:
    void drawMap(Surface &screen) const;
    void drawPanel(Surface &screen) const;
    void drawTaskForcesBar(Surface &screen) const;
    std::string missionWord(int mission) const;
    // A picture at a widget's place: across, in its middle; down, at its top.
    void drawIcon(Surface &screen, int x, int y, const Widget &w, const std::string &file) const;

    SavedGame _game;
    int _empire = 1;
    DataTable _shipClasses; // shiplist.sst, for the task forces' ships' names
    int _infoPanel = -1;
    int _emblemsPanel = -1;
    int _taskForcesPanel = -1;
    int _taskForceRow = -1;                 // drawn once for each task force
    std::string _taskForcesBack;            // the task forces' bar's backing picture
    char _letter = 'h';                     // the empire's letter in its files' names
    std::set<std::pair<int, int>> _scanned; // column, row
};

} // namespace reluva::botf
