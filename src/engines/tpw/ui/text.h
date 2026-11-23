// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"

#include <string>

namespace reluva::tpw {

/**
 * Draws text in one of the game's fonts onto an RGBA32 surface, clipped:
 * each character by its code in the language's character set, as the
 * game's strings hold it; one the set or the font hasn't is left out.
 * @param y The line's top.
 * @return The pen's x after the text.
 */
int drawGameText(Surface &s, const Font &font, const CharacterSet &characters, int x, int y, const std::string &utf8,
                 Color c);

/** How far drawGameText moves the pen for the text. */
int gameTextWidth(const Font &font, const CharacterSet &characters, const std::string &utf8);

} // namespace reluva::tpw
