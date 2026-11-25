// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
#include "engines/storm3d/formats/preferences.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/shell/shell_draw.h"
#include "image/image.h"

#include <array>
#include <vector>

namespace reluva::storm3d {

/** The settings screens the Options screen opens. */
enum class SettingsPage { kGraphics, kSound, kGame };

/**
 * The bar along the bottom of the Options screen and the settings
 * screens: Previous Menu (`bitmaps/multiset/navback`) beside two blank
 * plates (`mpsstart1`, `navmain1`).
 */
class NavigationBar {
public:
    Error load(const GameFiles &files);
    /** Whether a point is on Previous Menu. */
    static bool onPrevious(int x, int y);
    /** Whether a point is on the middle plate, or the right one. */
    static bool onStart(int x, int y);
    static bool onMain(int x, int y);
    /**
     * Draws it, Previous Menu saying `previous`; the other plates say
     * `start` and `main` (the Load Game screen's Load Game and Delete
     * Game), blank if they're empty; the words in `words`.
     */
    void draw(Surface &screen, ShellFont &font, const std::string &previous, bool lit, bool pressed,
              const std::string &start = {}, const std::string &main = {}, Color words = {0, 0, 0}) const;

private:
    image::Image _start;
    image::Image _main;
    ShellButton _previous;
};

/**
 * A settings screen, as the game draws it at 640 x 480 over
 * `bitmaps/credit/creditsBG.bmp`: yellow labels, and under or beside
 * them its controls: switches (`multiset/nav3but`) that turn through
 * their values when clicked, sliders (`holder/scrollbar.bmp`) whose blue
 * line − and + shorten and lengthen, and lists (`wonlogin/wls324pd`).
 * The values are the player's preferences (`ARMADA.PRF`). See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class SettingsScreen {
public:
    /** A label: its key in the page's label.map section, centred in x to x + w, its line's top, its size. */
    struct Label {
        const char *key;
        int x;
        int w;
        int top;
        float pixels;
    };
    /** A switch's value: its words' key in the page's section (shown as it is if it isn't one), and the number kept. */
    struct Choice {
        const char *key;
        double stored;
    };
    /** A control, and where its value is kept in the preferences. */
    struct Control {
        enum Kind { kSwitch, kSlider, kList } kind;
        int x;
        int y;
        Preferences::Line line;
        size_t field;
        std::vector<Choice> choices; // a switch's, in the order clicks turn them
        // A slider's: its range, a click's step, and how long its line is
        // a unit, from 0.
        double min;
        double max;
        double step;
        double pixels;
        bool fixed; // a click doesn't change it
    };
    /** A page: its label.map section, its Previous Menu's key there, its title's place, its labels and controls. */
    struct Layout {
        const char *section;
        const char *previous;
        int titleX;
        std::vector<Label> labels;
        std::vector<Control> controls;
    };
    static const Layout &layoutOf(SettingsPage page);
    /** The control of a page at a point; -1 if none. */
    static int controlAt(SettingsPage page, int x, int y);
    /** A slider's value after a click at `x` across it (from its left): − takes a step off, + adds one. */
    static double sliderAfterClick(const Control &slider, double value, int x);
    /** How long a slider's line is for a value. */
    static int lineOf(const Control &slider, double value);

    /**
     * Reads its pictures and text, its values those of `preferences`,
     * which it changes as the player does.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files, Preferences *preferences);
    /** Shows a page. */
    void open(SettingsPage page);
    /** @return whether the pointer came onto a control (the game plays a sound) */
    bool pointerAt(int x, int y);
    void press(int x, int y);
    /**
     * Let go at (x, y): a control under it changed.
     * @return true for Previous Menu, which leaves the page
     */
    bool release(int x, int y);

    void draw(Surface &screen);

private:
    static constexpr int kPrevious = -2;
    int targetAt(int x, int y) const;
    std::string textOf(const char *key) const;
    std::string shown(const Control &c) const;

    image::Image _background;
    NavigationBar _bar;
    std::array<image::Image, 3> _switch; // at rest, under the pointer, pressed
    std::array<image::Image, 3> _list;
    image::Image _slider;
    LabelMap _text;
    ShellFont _font;
    Preferences *_preferences = nullptr;
    SettingsPage _page = SettingsPage::kGraphics;
    int _hover = -1;
    int _pressed = -1;
};

} // namespace reluva::storm3d
