// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"

#include <map>
#include <string>
#include <vector>

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

/** A table's string with its parameters' places filled, by their numbers; one not given is left empty. */
std::string fillGameString(const StringTable &table, size_t index, const std::map<int, std::string> &values);

/**
 * Text broken into lines no wider than `width` in a font, at spaces
 * where it can, and at its own line breaks.
 */
std::vector<std::string> wrapGameText(const Font &font, const CharacterSet &characters, const std::string &utf8,
                                      int width);

/** Reads a font of the language's folder (`Language/American/`), by its name (`CASHMED`). */
Error readGameFont(const SearchSet &files, const std::string &folder, const char *name, Font &out);
/** Reads a string table of the language's folder, by its name (`MONTHS`). */
Error readStringTable(const SearchSet &files, const std::string &folder, const char *name,
                      const CharacterSet &characters, StringTable &out);

} // namespace reluva::tpw
