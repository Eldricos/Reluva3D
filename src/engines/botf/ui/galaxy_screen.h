// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/formats/animation.h"
#include "engines/botf/formats/data_table.h"
#include "engines/botf/formats/save_game.h"
#include "engines/botf/formats/window_layout.h"
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
    /** How often the strip's quickest animation, a star's, changes frame, in milliseconds. */
    static constexpr uint64_t kFastestFrameMillis = 33;
    /** Draws with `assets` and `lexicon`, which must outlive it. */
    GalaxyScreen(Assets &assets, const Lexicon &lexicon) : LayoutScreen(assets, lexicon) {}
    /**
     * Reads the layout and pictures of `empire`'s screen (0 Cardassians to
     * 4 Romulans); fails if the layout lacks a widget the screen needs.
     */
    Error load(int empire);
    /** Shows a saved game as its player sees it (`game.summary.empire`, which load() was given). */
    void open(SavedGame game);
    /**
     * Acts on a mouse click: the Map button opens or closes the zoom's
     * buttons, Small Map zooms in and Large Map out, and a click on the map
     * selects its sector.
     * @return Whether the screen changed.
     */
    bool handle(const Event &e);
    /** Draws the frame, the map, the empire's panel and the selected sector's strip. */
    void draw(Surface &screen) const;
    /**
     * Selects the sector at column `x` and row `y`: its system or stellar
     * object is shown in the strip under the map, turning.
     */
    void select(int x, int y);
    /** The sector under a point on the screen, selected; false if the point is off the grid. */
    bool click(int x, int y);
    /** The time the strip's animations are drawn at, in milliseconds. */
    using LayoutScreen::setTime;
    /**
     * Zooms the map in, to sectors 61 pixels square round the selected
     * sector, or out, to the whole galaxy's 39.
     */
    void setZoomedIn(bool in);
    /** Whether the map is zoomed in. */
    bool zoomedIn() const { return _zoom == 1; }
    /** The selected sector's column and row, or -1, -1. */
    std::pair<int, int> selected() const { return _selected; }

    /** Whether the sector at column `x` and row `y` is in the player's scanners' range. */
    bool scanned(int x, int y) const { return _scanned.count({x, y}) > 0; }

private:
    void drawMap(Surface &screen) const;
    void drawRanges(Surface &screen, int gx, int gy) const;
    void drawPanel(Surface &screen) const;
    void drawTaskForcesBar(Surface &screen) const;
    std::string missionWord(int mission) const;
    // The map's view on the screen, its first and last pixels, and the
    // grid's corner (for the zoom, round the selected sector); false
    // without a map.
    bool view(int &x0, int &y0, int &x1, int &y1) const;
    bool gridCorner(int &gx, int &gy) const;
    // The map on a surface the view's size, the grid's corner at (gx, gy).
    void drawGrid(Surface &view, int gx, int gy) const;
    void drawStrip(Surface &screen) const;
    void drawSystemStrip(Surface &screen, const SavedSystem &system) const;
    void drawStellarStrip(Surface &screen, const SavedStellarObject &object) const;
    // A frame of an animation at the strip's time, its middle at (cx, cy).
    void drawFrame(Surface &screen, const Animation &a, uint64_t frameMillis, int cx, int cy) const;
    // A widget of a strip's layout, with its corner on the screen.
    const WindowLayout::Placed *stripWidget(const std::vector<WindowLayout::Placed> &strip, int32_t id) const;
    // A picture at a widget's place: across, in its middle; down, at its top.
    void drawIcon(Surface &screen, int x, int y, const Widget &w, const std::string &file) const;

    SavedGame _game;
    int _empire = 1;
    DataTable _shipClasses; // shiplist.sst, for the task forces' ships' names
    int _infoPanel = -1;
    int _emblemsPanel = -1;
    int _taskForcesPanel = -1;
    int _taskForceRow = -1;
    int _zoomPanel = -1;                            // drawn once for each task force
    std::string _taskForcesBack;                    // the task forces' bar's backing picture
    char _letter = 'h';                             // the empire's letter in its files' names
    std::set<std::pair<int, int>> _scanned;         // column, row
    std::vector<std::pair<int, int>> _rangeCentres; // the empire's systems
    std::pair<int, int> _selected{-1, -1};          // the selected sector, or -1, -1
    int _zoom = 0;                                  // 0 out, 1 in
    // The strip's layouts, their widgets placed on the screen: a system's
    // (h_sysmap.wdf) and a stellar object's (h_starmp.wdf).
    WindowLayout _systemLayout;
    WindowLayout _stellarLayout;
    std::vector<WindowLayout::Placed> _systemStrip;
    std::vector<WindowLayout::Placed> _stellarStrip;
    std::vector<Animation> _planets; // the selected system's, innermost first
    Animation _sun;                  // its star, or the stellar object
};

} // namespace reluva::botf
