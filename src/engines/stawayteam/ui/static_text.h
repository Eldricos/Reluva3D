// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/ui/font.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/** A text Rules.txt puts on a screen (a `CStaticTextNernie`): its place, font, alignment and width. */
struct StaticText {
    int x = 0;
    int y = 0;
    std::string text;
    Font font;
    Font::Align align = Font::Align::kLeft;
    int wrap = 0; // 0: one line

    /** Draws it, its screen's top left at (originX, originY). */
    void draw(Surface &screen, int originX, int originY) const;
};

/** The `CStaticTextNernie`s of a definition's `Nernie` array (nullptr: none), their fonts from `data`. */
std::vector<StaticText> readStaticTexts(const Archive &data, const TextObject *definition);

/**
 * A `CRandomNumberNernie`: rows of random numbers that roll now and then
 * (0x54ca10, 0x54ca80).
 */
class RandomNumbers {
public:
    /** Its place, font and timing from `nernie`; its numbers from `seed`. */
    RandomNumbers(const Archive &data, const TextObject &nernie, uint32_t seed);

    /** `seconds` on: idle a while, then the numbers changed so many times. */
    void update(float seconds);
    /** Draws it, its screen's top left at (originX, originY). */
    void draw(Surface &screen, int originX, int originY) const;
    /** The numbers as shown, a row's after another. */
    const std::vector<std::string> &numbers() const { return _numbers; }

private:
    void roll();

    int _x = 0;
    int _y = 0;
    Font _font;
    int _digits = 0;       // Width of Numbers
    int _columns = 0;      // Column Width: numbers in a row
    int _rows = 0;         // Column Height
    int _columnSpace = 0;  // Space Between Columns, pixels
    int _rowSpace = 0;     // Space Between Rows
    float _idle = 0.0f;    // Idle Time, seconds
    float _between = 0.0f; // Time Between Changes
    int _changes = 0;      // Number of Changes
    bool _idling = true;
    float _time = 0.0f;
    int _changed = 0;
    uint32_t _random;
    std::vector<std::string> _numbers;
};

/** The `CRandomNumberNernie`s of a definition's `Nernie` array, each from its own `seed` on. */
std::vector<RandomNumbers> readRandomNumbers(const Archive &data, const TextObject *definition, uint32_t seed);

} // namespace reluva::stawayteam
