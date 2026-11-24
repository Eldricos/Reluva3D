// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/panel_movie.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::storm3d {

/** What the player chose on the main menu. */
enum class MenuChoice { kNone, kSinglePlayer, kMultiplayer, kOptions, kCredits, kQuit, kReplayIntro };

/**
 * The front end's main menu, as the game draws it at 640 x 480: its
 * background (`bitmaps/main/mainbk.bmp`), four panels each playing a
 * movie, and six labels (`bitmaps/ms/`), each drawn in one of three
 * looks: at rest, under the pointer, pressed. A panel and its label are
 * one button; Quit and Replay Intro are labels alone. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class MainMenu {
public:
    /** A button: its label, and its panel if it has one. */
    struct Button {
        MenuChoice choice;
        const char *label; // bitmaps/ms/<label>1.bmp to 3
        int labelX;
        int labelY;
        const char *movie; // animations/<movie>01.bik at rest, 02 under the pointer; null: none
        int panelX;
        int panelY;
    };
    /** The buttons, in the order they're drawn. */
    static const std::vector<Button> &buttons();
    /**
     * The button at a point of the screen, by its panel's and label's
     * rectangles (given their sizes, `sizes[i]` the label's, then the
     * panel's, of button i); -1 if none.
     */
    static int buttonAt(int x, int y, const std::vector<std::array<int, 4>> &sizes);

    /**
     * Reads its pictures and movies, starting the movies at `millis`.
     * @return a failure if the background or a label isn't there; a movie
     *         that isn't leaves its panel showing the background
     */
    Error load(const GameFiles &files, uint64_t millis);

    /**
     * The pointer moved: the button under it lit, its panel's movie
     * changed.
     * @return whether it's come onto a button (the game plays a sound)
     */
    bool pointerAt(int x, int y, uint64_t millis);
    /** The button under (x, y) pressed. */
    void press(int x, int y);
    /** Let go at (x, y): the choice if it's on the button pressed. */
    MenuChoice release(int x, int y);

    /** Draws it as at `millis` into a 640 x 480 RGBA surface. */
    void draw(Surface &screen, uint64_t millis);

private:
    struct Shown {
        std::array<image::Image, 3> looks; // at rest, under the pointer, pressed
        PanelMovie rest;
        PanelMovie lit;
    };
    std::vector<std::array<int, 4>> rectangles() const; // each button's label w, h, then panel w, h

    image::Image _background;
    std::vector<Shown> _shown; // as buttons()
    int _hover = -1;
    int _pressed = -1;
};

} // namespace reluva::storm3d
