// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's interface: its bitmap fonts, and its dialogs (dialogInfo.res)
// drawn and clicked as the original's look in recordings.

#include "common/events.h"
#include "common/types.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/resi.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::clue {

/** The game's screen: 640x480, RGBA32. */
///@{
constexpr int kScreenW = 640;
constexpr int kScreenH = 480;
///@}

/** Fills (x, y, w, h), clipped to the surface, opaque. */
void fillRect(Surface &s, int x, int y, int w, int h, Color c);

/**
 * A font of the game's: frame n of its animation is character n
 * (Windows-1252), drawn as its glyph image in its cell.
 */
class BitmapFont {
public:
    /**
     * Loads the glyphs from `data`, which must outlive the font.
     * @param packed Each glyph's ink a pixel after the last's (the ticker),
     *        not by the glyphs' cells.
     */
    BitmapFont(const GameData &data, const ResiFont &font, bool packed = false);
    /** A line's height, in pixels. */
    int lineHeight() const { return _lineHeight; }
    /** How wide a line (Windows-1252) is, in pixels. */
    int width(std::string_view line) const;
    /** Draws a line (Windows-1252) with its top left at (x, y). */
    void draw(Surface &s, int x, int y, std::string_view line) const;

private:
    struct Glyph {
        const Image *image = nullptr;
        int dx = 0; // where its image goes from the pen
        int advance = 0;
    };
    std::array<Glyph, 256> _glyphs{};
    int _lineHeight = 0;
};

/** The game's fonts, by their number in dialogInfo.res. */
class Fonts {
public:
    /** Loads every font `resi` lists from `data`, which must outlive them. */
    Fonts(const GameData &data, const Resi &resi);
    /** A font by number, or nullptr if there's no such font. */
    const BitmapFont *get(int id) const;

private:
    std::map<int, std::unique_ptr<BitmapFont>> _fonts;
};

/** A rectangle on the screen. */
struct UiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    /** Whether the pixel (px, py) is inside it. */
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

/** Typed text (UTF-8) as the fonts' Windows-1252: ASCII and Latin-1 kept. */
std::string typedText(std::string_view utf8);

/** Splits a dialog's text at its '|'s (and a text file's at its line ends). */
std::vector<std::string> splitLines(std::string_view text);

/**
 * Draws text in a rectangle (x, y, w, h): centred across it or from its
 * left, centred down it or from its top.
 */
void drawText(Surface &s, const BitmapFont &font, std::string_view text, int x, int y, int w, int h, bool centreX,
              bool centreY);

/**
 * A dialog of dialogInfo.res on the screen, with what the game changes in
 * it: which controls show, their text, which are disabled or chosen.
 * Controls are found by id; where several share one, the setters change
 * them all and the getters read the first.
 */
class DialogView {
public:
    /**
     * The dialog with its top left at (x, y); controls of a group start
     * hidden. Everything given must outlive it.
     */
    DialogView(const GameData &data, const Fonts &fonts, const ResiDialog &dialog, int x = 0, int y = 0);

    /** The dialog as dialogInfo.res has it. */
    const ResiDialog &info() const { return _dialog; }
    /** Shows or hides a control. */
    void setVisible(int id, bool visible);
    /** Shows or hides every control of that group. */
    void setGroupVisible(int group, bool visible);
    /** Whether a control shows; false if there's none of that id. */
    bool visible(int id) const;
    /** Enables or disables a control. */
    void setEnabled(int id, bool enabled);
    /** Sets a Text's, a TextButton's or an Edit's text, scrolled back to its top. */
    void setText(int id, const std::string &text);
    /** By its place in the dialog, for controls without an id of their own (9999). */
    ///@{
    void setTextAt(size_t index, std::string text);
    void setVisibleAt(size_t index, bool visible);
    ///@}
    /** A control's text: as set, else dialogInfo.res's. */
    std::string text(int id) const;
    /** A button drawn in its pressed frame however the mouse is (a choice made). */
    void setChosen(int id, bool chosen);
    /** A slider's place, 0-100; a scroll knob's, 0-100. */
    ///@{
    void setValue(int id, int value);
    int value(int id) const;
    ///@}
    /** The first line of a Text shown (for long texts in a box), kept within its lines. */
    ///@{
    void setScroll(int id, int line);
    int scroll(int id) const;
    ///@}
    /** How many lines a Text's text takes wrapped to its box, and how many the box shows. */
    ///@{
    int lineCount(int id) const;
    int visibleLines(int id) const;
    ///@}
    /** The Edit that takes typing, or -1. */
    void focus(int id);
    /**
     * Where a control is on the screen (empty if the dialog has none of that
     * id): for tools that click the game's own buttons.
     */
    UiRect rect(int id) const;

    /** Draws the dialog: its background, then its controls. */
    void draw(Surface &s) const { drawBackground(s), drawControls(s); }
    /** Draws only its background, or only its controls. */
    ///@{
    void drawBackground(Surface &s) const;
    void drawControls(Surface &s) const;
    ///@}
    /**
     * What a mouse or key event did: the id of a button clicked or a
     * slider moved, or -1.
     */
    int handleEvent(const Event &ev);

private:
    struct State {
        bool visible = true;
        bool enabled = true;
        bool chosen = false;
        bool textSet = false;
        std::string text;
        int value = 0;
        int scroll = 0;
    };
    const ResiControl *hit(int mx, int my) const;
    UiRect rectOf(const ResiControl &c) const;
    std::vector<std::string> wrapped(const ResiControl &c) const;

    const GameData &_data;
    const Fonts &_fonts;
    const ResiDialog &_dialog;
    int _x;
    int _y;
    // the dialog's size, for controls reaching to its edges
    int _width = 0;
    int _height = 0;
    std::vector<State> _state; // one per control
    int _hover = -1;
    int _pressed = -1;
    int _focus = -1;
    int _dragging = -1;
};

} // namespace reluva::clue
