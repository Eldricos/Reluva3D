// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/text_tables.h"

#include "common/bytes.h"
#include "common/text.h"

#include <string_view>

namespace reluva::sfc {

namespace {

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

std::string unquote(std::string_view cell) {
    if (cell.size() < 2 || cell.front() != '"' || cell.back() != '"')
        return std::string(cell);
    std::string out;
    for (size_t i = 1; i + 1 < cell.size(); ++i) {
        out += cell[i];
        if (cell[i] == '"' && cell[i + 1] == '"')
            ++i;
    }
    return out;
}

} // namespace

Error TabTable::read(const std::vector<uint8_t> &data) {
    rows.clear();
    const std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
    for (const std::string_view line : splitLines(text)) {
        std::vector<std::string> row;
        size_t start = 0;
        while (true) {
            const size_t tab = line.find('\t', start);
            row.push_back(unquote(line.substr(start, tab == std::string_view::npos ? line.npos : tab - start)));
            if (tab == std::string_view::npos)
                break;
            start = tab + 1;
        }
        rows.push_back(std::move(row));
    }
    return Error::success();
}

Error StringTable::read(const std::vector<uint8_t> &data) {
    entries.clear();
    if (data.size() % 2)
        return Error::fail("string table: an odd number of bytes for UTF-16");
    std::vector<uint16_t> units(data.size() / 2);
    for (size_t i = 0; i < units.size(); ++i)
        units[i] = readLE16(data.data() + 2 * i);
    const size_t first = !units.empty() && units[0] == 0xfeff ? 1 : 0;
    const std::string text = text::utf16leToUtf8(units.data() + first, units.size() - first);
    for (const std::string_view line : splitLines(text)) {
        if (line.empty())
            continue;
        if (line[0] != '|') {
            entries.push_back({{}, {std::string(line)}});
            continue;
        }
        // A line with a single '|' (aaStrings.txt has one, a lone "|") is
        // read as a key with no text.
        const size_t bar = line.find('|', 1);
        const std::string_view key = line.substr(1, bar == std::string_view::npos ? line.npos : bar - 1);
        const std::string value(bar == std::string_view::npos ? std::string_view() : line.substr(bar + 1));
        if (key.empty() && !entries.empty() && !entries.back().key.empty())
            entries.back().text.push_back(value);
        else
            entries.push_back({std::string(key), {value}});
    }
    return Error::success();
}

} // namespace reluva::sfc
