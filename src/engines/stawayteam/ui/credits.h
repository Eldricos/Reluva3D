// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/widgets.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::stawayteam {

/**
 * The credits rolling up: `Credits.txt`'s lines, centred, each fading in
 * at the bottom and out at the top, over and over
 * (docs/engines/stawayteam/systems/menus.md#the-credits).
 */
class CreditsRoll {
public:
    /** The lines of `text` (Credits.txt), drawn in `font`, which must outlive it. */
    CreditsRoll(std::string_view text, const Font &font);

    /** Starts it from its first line at `now` (milliseconds). */
    void start(uint64_t now) { _start = now; }
    /**
     * Draws it at `now` in the area of `screen` at (x, y), w by h, nothing
     * outside it; once its last line has gone, it starts again.
     */
    void draw(Surface &screen, int x, int y, int w, int h, uint64_t now);

    const std::vector<std::string> &lines() const { return _lines; }

private:
    // Draws line `line` with its top at y, across an area w wide, its
    // colours scaled by `brightness`, 0-1.
    void drawLine(Surface &area, const std::string &line, int y, int w, float brightness) const;

    const Font &_font;
    std::vector<std::string> _lines;
    uint64_t _start = 0;
};

/**
 * The credits window, over the main menu: `Escape Menu Background` with
 * the credits rolling where Rules.txt's `Credits` puts them, and RESUME,
 * which closes it.
 */
class CreditsWindow {
public:
    /**
     * The window on a screen of that size, the credits from `data`'s
     * Credits.txt from `now` on; `data` must outlive it.
     */
    CreditsWindow(const Archive &data, const TextObject &rules, int screenW, int screenH, uint64_t now);

    /** What `ev` does to it: true once it's closed. */
    bool handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, uint64_t now);

private:
    image::Image _background;
    Font _font;
    Font _buttonFont;
    CreditsRoll _roll;
    PictureButton _resume;
    int _x;
    int _y;
    int _rollX = 0;
    int _rollY = 0;
    int _rollW = 0;
    int _rollH = 0;
};

} // namespace reluva::stawayteam
