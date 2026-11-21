// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace reluva::stawayteam {

/**
 * A script of the game's text (.RDS): entries, each a `:Label` line and the
 * text after it up to the next label, as the dialogue, briefings and
 * descriptions are (docs/engines/stawayteam/formats/rds.md). Text is kept
 * as written, in Windows-1252, with its `$name` and `{...}` markup.
 */
struct RdsScript {
    /** One entry. */
    struct Entry {
        std::string label; // as written: leading and trailing spaces are part of it
        std::string text;  // its lines joined with '\n', blank lines at its end left off
    };
    std::vector<Entry> entries;

    /** The text of the first entry with a label, or nullptr. */
    const std::string *find(std::string_view label) const;
};

/**
 * Reads a script from a file's text; lines may end in CRLF or LF. Any text
 * is a script: one with no labels (three of the game's hold only comments)
 * has no entries.
 */
RdsScript readRdsScript(std::string_view text);

} // namespace reluva::stawayteam
