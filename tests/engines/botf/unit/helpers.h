// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What Birth of the Federation's unit tests build their files with: bytes
// put little-endian, files written, the parts of a .wdf layout and of
// lexicon.dic, and an archive in memory.

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::botf::test {

using Bytes = std::vector<uint8_t>;

/** Appends a value, little-endian. */
///@{
void put16(Bytes &b, uint32_t v);
void put32(Bytes &b, uint32_t v);
///@}
/** Writes a value, little-endian, at `at`, which must already be inside `b`. */
void set32(Bytes &b, size_t at, uint32_t v);
/** Writes the bytes to a file, making its folders. */
void writeFile(const std::filesystem::path &p, const Bytes &b);

/** Appends a 13-byte file name, as layouts and menus hold them: cut, or padded with zeros. */
void putName(Bytes &b, const std::string &name);
/** Appends a layout widget's common part, its type first. */
void putWidget(Bytes &b, uint32_t type, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               const std::string &bg);
/** Appends a window's widget count and its counts by type (1-15); types not in `byType` have none. */
void putWindowCounts(Bytes &b, const std::vector<std::pair<uint32_t, uint32_t>> &byType);
/** Appends a text font: its colour (stored red, blue, green), flags, "own", the .fnt. */
void putFont(Bytes &b, uint8_t r, uint8_t g, uint8_t bl, const std::string &file);

/**
 * A .fnt as docs/engines/botf/formats/fnt.md describes: the header (line
 * height, space advance, 120 advances) and a 16-bit TGA sheet of framed
 * cells, in rows of 10, 26, 26, 32 and 26. Glyph i is a cell of width
 * 2 + i % 4 filled with colour 0x0400 + i, except one pixel left as
 * background; the first cell of rows 2-5 is a pixel wider than its advance
 * says, as in the game.
 * @param rowsToDraw How many rows, from the top, have their glyphs filled.
 *        Below 5, the last row's cells are also split by a frame-coloured
 *        column.
 */
Bytes makeFont(int lineHeight, int rowsToDraw = 5);

/**
 * An .ani animation of `w` by `h` frames, each filled with one 16-bit
 * pixel, laid two across; with `alpha`, its TGA declares an alpha bit.
 */
Bytes makeAnimation(int w, int h, const std::vector<uint16_t> &frames, bool alpha = false);

/** lexicon.dic holding the words, numbered from 0. */
Bytes makeLexicon(const std::vector<std::string> &words);

/**
 * An archive as alt.res and the saved games are: the header named `name`,
 * each file's data as LZSS literals only (so read the same whatever the
 * window holds), and the directory; the files named in `stored` are kept
 * as they are, not LZSS (as a saved game's result.lst, in chunks).
 */
Bytes makeLzssArchive(const std::string &name, const std::vector<std::pair<std::string, Bytes>> &files,
                      const std::set<std::string> &stored = {});

/** `raw` as LZSS that holds only literals, so it reads the same whatever the window holds. */
Bytes lzssLiterals(const Bytes &raw);

/** A saved game holding only a gameInfo with `description`, `turn` and the player's `empire`. */
Bytes makeSave(const std::string &name, const std::string &description, uint32_t turn, uint8_t empire = 1);

/** Files in memory, by their exact names (not matched ignoring case), for the screens. */
struct MemoryArchive : Archive {
    std::map<std::string, Bytes> files;
    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override { return files.count(std::string(name)) > 0; }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return "memory"; }
};

} // namespace reluva::botf::test
