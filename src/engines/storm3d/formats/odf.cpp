// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/odf.h"

#include "common/text.h"

#include <cstdint>
#include <cstdlib>

namespace reluva::storm3d {

namespace {

constexpr int kMaxIncludeDepth = 16;

bool isKeyStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool isKeyChar(char c) {
    return isKeyStart(c) || (c >= '0' && c <= '9');
}

// The line without a `//` comment. Quotes are respected, so a `//` in a
// string stays: none of the game's files has one, but a path could.
std::string_view stripComment(std::string_view line) {
    bool quoted = false;
    for (size_t i = 0; i + 1 < line.size(); ++i) {
        if (line[i] == '"')
            quoted = !quoted;
        else if (!quoted && line[i] == '/' && line[i + 1] == '/')
            return line.substr(0, i);
    }
    return line;
}

} // namespace

Error Odf::load(std::string_view name, const Loader &loader) {
    _entries.clear();
    _problems.clear();
    return parse(name, loader, 0);
}

Error Odf::parse(std::string_view name, const Loader &loader, int depth) {
    if (depth > kMaxIncludeDepth)
        return Error::fail(std::string(name) + ": includes nest more than " + std::to_string(kMaxIncludeDepth) +
                           " deep");
    std::string text;
    if (Error err = loader(name, text))
        return err;

    std::string section;
    // The entry a line without `=` continues; only this file's own.
    size_t continued = SIZE_MAX;
    size_t lineNumber = 0;
    for (const std::string &raw : text::split(text, '\n')) {
        ++lineNumber;
        const std::string line = text::trim(stripComment(raw));
        if (line.empty())
            continue;
        const std::string where = std::string(name) + ":" + std::to_string(lineNumber);

        if (line.rfind("#include", 0) == 0) {
            const std::vector<std::string> names = splitStrings(line.substr(8));
            if (names.size() != 1 || names[0].empty()) {
                _problems.push_back(where + ": " + line);
                continue;
            }
            if (Error err = parse(names[0], loader, depth + 1))
                return Error::fail(where + ": " + err.message);
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = text::trim(std::string_view(line).substr(1, line.size() - 2));
            continued = SIZE_MAX;
            continue;
        }
        size_t keyEnd = 0;
        if (isKeyStart(line[0]))
            while (keyEnd < line.size() && isKeyChar(line[keyEnd]))
                ++keyEnd;
        const size_t equals = line.find_first_not_of(" \t", keyEnd);
        if (keyEnd > 0 && equals != std::string::npos && line[equals] == '=') {
            _entries.push_back(
                {std::string(name), section, line.substr(0, keyEnd), text::trim(line.substr(equals + 1))});
            continued = _entries.size() - 1;
            continue;
        }
        // A list of names goes on over the lines after its key
        // (possibleCraftNames = followed by lines of quoted names); every
        // such line in the game's files starts with a quote.
        if (continued != SIZE_MAX && line.front() == '"') {
            Entry &e = _entries[continued];
            e.value += e.value.empty() ? line : "\n" + line;
            continue;
        }
        _problems.push_back(where + ": " + line);
    }
    return Error::success();
}

const Odf::Entry *Odf::find(std::string_view key) const {
    for (auto it = _entries.rbegin(); it != _entries.rend(); ++it)
        if (text::equalsIgnoreCase(it->key, key))
            return &*it;
    return nullptr;
}

std::string Odf::string(std::string_view key, std::string_view fallback) const {
    const Entry *e = find(key);
    if (!e)
        return std::string(fallback);
    const std::vector<std::string> all = splitStrings(e->value);
    return all.empty() ? e->value : all.front();
}

std::vector<std::string> Odf::strings(std::string_view key) const {
    const Entry *e = find(key);
    return e ? splitStrings(e->value) : std::vector<std::string>{};
}

double Odf::number(std::string_view key, double fallback) const {
    const Entry *e = find(key);
    bool ok = false;
    const double value = e ? parseNumber(e->value, ok) : 0;
    return ok ? value : fallback;
}

std::vector<std::string> Odf::splitStrings(std::string_view value) {
    std::vector<std::string> out;
    size_t i = value.find('"');
    while (i != std::string_view::npos) {
        const size_t end = value.find('"', i + 1);
        out.emplace_back(value.substr(i + 1, end == std::string_view::npos ? std::string_view::npos : end - i - 1));
        if (end == std::string_view::npos)
            break;
        i = value.find('"', end + 1);
    }
    return out;
}

double Odf::parseNumber(std::string_view value, bool &ok) {
    const std::string text(value);
    const char *start = text.c_str();
    char *end = nullptr;
    const double v = std::strtod(start, &end);
    ok = end != start;
    return ok ? v : 0;
}

} // namespace reluva::storm3d
