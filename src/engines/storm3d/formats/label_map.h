// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * The front end's text (`label.map`): sections of entries, each a key and
 * its text, as
 *
 *     single_player {
 *         + title     {SINGLE PLAYER}
 *     }
 *
 * with `#` lines for comments. See
 * docs/engines/storm3d/games/armada/formats/label-map.md.
 */
class LabelMap {
public:
    /** One entry. */
    struct Entry {
        std::string section;
        std::string key;
        std::string text; // UTF-8, from the file's Windows-1252
    };

    /**
     * Reads the file's text.
     * @return a failure for an entry outside a section, a section left
     *         open, or an entry without its braces
     */
    Error parse(std::string_view file);

    /** Every entry, in the file's order. */
    const std::vector<Entry> &entries() const { return _entries; }
    /** A section's entry's text; `fallback` if there's none (keys match ignoring case). */
    std::string text(std::string_view section, std::string_view key, std::string_view fallback = {}) const;

private:
    std::vector<Entry> _entries;
};

} // namespace reluva::storm3d
