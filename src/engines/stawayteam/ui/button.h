// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/ui/font.h"
#include "image/image.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/** A picture of `Cache/Interface/Screens/<name>.frm16`; empty if there's none. */
image::Image screenPicture(const Archive &data, const std::string &name);

/**
 * A voice-over by its name, as scripts name them ("01 Mission Begin1"):
 * `<name>.ogg` under `Levels/Voice Overs` (inferred); nullptr if there's
 * none.
 */
std::shared_ptr<audio::Sound> voiceOver(const Archive &data, const std::string &name);

/**
 * A sound as Rules.txt and the program name it ("Interface/Main Screen
 * Button.wav"): the `.ogg` of that name in `Sounds/`, or of its last part
 * there ("UI/Button Action.wav"); nullptr if there's none.
 */
std::shared_ptr<audio::Sound> gameSound(const Archive &data, const std::string &name);

/**
 * A button of the game's menus: a picture for each state (`<name> Default`,
 * `Hover` and `Press`), and a label drawn over it in a font. An `&` in the
 * label marks its shortcut, the key after it.
 */
class Button {
public:
    /**
     * The button whose pictures are `<folder>/<name> Default.frm16`... of
     * the screens', its top left at (x, y) on its menu.
     */
    Button(const Archive &data, const std::string &folder, const std::string &name, int x, int y);
    /** A button of one picture, the same in every state (the escape and credits windows'). */
    Button(image::Image picture, int x, int y);

    /**
     * Its label (with its `&`), drawn in `font` (which must outlive it) at
     * `textY` below its top, centred across it (less `textX`), as the
     * game's menus set theirs (0x5cd0d1).
     */
    void setLabel(const std::string &label, const Font *font, int textX, int textY);
    /** Lines its label up on its right instead, `textX` in from it (the label's alignment 2, 0x5cd0b7). */
    void alignLabelRight() { _rightAligned = true; }
    /** The sound played when it's clicked. */
    void setSound(std::shared_ptr<audio::Sound> sound) { _sound = std::move(sound); }

    /** Its label without the `&`. */
    const std::string &label() const { return _label; }
    /** Its shortcut, in lower case; 0 for none. */
    char shortcut() const;

    int x() const { return _x; }
    int y() const { return _y; }
    /** Its size: its default picture's. */
    int width() const { return _default.w; }
    int height() const { return _default.h; }
    /** Whether (x, y), on its menu, is on it. */
    bool contains(int x, int y) const;

    /** Draws it, its menu's top left at (originX, originY). */
    void draw(Surface &screen, int originX, int originY, bool hover, bool pressed) const;

    /** Whether it's checked: then it shows its Press picture, under the mouse or not (0x4e8140). */
    bool checked() const { return _checked; }
    /** A disabled button shows its Disable picture, if it has one, and can't be chosen (0x4e8140). */
    bool enabled() const { return _enabled; }
    void setEnabled(bool enabled) { _enabled = enabled; }
    void setChecked(bool checked) { _checked = checked; }

    const std::shared_ptr<audio::Sound> &sound() const { return _sound; }

private:
    int _x;
    int _y;
    image::Image _default;
    image::Image _hover;
    image::Image _press;
    image::Image _disable;
    std::string _label;
    size_t _shortcut = std::string::npos; // the index in _label
    const Font *_font = nullptr;
    int _textX = 0;
    int _textY = 0;
    std::shared_ptr<audio::Sound> _sound;
    bool _checked = false;
    bool _enabled = true;
    bool _rightAligned = false;
};

/**
 * Buttons of a menu, as the mouse and keys work them: the one under the
 * mouse is lit, the one it's held down on pressed, and a click (let go on
 * the button it went down on) or a button's shortcut chooses it.
 */
class ButtonGroup {
public:
    /** The buttons' menu's top left on the screen. */
    void setOrigin(int x, int y) {
        _originX = x;
        _originY = y;
    }
    void add(Button button) { _buttons.push_back(std::move(button)); }
    const std::vector<Button> &buttons() const { return _buttons; }
    Button &button(size_t i) { return _buttons[i]; }

    /**
     * What `ev` does to them: the number of the button it chooses, its
     * sound played through `mixer` (nullptr: none), or -1.
     */
    int handleEvent(const Event &ev, audio::Mixer *mixer);
    /**
     * Presses button `i` as its menu's Enter or Esc does: its sound
     * played through `mixer` (nullptr: none). Returns `i`, or -1 if it's
     * disabled.
     */
    int press(size_t i, audio::Mixer *mixer);
    void draw(Surface &screen) const;

private:
    int buttonAt(int x, int y) const;

    std::vector<Button> _buttons;
    int _originX = 0;
    int _originY = 0;
    int _hover = -1;
    int _pressed = -1;
};

} // namespace reluva::stawayteam
