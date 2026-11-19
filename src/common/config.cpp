// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/config.h"

#include <fstream>

namespace reluva {

static std::string trim(const std::string &s) {
    const char *ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

Error Config::load(const std::filesystem::path &path) {
    _path = path;
    _sections.clear();

    std::ifstream in(path);
    if (!in)
        return Error::success(); // a missing file just means a fresh config

    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            _sections[section];
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty())
            continue;
        _sections[section][trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return Error::success();
}

Error Config::save() const {
    std::ofstream out(_path, std::ios::trunc);
    if (!out)
        return Error::fail("Cannot write " + _path.string());
    for (const auto &[name, values] : _sections) {
        out << '[' << name << "]\n";
        for (const auto &[k, v] : values)
            out << k << '=' << v << '\n';
        out << '\n';
    }
    return out ? Error::success() : Error::fail("Error writing " + _path.string());
}

std::string Config::get(const std::string &section, const std::string &key, const std::string &fallback) const {
    auto s = _sections.find(section);
    if (s == _sections.end())
        return fallback;
    auto v = s->second.find(key);
    return v == s->second.end() ? fallback : v->second;
}

void Config::set(const std::string &section, const std::string &key, const std::string &value) {
    _sections[section][key] = value;
}

void Config::unset(const std::string &section, const std::string &key) {
    const auto it = _sections.find(section);
    if (it != _sections.end())
        it->second.erase(key);
}

bool Config::has(const std::string &section, const std::string &key) const {
    const auto it = _sections.find(section);
    return it != _sections.end() && it->second.count(key) != 0;
}

bool Config::hasSection(const std::string &section) const {
    return _sections.count(section) != 0;
}

void Config::removeSection(const std::string &section) {
    _sections.erase(section);
}

std::map<std::string, std::string> Config::values(const std::string &section) const {
    const auto it = _sections.find(section);
    return it == _sections.end() ? std::map<std::string, std::string>{} : it->second;
}

std::map<std::string, std::string> Config::settingsFor(const std::string &target) const {
    std::map<std::string, std::string> out = values(kGlobalSection);
    const std::string engine = get(target, "engine");
    if (!engine.empty())
        for (const auto &[k, v] : values(engineSection(engine)))
            out[k] = v;
    for (const auto &[k, v] : values(target))
        out[k] = v;
    return out;
}

std::vector<std::string> Config::targets() const {
    std::vector<std::string> out;
    for (const auto &[name, values] : _sections)
        if (name != kGlobalSection && !isEngineSection(name))
            out.push_back(name);
    return out;
}

} // namespace reluva
