// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/preferences.h"

#include "common/text.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::storm3d {

Preferences Preferences::defaults(std::string_view driver) {
    // The file the game wrote on its first run under the emulator; what
    // the first line stands for isn't known.
    Preferences p;
    p._lines = {"9404509", "2 3 1 6 1 1 0", "10 10 10 8 1 1 1", std::string(driver), "0.5 0.5 5 5 5 4 640 480 16 1 0",
                "0"};
    return p;
}

Preferences Preferences::parse(std::string_view file, std::string_view driver) {
    Preferences p = defaults(driver);
    size_t i = 0;
    for (std::string line : text::split(file, '\n')) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (i < kLines)
            p._lines[i++] = line;
    }
    return p;
}

std::string Preferences::text() const {
    std::string out;
    for (const std::string &line : _lines)
        out += line + "\r\n";
    return out;
}

double Preferences::number(Line line, size_t field) const {
    const std::vector<std::string> parts = text::split(_lines[line], ' ');
    size_t n = 0;
    for (const std::string &part : parts)
        if (!part.empty() && n++ == field)
            return std::strtod(part.c_str(), nullptr);
    return 0;
}

void Preferences::setNumber(Line line, size_t field, double value) {
    std::vector<std::string> parts;
    for (const std::string &part : text::split(_lines[line], ' '))
        if (!part.empty())
            parts.push_back(part);
    if (parts.size() <= field)
        parts.resize(field + 1, "0");
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", value);
    parts[field] = buf;
    std::string joined;
    for (const std::string &part : parts)
        joined += (joined.empty() ? "" : " ") + part;
    _lines[line] = joined;
}

} // namespace reluva::storm3d
