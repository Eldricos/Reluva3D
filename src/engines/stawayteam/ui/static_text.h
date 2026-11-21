// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/ui/font.h"

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

} // namespace reluva::stawayteam
