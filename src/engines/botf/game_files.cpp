// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/game_files.h"

#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <vector>

namespace reluva::botf {

namespace {

std::string trim(std::string_view s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return std::string(s.substr(a, b - a));
}

std::string upper(std::string s) {
    for (char &c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::map<std::string, std::string> parseStbofIni(std::string_view text) {
    std::map<std::string, std::string> values;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(start, end - start);
        const size_t eq = line.find('=');
        if (eq != std::string_view::npos)
            values[upper(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
        start = end + 1;
    }
    return values;
}

std::string updateStbofIni(std::string_view text, const std::map<std::string, std::string> &values) {
    // The order the game writes its keys in, as a Stbof.ini it wrote has
    // them (docs/engines/botf/systems/game-files.md).
    static const char *const kOrder[] = {
        "CDPATH",    "HDPATH",       "CURRENTGAME", "LANGUAGE",       "VICTORY",        "INSTALL",
        "3D",        "TACTICAL",     "SOUNDON",     "BORG",           "MOVIEON",        "AUTOSAVE",
        "TOOLTIPS",  "RANDOMEVENTS", "TURNSTATUS",  "TIMELIMIT",      "TACTIMELIMIT",   "SHAPE",
        "SIZE",      "RACENUM",      "DIFFICULTY",  "CIVLEVEL1",      "CIVLEVEL2",      "CIVLEVEL3",
        "CIVLEVEL4", "CIVLEVEL5",    "ECONFILTER",  "EVENTFILTER",    "MILITARYFILTER", "MUSICVOLUME",
        "FXVOLUME",  "VIDEOVOLUME",  "VOICEVOLUME", "SCREENVOLATILE", "STARTINGSEED",   "VERSION"};
    struct Line {
        std::string text; // without its ending
        std::string key;  // upper case, or empty for a line without '='
    };
    std::vector<Line> lines;
    std::string ending = "\r\n";
    std::string tail; // after the last line ending (the installer's ^Z)
    size_t start = 0;
    bool foundEnding = false;
    while (start < text.size()) {
        const size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            tail = std::string(text.substr(start));
            break;
        }
        std::string_view line = text.substr(start, end - start);
        if (!foundEnding) {
            ending = !line.empty() && line.back() == '\r' ? "\r\n" : "\n";
            foundEnding = true;
        }
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        const size_t eq = line.find('=');
        lines.push_back({std::string(line), eq == std::string_view::npos ? "" : upper(trim(line.substr(0, eq)))});
        start = end + 1;
    }
    for (const auto &[key, value] : values) {
        auto it = std::find_if(lines.begin(), lines.end(), [&](const Line &l) { return l.key == key; });
        if (it != lines.end()) {
            it->text = it->text.substr(0, it->text.find('=') + 1) + value;
            continue;
        }
        // After the last line whose key comes before this one in the game's
        // order; else before the first whose key comes after it; else at the
        // end.
        const auto *const order = std::find(std::begin(kOrder), std::end(kOrder), key);
        auto comesBefore = [&](const Line &l) { return std::find(std::begin(kOrder), order, l.key) != order; };
        auto comesAfter = [&](const Line &l) {
            return order != std::end(kOrder) && std::find(order + 1, std::end(kOrder), l.key) != std::end(kOrder);
        };
        auto at = lines.end();
        if (auto last = std::find_if(lines.rbegin(), lines.rend(), comesBefore); last != lines.rend())
            at = last.base();
        else if (auto first = std::find_if(lines.begin(), lines.end(), comesAfter); first != lines.end())
            at = first;
        lines.insert(at, {key + "=" + value, key});
    }
    std::string out;
    for (const Line &l : lines)
        out += l.text + ending;
    return out + tail;
}

GameFiles::GameFiles(const std::filesystem::path &gameDir) : _game(gameDir) {
    const auto ini = fs::findFile(gameDir, "stbof.ini");
    if (!ini)
        return;
    _iniPath = *ini;
    std::ifstream in(*ini, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    _ini = parseStbofIni(text);
    const auto &values = _ini;
    if (auto it = values.find("LANGUAGE"); it != values.end() && !it->second.empty())
        _language = upper(it->second);
    if (auto it = values.find("CDPATH"); it != values.end() && !it->second.empty()) {
        std::string cd = it->second;
        // "D:\" names a drive's root on Windows; elsewhere it's not a folder
        // here, and is left out.
        for (char &c : cd)
            if (c == '\\')
                c = '/';
        std::error_code ec;
        if (std::filesystem::is_directory(cd, ec))
            _cd = std::filesystem::path(cd);
    }
}

std::optional<std::filesystem::path> GameFiles::find(std::string_view name) const {
    const std::string inLanguage = _language + "/" + std::string(name);
    if (auto p = fs::findFile(_game, name))
        return p;
    if (auto p = fs::findFile(_game, inLanguage))
        return p;
    if (_cd) {
        if (auto p = fs::findFile(*_cd, name))
            return p;
        if (auto p = fs::findFile(*_cd, inLanguage))
            return p;
    }
    return std::nullopt;
}

std::string GameFiles::speechFile() const {
    return fs::toLower(_language) + ".snd";
}

} // namespace reluva::botf
