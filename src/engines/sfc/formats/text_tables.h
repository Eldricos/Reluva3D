// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::sfc {

/**
 * A table saved from a spreadsheet as tab-separated text
 * (docs/engines/sfc/games/sfc2/formats/text-tables.md): the ship and
 * fighter lists, the sound and music tables. A row whose first cell is
 * empty is a comment, to the game; the rows are kept as they are.
 */
struct TabTable {
    std::vector<std::vector<std::string>> rows;

    /** Lines end with CR LF or LF; a cell in double quotes loses them, and "" in it is ". */
    Error read(const std::vector<uint8_t> &data);
    /** Whether a row is one the game reads: its first cell isn't empty. */
    static bool isData(const std::vector<std::string> &row) { return !row.empty() && !row[0].empty(); }
};

/**
 * The interface's text, the `.txt` files of `Assets/Strings/`: UTF-16 lines of
 * `|key|text`. A line `||text` adds another line to the key before it; a
 * line without a `|` first is a heading, kept as an entry with no key; a
 * line with only one `|` is a key with no text.
 */
struct StringTable {
    /** A key and its lines, or a heading. */
    struct Entry {
        std::string key;               // empty for a heading
        std::vector<std::string> text; // UTF-8
    };
    std::vector<Entry> entries;

    /** Reads a whole file: UTF-16 little-endian, a byte-order mark first or not. */
    Error read(const std::vector<uint8_t> &data);
};

} // namespace reluva::sfc
