// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace reluva::sith {

namespace {

bool blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && blank(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && blank(s.back()))
        s.remove_suffix(1);
    return s;
}

} // namespace

TextLines::TextLines(const std::vector<uint8_t> &data) {
    const std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
    int number = 0;
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        ++number;
        line = trim(line.substr(0, line.find('#')));
        if (!line.empty())
            _lines.push_back({line, number});
    }
}

std::vector<std::string_view> words(std::string_view line) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && blank(line[i]))
            ++i;
        const size_t start = i;
        while (i < line.size() && !blank(line[i]))
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

bool startsWith(std::string_view line, std::string_view keyword) {
    if (line.size() < keyword.size())
        return false;
    for (size_t i = 0; i < keyword.size(); ++i)
        if (std::tolower(uint8_t(line[i])) != std::tolower(uint8_t(keyword[i])))
            return false;
    return line.size() == keyword.size() || blank(line[keyword.size()]);
}

std::string_view after(std::string_view line, std::string_view keyword) {
    return trim(line.substr(std::min(keyword.size(), line.size())));
}

bool toInt(std::string_view word, int &out) {
    const std::string s(word);
    char *end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end)
        return false;
    out = int(v);
    return true;
}

bool toFloat(std::string_view word, float &out) {
    const std::string s(word);
    char *end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return !s.empty() && !*end;
}

bool toFlags(std::string_view word, uint32_t &out) {
    const std::string s(word);
    char *end = nullptr;
    out = uint32_t(std::strtoul(s.c_str(), &end, 0));
    return !s.empty() && !*end;
}

bool numbered(std::string_view line, int &number, std::vector<std::string_view> &fields) {
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || !toInt(trim(line.substr(0, colon)), number))
        return false;
    fields = words(line.substr(colon + 1));
    return true;
}

} // namespace reluva::sith
