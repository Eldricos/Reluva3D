// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/strings.h"

#include "common/fs.h"

#include <cstdlib>

namespace reluva::sith {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// What's between the quote at `s`'s start and the next; empty and false if
// it isn't quoted or isn't closed. No escapes: "\" is a backslash.
bool quoted(std::string_view &s, std::string &out) {
    if (s.empty() || s.front() != '"')
        return false;
    const size_t end = s.find('"', 1);
    if (end == std::string_view::npos)
        return false;
    out.assign(s.substr(1, end - 1));
    s.remove_prefix(end + 1);
    return true;
}

// The game's messages write a new line as \n.
std::string newLines(std::string s) {
    for (size_t at = s.find("\\n"); at != std::string::npos; at = s.find("\\n", at + 1))
        s.replace(at, 2, "\n");
    return s;
}

} // namespace

const std::string *StringTable::find(std::string_view key) const {
    const std::string lower = fs::toLower(key);
    for (const auto &[k, v] : entries)
        if (fs::toLower(k) == lower)
            return &v;
    return nullptr;
}

Error parseStrings(const std::vector<uint8_t> &data, StringTable &out) {
    out = StringTable{};
    const std::string_view text(reinterpret_cast<const char *>(data.data()), data.size());
    bool counted = false;
    int lineNumber = 0;
    for (size_t pos = 0; pos < text.size();) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNumber;
        if (line.empty() || line.front() == '#')
            continue;
        if (!counted) {
            if (fs::toLower(line.substr(0, 4)) != "msgs")
                return Error::fail("string table without its MSGS line");
            out.declaredCount = uint32_t(std::strtoul(std::string(line.substr(4)).c_str(), nullptr, 10));
            counted = true;
            continue;
        }
        if (fs::toLower(line.substr(0, 3)) == "end" && (line.size() == 3 || line[3] == ' ' || line[3] == '\t'))
            break;

        // "key" number "string", then perhaps a comment, which may hold quotes.
        std::string key;
        std::string value;
        if (!quoted(line, key))
            return Error::fail("string table line " + std::to_string(lineNumber) + " has no quoted key");
        line = trim(line);
        while (!line.empty() && line.front() != ' ' && line.front() != '\t' && line.front() != '"')
            line.remove_prefix(1);
        line = trim(line);
        if (!quoted(line, value)) {
            // One credits line has its string unquoted: the rest of the line.
            const size_t comment = line.find('#');
            value.assign(trim(line.substr(0, comment)));
        }
        out.entries.emplace_back(std::move(key), newLines(std::move(value)));
    }
    if (!counted)
        return Error::fail("string table without its MSGS line");
    return Error::success();
}

} // namespace reluva::sith
