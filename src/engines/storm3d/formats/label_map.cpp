// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/label_map.h"

#include "common/text.h"

namespace reluva::storm3d {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

} // namespace

Error LabelMap::parse(std::string_view file) {
    _entries.clear();
    std::string section;
    bool open = false;
    int number = 0;
    while (!file.empty()) {
        const size_t end = file.find('\n');
        const std::string_view line = trim(file.substr(0, end));
        file.remove_prefix(end == std::string_view::npos ? file.size() : end + 1);
        ++number;
        if (line.empty() || line.front() == '#')
            continue;
        const std::string where = " (line " + std::to_string(number) + ")";
        if (line == "}") {
            if (!open)
                return Error::fail("a } outside a section" + where);
            open = false;
        } else if (line.front() == '+') {
            if (!open)
                return Error::fail("an entry outside a section" + where);
            const size_t from = line.find('{');
            const size_t to = line.rfind('}');
            if (from == std::string_view::npos || to == std::string_view::npos || to < from)
                return Error::fail("an entry without its braces" + where);
            const std::string_view key = trim(line.substr(1, from - 1));
            _entries.push_back(
                {section, std::string(key), text::windows1252ToUtf8(line.substr(from + 1, to - from - 1))});
        } else if (line.back() == '{') {
            if (open)
                return Error::fail("a section inside another" + where);
            section = std::string(trim(line.substr(0, line.size() - 1)));
            open = true;
        } else {
            return Error::fail("a line that's no entry nor section" + where);
        }
    }
    if (open)
        return Error::fail("section " + section + " isn't closed");
    return Error::success();
}

std::string LabelMap::text(std::string_view section, std::string_view key, std::string_view fallback) const {
    for (const Entry &e : _entries)
        if (text::equalsIgnoreCase(e.section, section) && text::equalsIgnoreCase(e.key, key))
            return e.text;
    return std::string(fallback);
}

} // namespace reluva::storm3d
