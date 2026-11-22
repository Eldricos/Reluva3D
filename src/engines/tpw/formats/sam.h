// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::tpw {

/**
 * A settings file (`.sam`): named values, one a line, and grids, as
 * SimCoaster's `Data/Config.sam` and every ride's. See
 * docs/engines/tpw/games/simcoaster/formats/sam.md.
 */
class SamFile {
public:
    /** A value: a number, or text from between quotes. */
    struct Value {
        bool isText = false;
        double number = 0;
        std::string text;
    };

    /** A line's name and its values, or a name and the grid after it. */
    struct Entry {
        std::string name; // as written: "Upgrades[0].CostOfUpgrade"
        std::vector<Value> values;
        std::vector<std::string> grid; // the rows between its "---" lines
        int line = 0;                  // from 1
    };

    /**
     * Reads the file. Fails, saying the line, on one that isn't a comment,
     * a name with its values, or a grid.
     */
    Error parse(const std::vector<uint8_t> &data);

    /** Every entry in the file's order. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** The last entry of that name, ignoring case; nullptr if there's none. */
    const Entry *find(std::string_view name) const;

    /**
     * A name's first value as a number, or as text; `fallback` if the file
     * hasn't the name, or its value isn't of that kind.
     */
    ///@{
    double number(std::string_view name, double fallback = 0) const;
    std::string text(std::string_view name, std::string_view fallback = {}) const;
    ///@}

private:
    std::vector<Entry> _entries;
};

} // namespace reluva::tpw
