// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/game_files.h"

#include "common/fs.h"

#include <cctype>
#include <fstream>
#include <iterator>

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

GameFiles::GameFiles(const std::filesystem::path &gameDir) : _game(gameDir) {
    const auto ini = fs::findFile(gameDir, "stbof.ini");
    if (!ini)
        return;
    std::ifstream in(*ini, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto values = parseStbofIni(text);
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
