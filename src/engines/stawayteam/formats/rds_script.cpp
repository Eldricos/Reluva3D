// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/formats/rds_script.h"

namespace reluva::stawayteam {

const std::string *RdsScript::find(std::string_view label) const {
    for (const Entry &e : entries)
        if (e.label == label)
            return &e.text;
    return nullptr;
}

RdsScript readRdsScript(std::string_view text) {
    RdsScript out;
    RdsScript::Entry *entry = nullptr;
    // Blank lines are held back until a line of text follows them, so that
    // those between entries aren't part of either.
    size_t blanks = 0;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.substr(0, 2) == "//")
            continue;
        if (!line.empty() && line.front() == ':') {
            out.entries.push_back({std::string(line.substr(1)), {}});
            entry = &out.entries.back();
            blanks = 0;
            continue;
        }
        if (!entry)
            continue; // before the first label: only comments and blank lines in the game's files
        if (line.empty()) {
            ++blanks;
            continue;
        }
        if (!entry->text.empty())
            entry->text.append(blanks + 1, '\n');
        blanks = 0;
        entry->text += line;
    }
    return out;
}

} // namespace reluva::stawayteam
