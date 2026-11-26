// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/settings_screen.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::storm3d {

/** What the player chose on the debriefing. */
enum class DebriefingChoice { kNone, kContinue, kQuit };

/**
 * The debriefing a skirmish ends on, as the game draws it at 640 x 480
 * over the credits' background (`bitmaps/credit/creditsBG.bmp`): each
 * player's dilithium collected and spent, and ships and stations
 * created, lost and destroyed, a row each in their colour; Continue and
 * Quit To Windows along the bottom. See
 * docs/engines/storm3d/games/armada/systems/play.md.
 */
class DebriefingScreen {
public:
    /** A player's row: who and which side, their colour, and their eight numbers in the columns' order. */
    struct Row {
        std::string name;
        std::string side;
        Color colour{255, 255, 255};
        std::array<int, 8> numbers{};
    };
    /** The choice under a point: kNone if none. */
    static DebriefingChoice choiceAt(int x, int y);

    /**
     * Reads its pictures and text.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files);
    void setRows(const std::vector<Row> &rows) { _rows = rows; }

    /** @return whether the pointer came onto a button (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y) { _pressed = choiceAt(x, y); }
    /** Let go at (x, y): the choice if it's on the button pressed. */
    DebriefingChoice release(int x, int y);

    void draw(Surface &screen);

private:
    image::Image _background;
    NavigationBar _bar;
    LabelMap _text;
    ShellFont _font;
    std::vector<Row> _rows;
    DebriefingChoice _hover = DebriefingChoice::kNone;
    DebriefingChoice _pressed = DebriefingChoice::kNone;
};

} // namespace reluva::storm3d
