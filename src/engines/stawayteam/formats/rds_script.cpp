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
    // As the game's reader does (0x413f40): `//` lines skipped; a `:` line
    // starts an entry, its label the rest of the line as written; any other
    // line, blank ones too, is added to the entry with a line break after
    // it, and the entry's text loses its trailing tabs, spaces and line
    // breaks as the next starts or the file ends (0x4e3450, 0x5dfce0).
    // Lines before the first label are dropped (0x5dfc00).
    RdsScript out;
    const auto finish = [&out] {
        if (out.entries.empty())
            return;
        std::string &done = out.entries.back().text;
        while (!done.empty() &&
               (done.back() == ' ' || done.back() == '\t' || done.back() == '\r' || done.back() == '\n'))
            done.pop_back();
    };
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
            finish();
            out.entries.push_back({std::string(line.substr(1)), {}});
            continue;
        }
        if (out.entries.empty())
            continue;
        out.entries.back().text.append(line);
        out.entries.back().text += '\n';
    }
    finish();
    return out;
}

} // namespace reluva::stawayteam
