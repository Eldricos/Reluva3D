// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The game's text files (Text/*.txt, Text/Dialogue/...): UTF-16 with a
// byte order mark, as a spreadsheet saves "Unicode text". Tables are rows
// of tab-separated cells, the first row their headings; a cell with a
// comma or a quote is in quotes, with quotes in it doubled.
//
// Many cells hold commands for the game's script, separated by commas:
//   "scene sirkeci.market_01 1 0, fade in, add ticket"
// and the dialogue event files hold one command per line. See
// docs/formats/awe/text.md.

namespace reluva::awe {

// UTF-16 (little-endian unless a byte order mark says otherwise) as UTF-8,
// the byte order mark dropped. Bytes without one that don't look like
// UTF-16 are taken as Windows-1252.
std::string decodeText(const std::vector<uint8_t>& data);

struct Table {
    std::vector<std::string> headings;
    std::vector<std::vector<std::string>> rows; // empty rows left out

    // The column with this heading (ignoring case), or -1.
    int column(std::string_view heading) const;
    // A row's cell, "" if it has none there.
    const std::string& cell(size_t row, int column) const;
    // The first row whose first cell is `key` (ignoring case), or -1.
    int find(std::string_view key) const;
};

Table parseTable(std::string_view text);

// One script command: its words, split at spaces ("talk dia_debenham a+"
// -> talk, dia_debenham, a+).
struct Command {
    std::vector<std::string> words;
    const std::string& verb() const { return words.front(); }
};

// Splits a cell's commands at commas (or an event file's at line ends).
// A "0" cell means none.
std::vector<Command> parseCommands(std::string_view text);

} // namespace reluva::awe
