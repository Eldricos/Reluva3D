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
#include <string>
#include <string_view>
#include <vector>

namespace reluva::botf::test {

using Bytes = std::vector<uint8_t>;

void put16(Bytes &b, uint32_t v);
void put32(Bytes &b, uint32_t v);
void set32(Bytes &b, size_t at, uint32_t v);
void writeFile(const std::filesystem::path &p, const Bytes &b);

// A 13-byte file name, as layouts and menus hold them.
void putName(Bytes &b, const std::string &name);
// A layout widget's common part, its type first.
void putWidget(Bytes &b, uint32_t type, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               const std::string &bg);
// A window's widget count and its counts by type (1-15).
void putWindowCounts(Bytes &b, const std::vector<std::pair<uint32_t, uint32_t>> &byType);
// A text font: colour (stored red, blue, green), flags, "own", the .fnt.
void putFont(Bytes &b, uint8_t r, uint8_t g, uint8_t bl, const std::string &file);

// A .fnt as docs/formats/botf/fnt.md describes: the header (line height,
// space advance, 120 advances) and a 16-bit TGA sheet of framed cells, in
// rows of 10, 26, 26, 32 and 26. Glyph i is a cell of width 2 + i % 4 filled
// with colour 0x0400 + i, except one pixel left as background; the first
// cell of rows 2-5 is a pixel wider than its advance says, as in the game.
Bytes makeFont(int lineHeight, int rowsToDraw = 5);

// lexicon.dic holding the words, numbered from 0.
Bytes makeLexicon(const std::vector<std::string> &words);

// Files in memory, for the screens.
struct MemoryArchive : Archive {
    std::map<std::string, Bytes> files;
    std::vector<std::string> list() const override;
    bool has(std::string_view name) const override { return files.count(std::string(name)) > 0; }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override;
    std::string describe() const override { return "memory"; }
};

} // namespace reluva::botf::test
