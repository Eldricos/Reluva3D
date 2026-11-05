// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/ini.h"

#include <cstdlib>

#include "common/serializer.h"

namespace reluva::stgenerations {

void IniFile::load(const std::string& text) {
    _sections.clear();
    _changes.clear();
    Section* section = nullptr;
    for (std::string line : text::split(text, '\n')) {
        line = text::trim(line);
        if (line.empty() || line[0] == ';')
            continue;
        if (line[0] == '[') {
            size_t end = line.find(']');
            section = &_sections[text::toLower(line.substr(1, end == std::string::npos ? end : end - 1))];
            continue;
        }
        size_t eq = line.find('=');
        if (!section || eq == std::string::npos)
            continue;
        std::string key = text::toLower(text::trim(line.substr(0, eq)));
        // The first copy of a key wins, as the Windows lookups find it.
        if (!section->count(key))
            (*section)[key] = text::trim(line.substr(eq + 1));
    }
}

bool IniFile::hasSection(const std::string& section) const {
    return _sections.count(text::toLower(section)) != 0;
}

bool IniFile::has(const std::string& section, const std::string& key) const {
    auto s = _sections.find(text::toLower(section));
    return s != _sections.end() && s->second.count(text::toLower(key));
}

std::string IniFile::get(const std::string& section, const std::string& key,
                            const std::string& fallback) const {
    auto s = _sections.find(text::toLower(section));
    if (s == _sections.end())
        return fallback;
    auto k = s->second.find(text::toLower(key));
    return k == s->second.end() ? fallback : k->second;
}

int IniFile::getInt(const std::string& section, const std::string& key, int fallback) const {
    return has(section, key) ? std::atoi(get(section, key).c_str()) : fallback;
}

double IniFile::getDouble(const std::string& section, const std::string& key, double fallback) const {
    return has(section, key) ? std::atof(get(section, key).c_str()) : fallback;
}

bool IniFile::getBool(const std::string& section, const std::string& key, bool fallback) const {
    if (!has(section, key))
        return fallback;
    std::string v = text::toLower(get(section, key));
    return v == "true" || v == "yes" || std::atoi(v.c_str()) != 0;
}

void IniFile::set(const std::string& section, const std::string& key, const std::string& value) {
    _sections[text::toLower(section)][text::toLower(key)] = value;
    _changes[{ text::toLower(section), text::toLower(key) }] = value;
}

void IniFile::syncChanges(Serializer& s) {
    std::vector<std::pair<std::string, std::string>> keys;
    std::vector<std::string> values;
    for (const auto& change : _changes) {
        keys.push_back(change.first);
        values.push_back(change.second);
    }
    uint32_t count = (uint32_t)keys.size();
    s.syncAsUint32(count);
    if (s.isLoading()) {
        if (count > 10000) {
            s.fail();
            return;
        }
        keys.resize(count);
        values.resize(count);
    }
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        s.syncString(keys[i].first);
        s.syncString(keys[i].second);
        s.syncString(values[i]);
    }
    if (s.isLoading() && !s.err()) {
        for (uint32_t i = 0; i < count; i++)
            set(keys[i].first, keys[i].second, values[i]);
    }
}

} // namespace reluva::stgenerations
