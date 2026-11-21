// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/events.h"
#include "common/types.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/formats/window_layout.h"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// What the game's screens built from a .wdf layout share: the layout, drawn
// over the main menu (opening.wdf) as the game draws its panels; finding
// widgets by id and buttons under the mouse; buttons that can't be used, or
// are under the pointer; and text in a text box, in its font and alignment.
// Each screen adds its controls (main_menu.cpp, setup_screen.cpp,
// empire_screen.cpp).

namespace reluva::botf {

/** What a screen's controls sound like when used, for the engine to play. */
enum class UiSound {
    kClick,      // a button on most screens
    kSetupClick, // a control on the game setup and empire selection screens
    kGameChosen, // a saved game's slot chosen, loading
    kSlotChosen, // a slot chosen, saving
    // An empire chosen on the empire selection screen, in race.rst's order.
    kCardassiansChosen,
    kFederationChosen,
    kFerengiChosen,
    kKlingonsChosen,
    kRomulansChosen,
    kVoiceVolume, // the options' volumes set: each plays a sample
    kVideoVolume,
    kEffectsVolume,
};

/** A screen drawn from a .wdf layout, over the main menu or as it: what every such screen shares. */
class LayoutScreen {
public:
    /** The time, in milliseconds, for a pulsing button; the engine's, each frame. */
    void setTime(uint64_t millis) { _now = millis; }
    /** Whether a button is pulsing: the screen changes as time passes. */
    bool pulsing() const { return _pulsing != 0; }
    /** Whether a held button is acting again and again (the timers' arrows). */
    bool repeating() const { return _repeating != 0; }
    /** Where the screen's sounds go (the engine's); none by default. */
    void setSoundHandler(std::function<void(UiSound)> handler) { _sound = std::move(handler); }

    /** A widget of the screen's layout, and where it is on the screen. */
    struct Placed {
        const Widget *widget = nullptr;
        int x = 0;
        int y = 0;
        int panel = -1; // the panel it's in (addPanel), or -1 for the screen's layout
        bool contains(int px, int py) const { return px >= x && py >= y && px < x + widget->w && py < y + widget->h; }
    };

protected:
    LayoutScreen(Assets &assets, const Lexicon &lexicon) : _assets(assets), _lexicon(lexicon) {}

    // Reads the screen's layout, and opening.wdf to draw under it unless
    // `overMenu` is false (the main menu itself), and checks the layout has
    // the widgets the screen needs.
    Error loadLayouts(const char *file, std::initializer_list<int32_t> needed, bool overMenu = true);
    // Another layout, a panel, drawn in the place of the layout's widget
    // `place` (an empty picture the panel's size), shown or not: its
    // number for showPanel(). Its widgets are found by id as the layout's.
    Error addPanel(const char *file, int32_t place, std::initializer_list<int32_t> needed, int &panel);
    void showPanel(int panel, bool shown);
    bool panelShown(int panel) const;
    const Placed *find(int32_t id) const;
    // The button under the mouse, among those not hidden or disabled; or 0.
    int32_t buttonAt(int x, int y) const;
    // Follows the pointer, for the button under it.
    void trackPointer(const Event &event);
    // The button that pulses, or 0: from its "selected" picture to nearly its
    // normal one and back, from now (the button under the pointer, on the
    // main menu and the empire selection screen's names).
    void setPulsing(int32_t id);
    // The main menu, dimmed, then the layout: backgrounds and buttons (the
    // pressed one with its "selected" picture, as are those `selected`
    // names, and the pulsing one between its two; the `_disabled` ones
    // dimmed), parents before children, leaving out the
    // `_hidden` widgets; then the panels shown, the same way.
    void drawLayouts(Surface &screen, const std::set<int32_t> &selected = {}) const;
    // Text in a text box, in its font and alignment, in the middle of its
    // height; or, with `lineGap`, wrapped to its width from its top, the
    // lines `lineGap` pixels further apart than the font's.
    void drawText(Surface &screen, int32_t textBox, const std::string &text, Color color, int lineGap = -1) const;
    // The same for a text box of another layout, its corner at (boxX, boxY).
    void drawTextAt(Surface &screen, const Widget &textBox, int boxX, int boxY, const std::string &text, Color color,
                    int lineGap = -1) const;
    void drawPicture(Surface &screen, int32_t widget, const std::string &file) const;
    // Adds typed UTF-8 to `text`, a text box's: only characters its font has
    // (and so Windows-1252 can hold), as long as it stays within
    // `maxCharacters` and the box's width.
    void typeInto(std::string &text, int32_t textBox, const char *utf8, size_t maxCharacters) const;
    // Takes the last character (UTF-8) off `text`: Backspace.
    static void eraseLast(std::string &text);
    // A typing cursor after `text` in a text box: a white block 8 pixels
    // wide, as high as the font's glyphs (the game's, on Save Game).
    void drawCursor(Surface &screen, int32_t textBox, const std::string &text) const;
    // The colour a text box's font names, or black.
    Color fontColor(int32_t textBox) const;
    // The lexicon's entry, or `english` when there's no lexicon.
    std::string word(int entry, const char *english) const;
    void sound(UiSound s) const {
        if (_sound)
            _sound(s);
    }
    // A held button that acts again while it's held: from now, after a
    // pause, then at a steady rate (the timers' arrows).
    void startRepeat(int32_t button);
    // How many times the held button is due to act again by setTime's time.
    int repeatsDue();

    Assets &_assets;
    const Lexicon &_lexicon;
    WindowLayout _menu;
    WindowLayout _layout;
    std::map<int32_t, Placed> _widgets;
    /** A panel: its layout, where it's drawn, and whether it's shown. */
    struct Panel {
        WindowLayout layout;
        int x = 0; // its place: where it's drawn, and what it's cut to
        int y = 0;
        int w = 0;
        int h = 0;
        bool shown = false;
    };
    std::vector<std::unique_ptr<Panel>> _panels; // owned apart, as _widgets points into them
    std::set<int32_t> _hidden;                   // widgets the game doesn't show on this screen
    std::set<int32_t> _disabled;                 // buttons that can't be used just now
    int32_t _pressed = 0;                        // the button the mouse went down on, or 0
    int32_t _pointed = 0;                        // the button under the pointer, or 0
    int32_t _pulsing = 0;                        // the button pulsing (setPulsing), or 0
    uint64_t _pulseStart = 0;                    // when it began, in setTime's milliseconds
    uint64_t _now = 0;
    std::set<int32_t> _litWhileHeld; // if any, the only buttons lit while held
    int32_t _repeating = 0;          // the held button acting again while held, or 0
    uint64_t _repeatNext = 0;        // when it acts next, in setTime's milliseconds
    std::function<void(UiSound)> _sound;
};

/** ASCII letters in capitals; the rest as it is. */
std::string upperAscii(std::string s);

} // namespace reluva::botf
