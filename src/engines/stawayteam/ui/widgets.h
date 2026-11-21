// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/ui/font.h"
#include "image/image.h"

#include <cstdint>
#include <memory>
#include <string>

namespace reluva::stawayteam {

/** `picture` with its colours scaled by `brightness` (0-1), as the game's toolkit dims what it draws. */
image::Image dimmed(const image::Image &picture, float brightness);

/**
 * A button of the game's window toolkit, on one picture (`Button
 * Background`): the picture dim until the mouse is over it, then
 * brightening; the label at its left (docs/engines/stawayteam/systems/menus.md#the-window-toolkit).
 */
class PictureButton {
public:
    /** The button with its label (with its `&`) in `font`, which must outlive it. */
    PictureButton(image::Image picture, const std::string &label, const Font *font);

    void setPosition(int x, int y) {
        _x = x;
        _y = y;
    }
    int x() const { return _x; }
    int y() const { return _y; }
    int width() const { return _picture.w; }
    int height() const { return _picture.h; }
    const std::string &label() const { return _label; }
    /** The sound played when it's clicked (the toolkit's `Button Action`). */
    void setSound(std::shared_ptr<audio::Sound> sound) { _sound = std::move(sound); }

    /**
     * What `ev` does to it, its window's top left at (originX, originY):
     * true when it's clicked or its shortcut pressed, its sound played
     * through `mixer` (nullptr: none).
     */
    bool handleEvent(const Event &ev, int originX, int originY, audio::Mixer *mixer);
    /** Draws it at `now` (milliseconds), its window's top left at (originX, originY). */
    void draw(Surface &screen, int originX, int originY, uint64_t now);

private:
    bool contains(int x, int y) const { return x >= _x && y >= _y && x < _x + width() && y < _y + height(); }

    image::Image _picture;
    std::string _label;
    char _shortcut = 0;
    const Font *_font;
    std::shared_ptr<audio::Sound> _sound;
    int _x = 0;
    int _y = 0;
    bool _hover = false;
    bool _pressed = false;
    float _brightness;
    uint64_t _lastDraw = 0;
};

/**
 * A toggle of the toolkit: a box (its `Default` picture, or `Press` while
 * held down or checked, a cross over it when checked) and its label after
 * it.
 */
class Toggle {
public:
    /** The toggle with its label in `font`, which must outlive it. */
    Toggle(image::Image normal, image::Image press, std::string label, const Font *font);

    void setPosition(int x, int y) {
        _x = x;
        _y = y;
    }
    int x() const { return _x; }
    int y() const { return _y; }
    /** Its size: the box, the label, and 1 between them (0x4e8b72). */
    int width() const;
    int height() const;
    const std::string &label() const { return _label; }
    bool checked() const { return _checked; }
    void setChecked(bool checked) { _checked = checked; }

    /** What `ev` does to it, its window's top left at (originX, originY): true when it's clicked. */
    bool handleEvent(const Event &ev, int originX, int originY);
    void draw(Surface &screen, int originX, int originY) const;

private:
    bool contains(int x, int y) const { return x >= _x && y >= _y && x < _x + width() && y < _y + height(); }

    image::Image _normal;
    image::Image _press;
    std::string _label;
    const Font *_font;
    int _x = 0;
    int _y = 0;
    bool _pressed = false;
    bool _hover = false;
    bool _checked = false;
};

/**
 * A text button of the toolkit (0x5efe20): its label alone, dim until the
 * mouse is over it or it's checked, then brightening, as a picture
 * button's picture (the category tabs, the keyboard screen's actions).
 */
class TextButton {
public:
    /** The button with `label` in `font`, which must outlive it, w by h. */
    TextButton(std::string label, const Font *font, int w, int h);

    void setPosition(int x, int y) {
        _x = x;
        _y = y;
    }
    /** Where its text goes: its left at `x` across, or with `rightAligned` its right; its top `y` down. */
    void setText(int x, int y, bool rightAligned) {
        _textX = x;
        _textY = y;
        _rightAligned = rightAligned;
    }
    int x() const { return _x; }
    int y() const { return _y; }
    int width() const { return _w; }
    int height() const { return _h; }
    const std::string &label() const { return _label; }
    bool checked() const { return _checked; }
    void setChecked(bool checked) { _checked = checked; }

    /** What `ev` does to it, its window's top left at (originX, originY): true when it's clicked. */
    bool handleEvent(const Event &ev, int originX, int originY);
    void draw(Surface &screen, int originX, int originY, uint64_t now);

private:
    bool contains(int x, int y) const { return x >= _x && y >= _y && x < _x + _w && y < _y + _h; }

    std::string _label;
    const Font *_font;
    int _x = 0;
    int _y = 0;
    int _w;
    int _h;
    int _textX = 1;
    int _textY = 1;
    bool _rightAligned = false;
    bool _hover = false;
    bool _pressed = false;
    bool _checked = false;
    float _brightness = 1.0f;
    uint64_t _lastDraw = 0;
};

/**
 * Draws the toolkit's frame round the rectangle (x, y, w, h): `thickness`
 * pixels in, light on the top and left and dark on the bottom and right
 * if `raised`, the other way round if not (0x408000).
 */
void drawBevel(Surface &screen, int x, int y, int w, int h, int thickness, bool raised);

/**
 * A slider of the toolkit (0x4e4b10): a sunken track with a groove, and a
 * raised thumb filled with the toolkit's window texture; its value from 0
 * to one less than its maximum.
 */
class Slider {
public:
    /**
     * The slider, sized for `font` (which must outlive it) and from 0 to
     * `maximum` - 1; `thumbFill` is the toolkit's window texture.
     */
    Slider(const Font *font, int maximum, image::Image thumbFill);

    void setPosition(int x, int y) {
        _x = x;
        _y = y;
    }
    void setWidth(int w) { _w = w; }
    int x() const { return _x; }
    int y() const { return _y; }
    int width() const { return _w; }
    int height() const;
    int value() const { return _value; }
    /** Sets the value, 0 to its maximum (the program can set the maximum; the mouse can't, 0x4e50c0). */
    void setValue(int value);
    /** A disabled slider takes no input (its `+0x5d`, as a muted volume's). */
    void setEnabled(bool enabled) { _enabled = enabled; }
    /** Whether the keys go to it: the last slider clicked (inferred). */
    void setFocused(bool focused) { _focused = focused; }
    bool focused() const { return _focused; }

    /** What `ev` does to it, its window's top left at (originX, originY): true when its value changes. */
    bool handleEvent(const Event &ev, int originX, int originY);
    void draw(Surface &screen, int originX, int originY) const;

private:
    // The value for the mouse at `mx` across the slider (0x4e4ed0).
    int valueAt(int mx) const;

    const Font *_font;
    int _maximum;
    image::Image _thumbFill;
    int _x = 0;
    int _y = 0;
    int _w = 0;
    int _value = 0;
    bool _dragging = false;
    bool _enabled = true;
    bool _focused = false;
};

/**
 * A check box of the toolkit (0x4e6020): a framed box, the font's
 * baseline and 2 wide, raised, or sunken while held down or checked,
 * crossed when checked; its label after it.
 */
class CheckBox {
public:
    /** The check box with its label (with its `&`) in `font`, which must outlive it. */
    CheckBox(const std::string &label, const Font *font);

    void setPosition(int x, int y) {
        _x = x;
        _y = y;
    }
    int x() const { return _x; }
    int y() const { return _y; }
    int width() const;
    int height() const;
    const std::string &label() const { return _label; }
    bool checked() const { return _checked; }
    void setChecked(bool checked) { _checked = checked; }

    /** What `ev` does to it, its window's top left at (originX, originY): true when it's clicked (and changed). */
    bool handleEvent(const Event &ev, int originX, int originY);
    void draw(Surface &screen, int originX, int originY) const;

private:
    int box() const;
    bool contains(int x, int y) const { return x >= _x && y >= _y && x < _x + width() && y < _y + height(); }

    std::string _label;
    char _shortcut = 0;
    const Font *_font;
    int _x = 0;
    int _y = 0;
    bool _pressed = false;
    bool _hover = false;
    bool _checked = false;
};

} // namespace reluva::stawayteam
