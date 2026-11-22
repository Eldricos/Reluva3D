// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/formats/area_layout.h"

#include <charconv>

namespace reluva::legoloco {

namespace {

// The text's lines, trimmed, without the empty ones.
std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        if (!line.empty())
            out.push_back(line);
    }
    return out;
}

bool number(std::string_view s, int &out) {
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && end == s.data() + s.size() && out >= 0;
}

} // namespace

Error parseAreaLayout(std::string_view text, AreaLayout &out) {
    out = AreaLayout();
    const auto l = lines(text);
    if (l.size() < 3 || !number(l[0], out.areas) || !number(l[1], out.columns) || !number(l[2], out.rows))
        return Error::fail("doesn't start with the areas, columns and rows");
    if (l.size() - 3 < size_t(out.areas))
        return Error::fail("names " + std::to_string(l.size() - 3) + " of its " + std::to_string(out.areas) + " areas");
    for (int i = 0; i < out.areas; ++i)
        out.boards.emplace_back(l[size_t(3 + i)]);
    return Error::success();
}

std::vector<std::string> parseLayoutIndex(std::string_view text) {
    std::vector<std::string> out;
    for (std::string_view line : lines(text))
        out.emplace_back(line);
    return out;
}

} // namespace reluva::legoloco
