// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The syntax is SimCoaster's 369 settings files', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/sam.md.

#include "engines/tpw/formats/sam.h"

#include "common/fs.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::tpw {

namespace {

constexpr std::string_view kGridFence = "---";

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (isSpace(s.front()) || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (isSpace(s.back()) || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

bool isWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// How many fields a name has after its group ("Info.Id" 1, "A[0].B.C" 2),
// or -1 if it isn't one: words, each with an optional [index], between dots.
int fieldsOf(std::string_view name) {
    int dots = 0;
    size_t i = 0;
    while (true) {
        const size_t start = i;
        while (i < name.size() && isWordChar(name[i]))
            ++i;
        if (i == start)
            return -1;
        if (i < name.size() && name[i] == '[') {
            const size_t digits = ++i;
            while (i < name.size() && name[i] >= '0' && name[i] <= '9')
                ++i;
            if (i == digits || i >= name.size() || name[i] != ']')
                return -1;
            ++i;
        }
        if (i == name.size())
            return dots > 0 ? dots : -1;
        if (name[i] != '.')
            return -1;
        ++dots;
        ++i;
    }
}

// A number as the files write them: a sign, digits, a point, digits.
bool parseNumber(std::string_view token, double &out) {
    size_t i = token.empty() || (token[0] != '-' && token[0] != '+') ? 0 : 1;
    bool digits = false;
    bool point = false;
    for (; i < token.size(); ++i) {
        if (token[i] >= '0' && token[i] <= '9')
            digits = true;
        else if (token[i] == '.' && !point)
            point = true;
        else
            return false;
    }
    if (!digits)
        return false;
    out = std::strtod(std::string(token).c_str(), nullptr);
    return true;
}

// The next value at the start of `rest`, taken off it; false (and `rest`
// as it was) if what's there isn't one: the line's comment starts there.
bool takeValue(std::string_view &rest, SamFile::Value &out) {
    rest = trim(rest);
    if (rest.empty())
        return false;
    if (rest[0] == '"') {
        const size_t end = rest.find('"', 1);
        if (end == std::string_view::npos)
            return false;
        out.isText = true;
        out.text = std::string(rest.substr(1, end - 1));
        rest.remove_prefix(end + 1);
        return true;
    }
    size_t end = 0;
    while (end < rest.size() && !isSpace(rest[end]))
        ++end;
    if (!parseNumber(rest.substr(0, end), out.number))
        return false;
    out.isText = false;
    rest.remove_prefix(end);
    return true;
}

} // namespace

Error SamFile::parse(const std::vector<uint8_t> &data) {
    _entries.clear();
    const std::string_view all(reinterpret_cast<const char *>(data.data()), data.size());
    std::vector<std::string_view> lines;
    for (size_t at = 0; at <= all.size();) {
        const size_t end = std::min(all.find('\n', at), all.size());
        lines.push_back(trim(all.substr(at, end - at)));
        at = end + 1;
    }
    auto fail = [](size_t i, const std::string &why) {
        return Error::fail("line " + std::to_string(i + 1) + ": " + why);
    };
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        if (line.empty() || line[0] == '#')
            continue;
        size_t nameEnd = 0;
        while (nameEnd < line.size() && !isSpace(line[nameEnd]))
            ++nameEnd;
        Entry e;
        e.name = std::string(line.substr(0, nameEnd));
        e.line = int(i + 1);
        const int fields = fieldsOf(e.name);
        if (fields < 0)
            return fail(i, "\"" + std::string(line) + "\" isn't a setting");
        std::string_view rest = line.substr(nameEnd);
        Value v;
        while (int(e.values.size()) < fields && takeValue(rest, v))
            e.values.push_back(v);
        if (e.values.empty()) {
            // A grid: the name alone, then rows between two fences.
            size_t row = i + 1;
            while (row < lines.size() && lines[row].empty())
                ++row;
            if (row >= lines.size() || lines[row] != kGridFence)
                return fail(i, e.name + " has no value");
            for (++row; row < lines.size() && lines[row] != kGridFence; ++row)
                e.grid.emplace_back(lines[row]);
            if (row >= lines.size())
                return fail(i, e.name + "'s grid has no end");
            i = row;
        } else if (int(e.values.size()) < fields) {
            return fail(i, e.name + " has " + std::to_string(e.values.size()) + " of its " + std::to_string(fields) +
                               " values");
        }
        _entries.push_back(std::move(e));
    }
    return Error::success();
}

const SamFile::Entry *SamFile::find(std::string_view name) const {
    const std::string key = fs::toLower(name);
    for (auto it = _entries.rbegin(); it != _entries.rend(); ++it)
        if (fs::toLower(it->name) == key)
            return &*it;
    return nullptr;
}

double SamFile::number(std::string_view name, double fallback) const {
    const Entry *e = find(name);
    return e && !e->values.empty() && !e->values[0].isText ? e->values[0].number : fallback;
}

std::string SamFile::text(std::string_view name, std::string_view fallback) const {
    const Entry *e = find(name);
    return e && !e->values.empty() && e->values[0].isText ? e->values[0].text : std::string(fallback);
}

} // namespace reluva::tpw
