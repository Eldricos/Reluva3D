// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::botf {

/**
 * A .tdb text table from stbof.res (docs/engines/botf/formats/tdb.md): a 16-byte
 * header (count, a leftover value, the longest string's length, 0), one
 * 16-byte record per string (key, length, a leftover value, offset), then
 * the strings, zero-terminated, packed after the records.
 */
class TextTable {
public:
    /** A string and the key the game looks it up by. */
    struct Entry {
        uint32_t key = 0;
        std::string text; // UTF-8, from Windows-1252; as stored, without the zero byte
    };

    /** Reads the table, replacing the entries; fails at a string outside the text or without its zero byte. */
    Error load(const std::vector<uint8_t> &data);

    /** In the file's record order (by key in most tables, not all). */
    const std::vector<Entry> &entries() const { return _entries; }
    /**
     * The first entry with this key, or nullptr. Keys aren't unique in
     * every table.
     */
    const Entry *find(uint32_t key) const;
    /** The longest string's length, in bytes, as the header gives it. */
    uint32_t longest() const { return _longest; }

private:
    std::vector<Entry> _entries;
    uint32_t _longest = 0;
};

/**
 * A table's text as the game would show it: without the leading space and
 * the trailing newline each string carries.
 */
std::string displayText(const std::string &text);

} // namespace reluva::botf
