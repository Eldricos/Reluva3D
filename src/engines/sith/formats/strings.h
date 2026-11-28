// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::sith {

/**
 * A .uni string table: the text the game shows, each string by its key
 * (docs/engines/sith/games/jk/formats/uni.md).
 */
struct StringTable {
    /** The count on its MSGS line: at least the number of strings, not always equal. */
    uint32_t declaredCount = 0;
    /** Each key and its string, in the file's order. */
    std::vector<std::pair<std::string, std::string>> entries;

    /** The string for a key, matched ignoring case, or nullptr. */
    const std::string *find(std::string_view key) const;
};

/** Reads a .uni file; fails if it has no MSGS line, or an entry can't be read. */
Error parseStrings(const std::vector<uint8_t> &data, StringTable &out);

} // namespace reluva::sith
