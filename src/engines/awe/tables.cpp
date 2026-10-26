// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/tables.h"

#include "common/fs.h"
#include "common/text.h"

namespace reluva::awe {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

// Windows-1252's characters 0x80-0x9f, as code points.
constexpr uint16_t kCp1252[32] = {0x20ac, 0xfffd, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                  0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0xfffd, 0x017d, 0xfffd,
                                  0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                  0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0xfffd, 0x017e, 0x0178};

} // namespace

std::string decodeText(const std::vector<uint8_t>& data) {
    size_t start = 0;
    bool bigEndian = false, utf16 = false;
    if (data.size() >= 2 && data[0] == 0xff && data[1] == 0xfe) {
        utf16 = true, start = 2;
    } else if (data.size() >= 2 && data[0] == 0xfe && data[1] == 0xff) {
        utf16 = true, bigEndian = true, start = 2;
    } else if (data.size() >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
        return std::string(data.begin() + 3, data.end());
    } else if (data.size() >= 2 && data[1] == 0 && data[0] != 0) {
        utf16 = true; // no byte order mark, but ASCII as UTF-16LE
    }
    if (utf16) {
        std::vector<uint16_t> units;
        units.reserve(data.size() / 2);
        for (size_t i = start; i + 1 < data.size(); i += 2)
            units.push_back(bigEndian ? uint16_t(data[i] << 8 | data[i + 1]) : uint16_t(data[i] | data[i + 1] << 8));
        return text::utf16leToUtf8(units.data(), units.size());
    }
    std::vector<uint16_t> units;
    units.reserve(data.size());
    for (uint8_t b : data)
        units.push_back(b >= 0x80 && b < 0xa0 ? kCp1252[b - 0x80] : b);
    return text::utf16leToUtf8(units.data(), units.size());
}

Table parseTable(std::string_view text) {
    Table t;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false, rowHasText = false, first = true;
    auto endCell = [&] {
        rowHasText |= !trim(cell).empty();
        row.push_back(std::string(trim(cell)));
        cell.clear();
    };
    auto endRow = [&] {
        endCell();
        if (first) {
            t.headings = std::move(row);
            first = false;
        } else if (rowHasText) {
            t.rows.push_back(std::move(row));
        }
        row.clear();
        rowHasText = false;
    };
    for (size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (quoted) {
            if (c == '"' && i + 1 < text.size() && text[i + 1] == '"')
                cell += '"', i++;
            else if (c == '"')
                quoted = false;
            else
                cell += c;
        } else if (c == '"' && trim(cell).empty()) {
            cell.clear();
            quoted = true;
        } else if (c == '\t') {
            endCell();
        } else if (c == '\n') {
            endRow();
        } else if (c != '\r') {
            cell += c;
        }
    }
    if (!cell.empty() || !row.empty())
        endRow();
    return t;
}

int Table::column(std::string_view heading) const {
    const std::string want = fs::toLower(heading);
    for (size_t i = 0; i < headings.size(); i++)
        if (fs::toLower(headings[i]) == want)
            return int(i);
    return -1;
}

const std::string& Table::cell(size_t row, int col) const {
    static const std::string none;
    if (row >= rows.size() || col < 0 || size_t(col) >= rows[row].size())
        return none;
    return rows[row][size_t(col)];
}

int Table::find(std::string_view key) const {
    const std::string want = fs::toLower(key);
    for (size_t i = 0; i < rows.size(); i++)
        if (!rows[i].empty() && fs::toLower(rows[i][0]) == want)
            return int(i);
    return -1;
}

std::vector<Command> parseCommands(std::string_view text) {
    std::vector<Command> out;
    if (trim(text) == "0")
        return out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find_first_of(",\n", start);
        if (end == std::string_view::npos)
            end = text.size();
        Command cmd;
        std::string_view part = text.substr(start, end - start);
        size_t i = 0;
        while (i < part.size()) {
            while (i < part.size() && isSpace(part[i]))
                i++;
            size_t j = i;
            while (j < part.size() && !isSpace(part[j]))
                j++;
            if (j > i)
                cmd.words.emplace_back(part.substr(i, j - i));
            i = j;
        }
        if (!cmd.words.empty())
            out.push_back(std::move(cmd));
        start = end + 1;
    }
    return out;
}

} // namespace reluva::awe
