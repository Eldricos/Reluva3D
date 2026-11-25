// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/gui_config.h"

#include "common/text.h"

#include <cstdlib>

namespace reluva::storm3d {

namespace {

// An include within an include, and so on: the install's go one deep.
constexpr int kMaxDepth = 8;

// The words of a line: quoted names whole, without their quotes.
std::vector<std::string> wordsOf(std::string_view line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            ++i;
        if (i >= line.size())
            break;
        if (line[i] == '"') {
            const size_t end = line.find('"', i + 1);
            words.emplace_back(line.substr(i + 1, (end == std::string_view::npos ? line.size() : end) - i - 1));
            i = end == std::string_view::npos ? line.size() : end + 1;
            continue;
        }
        const size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            ++i;
        words.emplace_back(line.substr(start, i - start));
    }
    return words;
}

} // namespace

Error GuiConfig::load(std::string_view name, const Loader &loader) {
    return load(name, loader, 0);
}

Error GuiConfig::load(std::string_view name, const Loader &loader, int depth) {
    std::string text;
    if (Error err = loader(name, text))
        return err;
    const std::string folder(name.substr(0, name.find_last_of('/') + 1));
    for (std::string line : text::split(text, '\n')) {
        if (const size_t comment = line.find("//"); comment != std::string::npos)
            line.resize(comment);
        const std::vector<std::string> words = wordsOf(line);
        if (words.size() >= 2 && words[0] == "#include" && depth < kMaxDepth) {
            if (Error err = load(folder + words[1], loader, depth + 1))
                return err;
            continue;
        }
        parse(line);
    }
    return Error::success();
}

void GuiConfig::parse(std::string_view text) {
    for (std::string line : text::split(text, '\n')) {
        if (const size_t comment = line.find("//"); comment != std::string::npos)
            line.resize(comment);
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::vector<std::string> name = wordsOf(std::string_view(line).substr(0, eq));
        if (name.size() != 1)
            continue;
        _values[name[0]] = wordsOf(std::string_view(line).substr(eq + 1));
    }
}

const std::vector<std::string> *GuiConfig::find(std::string_view name) const {
    auto it = _values.find(name);
    return it == _values.end() ? nullptr : &it->second;
}

double GuiConfig::number(std::string_view name, size_t i, double fallback) const {
    const std::vector<std::string> *v = find(name);
    return v && i < v->size() ? std::strtod((*v)[i].c_str(), nullptr) : fallback;
}

std::string GuiConfig::text(std::string_view name) const {
    const std::vector<std::string> *v = find(name);
    return v && !v->empty() ? (*v)[0] : std::string();
}

std::array<int, 4> GuiConfig::area(std::string_view name) const {
    std::array<int, 4> a{};
    for (size_t i = 0; i < 4; ++i)
        a[i] = int(number(name, i));
    return a;
}

} // namespace reluva::storm3d
