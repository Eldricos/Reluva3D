// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/formats/easter_eggs.h"

#include <charconv>
#include <cstdio>
#include <limits>
#include <map>
#include <string>

namespace reluva::legoloco {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

// Each section's keys and values, both sections and keys lower-cased, the
// first of a key kept, as GetPrivateProfileString finds them.
std::map<std::string, std::map<std::string, std::string>> readIni(std::string_view text) {
    std::map<std::string, std::map<std::string, std::string>> sections;
    std::map<std::string, std::string> *section = nullptr;
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        const std::string_view line = trim(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            section = &sections[lower(trim(line.substr(1, line.size() - 2)))];
            continue;
        }
        const size_t eq = line.find('=');
        if (section && eq != std::string_view::npos)
            section->emplace(lower(trim(line.substr(0, eq))), std::string(trim(line.substr(eq + 1))));
    }
    return sections;
}

// The fields of one line, read as sscanf reads "%ld,%ld,...": a number
// after any spaces, then the comma exactly.
class Fields {
public:
    explicit Fields(std::string_view s) : _s(s) {}

    template<typename T>
    bool number(T &out) {
        while (_p < _s.size() && (_s[_p] == ' ' || _s[_p] == '\t'))
            ++_p;
        long long v = 0;
        const auto [end, ec] = std::from_chars(_s.data() + _p, _s.data() + _s.size(), v);
        if (ec != std::errc() || v < std::numeric_limits<T>::min() || v > std::numeric_limits<T>::max())
            return false;
        _p = size_t(end - _s.data());
        out = T(v);
        return true;
    }
    // %c: the very next character, spaces included.
    bool character(char &out) {
        if (_p >= _s.size())
            return false;
        out = _s[_p++];
        return true;
    }
    bool comma() { return _p < _s.size() && _s[_p++] == ','; }

private:
    std::string_view _s;
    size_t _p = 0;
};

bool parseTimeEvent(std::string_view s, TimeEvent &e) {
    Fields f(s);
    return f.number(e.startDay) && f.comma() && f.number(e.startMonth) && f.comma() && f.number(e.endDay) &&
           f.comma() && f.number(e.endMonth) && f.comma() && f.number(e.startHour) && f.comma() &&
           f.number(e.startMinute) && f.comma() && f.number(e.endHour) && f.comma() && f.number(e.endMinute) &&
           f.comma() && f.number(e.resource) && f.comma() && f.number(e.frameSet) && f.comma() && f.number(e.period) &&
           f.comma() && f.character(e.type) && f.comma() && f.number(e.x) && f.comma() && f.number(e.y);
}

bool parseLoadEvent(std::string_view s, LoadEvent &e) {
    Fields f(s);
    return f.number(e.startDay) && f.comma() && f.number(e.startMonth) && f.comma() && f.number(e.endDay) &&
           f.comma() && f.number(e.endMonth) && f.comma() && f.number(e.resource) && f.comma() &&
           f.number(e.replacement);
}

} // namespace

Error parseEasterEggs(std::string_view text, EasterEggs &out) {
    out = EasterEggs();
    const auto sections = readIni(text);
    auto each = [&](const char *name, auto parse) -> Error {
        const auto section = sections.find(name);
        if (section == sections.end())
            return Error::success();
        for (int key = 1;; ++key) {
            char keyText[16];
            std::snprintf(keyText, sizeof keyText, "%03d", key);
            const auto value = section->second.find(keyText);
            if (value == section->second.end() || value->second.empty())
                return Error::success();
            if (!parse(value->second))
                return Error::fail(std::string("[") + name + "] " + keyText + ": can't read \"" + value->second + "\"");
        }
    };
    if (Error err = each("timeevents", [&](const std::string &v) {
            TimeEvent e;
            if (!parseTimeEvent(v, e))
                return false;
            out.timeEvents.push_back(e);
            return true;
        }))
        return err;
    return each("loadevents", [&](const std::string &v) {
        LoadEvent e;
        if (!parseLoadEvent(v, e))
            return false;
        out.loadEvents.push_back(e);
        return true;
    });
}

} // namespace reluva::legoloco
