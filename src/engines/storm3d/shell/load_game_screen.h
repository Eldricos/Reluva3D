// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/settings_screen.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

namespace reluva::storm3d {

/**
 * The Load Game screen, as the game draws it at 640 x 480 over
 * `bitmaps/misc/LoadBG.bmp`: the list of saved games in its panel, a
 * scroll bar beside it (`multiset/scrollup`, `scrolltb`, `scrolldn`),
 * and the bottom bar saying Load Game and Delete Game. Saved games are
 * made in play, which Reluva can't do yet, so its list is empty. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class LoadGameScreen {
public:
    /**
     * Reads its pictures and text.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files);

    /** @return whether the pointer came onto a button (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y);
    /**
     * Let go at (x, y).
     * @return true for Previous Menu, which leaves the screen
     */
    bool release(int x, int y);

    void draw(Surface &screen);

private:
    enum Target { kNone, kPrevious, kLoad, kDelete };
    static Target targetAt(int x, int y);

    image::Image _background;
    ShellButton _up;
    ShellButton _down;
    image::Image _thumb;
    NavigationBar _bar;
    LabelMap _text;
    ShellFont _font;
    Target _hover = kNone;
    Target _pressed = kNone;
};

} // namespace reluva::storm3d
