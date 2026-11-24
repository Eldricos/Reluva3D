// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/label_map.h"
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
    /** Draws it, Previous Menu saying `previous`. */
    void draw(Surface &screen, ShellFont &font, const std::string &previous, bool lit, bool pressed) const;

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
 * The values are kept while the program runs. See
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
    /** A control and its values. */
    struct Control {
        enum Kind { kSwitch, kSlider, kList } kind;
        int x;
        int y;
        // A switch's or list's values, as label.map keys in the page's
        // section; a value that isn't one ("8") is shown as it is.
        std::vector<const char *> values;
        int initial; // a switch's or list's value; a slider's line, in pixels
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
    /** A slider's line after a click at `x` across it (from its left): − shortens it a step, + lengthens it. */
    static int sliderAfterClick(int line, int x);

    /**
     * Reads its pictures and text, the values at their first.
     * @return a failure if one isn't there
     */
    Error load(const GameFiles &files);
    /** Shows a page. */
    void open(SettingsPage page);
    /** A control's value now. */
    int value(SettingsPage page, size_t control) const { return _values[size_t(page)][control]; }

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

    image::Image _background;
    NavigationBar _bar;
    std::array<image::Image, 3> _switch; // at rest, under the pointer, pressed
    std::array<image::Image, 3> _list;
    image::Image _slider;
    LabelMap _text;
    ShellFont _font;
    std::array<std::vector<int>, 3> _values; // each page's controls'
    SettingsPage _page = SettingsPage::kGraphics;
    int _hover = -1;
    int _pressed = -1;
};

} // namespace reluva::storm3d
