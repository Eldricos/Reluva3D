// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/objectives.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/** An objective as the lists show it: its state and its words. */
struct ObjectiveLine {
    ObjectiveStatus status = ObjectiveStatus::kIncomplete;
    std::string text;
    bool primary = false; // required to end the mission
};

/**
 * Draws a mission's objectives as the game lists them (0x47f990): "Primary
 * Objectives", then each primary one's state's icon with its words wrapped
 * beside it, then the same for "Secondary Objectives".
 */
class ObjectiveList {
public:
    /** `data` must outlive it. */
    explicit ObjectiveList(const Archive &data);

    /** Draws `lines` from (x, y), `width` across, `spacing` between them, at `brightness` (0-1). */
    void draw(Surface &screen, int x, int y, int width, int spacing, const std::vector<ObjectiveLine> &lines,
              float brightness = 1.0f) const;

private:
    Font _heading;
    Font _text;
    std::array<image::Image, 4> _icons; // by ObjectiveStatus
};

/**
 * The objectives window, which the objectives button opens (Rules.txt's
 * `Objectives Menu`, 0x51ad60): the list on its background, and RESUME.
 */
class ObjectivesWindow {
public:
    /** On a screen `screenW` x `screenH`; `data` must outlive it. */
    ObjectivesWindow(const Archive &data, const TextObject &rules, int screenW, int screenH);

    /** What `ev` does to it: true once it's closed. */
    bool handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, const std::vector<ObjectiveLine> &lines) const;

private:
    image::Image _background;
    ObjectiveList _list;
    Font _font;
    ButtonGroup _buttons;
    int _listX = 0;
    int _listY = 0;
    int _listW = 0;
    int _spacing = 0;
    // Its top left: in the middle of the screen (inferred).
    int _x;
    int _y;
};

/**
 * The window a mission ends with (Rules.txt's `End Game Menu`, 0x50cfd0):
 * its message and the objectives; CONTINUE when it's completed, LOAD GAME,
 * RESTART and END GAME when it's failed.
 */
class EndGameWindow {
public:
    /** What the player chose. */
    enum class Choice { kNone, kContinue, kLoadGame, kRestart, kEndGame };

    /** For a mission `completed` or failed, saying `message`; `data` must outlive it. */
    EndGameWindow(const Archive &data, const TextObject &rules, bool completed, std::string message, int screenW,
                  int screenH);

    Choice handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, const std::vector<ObjectiveLine> &lines) const;

private:
    image::Image _background;
    ObjectiveList _list;
    Font _font;
    std::map<std::string, Font> _buttonFonts;
    ButtonGroup _buttons;
    std::vector<Choice> _choices; // each button's
    std::string _message;
    bool _completed;
    bool _endPressed = false; // END GAME once: a second time leaves
    Font _hintFont;           // the line under the buttons: Trek Small, then Trek Small Red
    Font _warningFont;
    int _textX = 0;
    int _textY = 0;
    int _textW = 0;
    int _spacing = 0;
    int _x;
    int _y;
};

} // namespace reluva::stawayteam
