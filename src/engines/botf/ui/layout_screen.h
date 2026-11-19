// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/formats/window_layout.h"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <string>

// What the game's screens built from a .wdf layout share: the layout, drawn
// over the main menu (opening.wdf) as the game draws its panels; finding
// widgets by id and buttons under the mouse; buttons that can't be used, or
// are under the pointer; and text in a text box, in its font and alignment.
// Each screen adds its controls (main_menu.cpp, setup_screen.cpp,
// empire_screen.cpp).

namespace reluva::botf {

/** A screen drawn from a .wdf layout, over the main menu or as it: what every such screen shares. */
class LayoutScreen {
public:
    /** A widget of the screen's layout, and where it is on the screen. */
    struct Placed {
        const Widget *widget = nullptr;
        int x = 0;
        int y = 0;
        bool contains(int px, int py) const { return px >= x && py >= y && px < x + widget->w && py < y + widget->h; }
    };

protected:
    LayoutScreen(Assets &assets, const Lexicon &lexicon) : _assets(assets), _lexicon(lexicon) {}

    // Reads the screen's layout, and opening.wdf to draw under it unless
    // `overMenu` is false (the main menu itself), and checks the layout has
    // the widgets the screen needs.
    Error loadLayouts(const char *file, std::initializer_list<int32_t> needed, bool overMenu = true);
    const Placed *find(int32_t id) const;
    // The button under the mouse, among those not hidden or disabled; or 0.
    int32_t buttonAt(int x, int y) const;
    // Follows the pointer, for the button under it (with `_highlightPointed`).
    void trackPointer(const Event &event);
    // The main menu, dimmed, then the layout: backgrounds and buttons (the
    // pressed one with its "selected" picture, as are those `selected`
    // names, and the one under the pointer with `_highlightPointed`; the
    // `_disabled` ones dimmed), parents before children, leaving out the
    // `_hidden` widgets.
    void drawLayouts(Surface &screen, const std::set<int32_t> &selected = {}) const;
    // Text in a text box, in its font and alignment, in the middle of its
    // height; or, with `lineGap`, wrapped to its width from its top, the
    // lines `lineGap` pixels further apart than the font's.
    void drawText(Surface &screen, int32_t textBox, const std::string &text, Color color, int lineGap = -1) const;
    void drawPicture(Surface &screen, int32_t widget, const std::string &file) const;
    // The colour a text box's font names, or black.
    Color fontColor(int32_t textBox) const;
    // The lexicon's entry, or `english` when there's no lexicon.
    std::string word(int entry, const char *english) const;

    Assets &_assets;
    const Lexicon &_lexicon;
    WindowLayout _menu;
    WindowLayout _layout;
    std::map<int32_t, Placed> _widgets;
    std::set<int32_t> _hidden;      // widgets the game doesn't show on this screen
    std::set<int32_t> _disabled;    // buttons that can't be used just now
    int32_t _pressed = 0;           // the button the mouse went down on, or 0
    int32_t _pointed = 0;           // the button under the pointer, or 0
    bool _highlightPointed = false; // the button under the pointer shows its "selected" picture
};

/** ASCII letters in capitals; the rest as it is. */
std::string upperAscii(std::string s);

} // namespace reluva::botf
