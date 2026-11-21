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

} // namespace reluva::stawayteam
