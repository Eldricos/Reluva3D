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

// The game's screen: 640x480, RGBA32.
constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

// Fills (x, y, w, h), clipped to the surface, opaque.
void fillRect(Surface &s, int x, int y, int w, int h, Color c);

// A font of the game's: frame n of its animation is character n
// (Windows-1252), drawn as its glyph image in its cell.
class BitmapFont {
public:
    // Packed: each glyph's ink a pixel after the last's (the ticker), not
    // by the glyphs' cells.
    BitmapFont(const GameData &data, const ResiFont &font, bool packed = false);
    int lineHeight() const { return _lineHeight; }
    int width(std::string_view line) const;
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

// The game's fonts, by their number in dialogInfo.res.
class Fonts {
public:
    Fonts(const GameData &data, const Resi &resi);
    const BitmapFont *get(int id) const; // nullptr if there's no such font

private:
    std::map<int, std::unique_ptr<BitmapFont>> _fonts;
};

struct UiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// Typed text (UTF-8) as the fonts' Windows-1252: ASCII and Latin-1 kept.
std::string typedText(std::string_view utf8);

// Splits a dialog's text at its '|'s (and a text file's at its line ends).
std::vector<std::string> splitLines(std::string_view text);

// Draws text in a rectangle (x, y, w, h): centred across it or from its
// left, centred down it or from its top.
void drawText(Surface &s, const BitmapFont &font, std::string_view text, int x, int y, int w, int h, bool centreX,
              bool centreY);

// A dialog of dialogInfo.res on the screen, with what the game changes in
// it: which controls show, their text, which are disabled or chosen.
class DialogView {
public:
    DialogView(const GameData &data, const Fonts &fonts, const ResiDialog &dialog, int x = 0, int y = 0);

    const ResiDialog &info() const { return _dialog; }
    void setVisible(int id, bool visible);
    void setGroupVisible(int group, bool visible); // every control of that group
    bool visible(int id) const;
    void setEnabled(int id, bool enabled);
    void setText(int id, const std::string &text); // a Text's, a TextButton's or an Edit's
    // By its place in the dialog, for controls without an id of their own (9999).
    void setTextAt(size_t index, std::string text);
    void setVisibleAt(size_t index, bool visible);
    std::string text(int id) const;
    // A button drawn in its pressed frame however the mouse is (a choice made).
    void setChosen(int id, bool chosen);
    // A slider's place, 0-100; a scroll knob's, 0-100.
    void setValue(int id, int value);
    int value(int id) const;
    // The first line of a Text shown (for long texts in a box).
    void setScroll(int id, int line);
    int scroll(int id) const;
    int lineCount(int id) const; // of a Text's text wrapped to its box
    int visibleLines(int id) const;
    // The Edit that takes typing, or -1.
    void focus(int id);
    // Where a control is on the screen (empty if the dialog has none of that
    // id): for tools that click the game's own buttons.
    UiRect rect(int id) const;

    void draw(Surface &s) const { drawBackground(s), drawControls(s); }
    void drawBackground(Surface &s) const;
    void drawControls(Surface &s) const;
    // What a mouse or key event did: the id of a button clicked or a
    // slider moved, or -1.
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
