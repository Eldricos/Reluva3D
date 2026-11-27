// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/tech_tree.h"

#include "common/text.h"

#include <sstream>

namespace reluva::storm3d {

namespace {

std::string nameOf(const std::string &file) {
    std::string name = text::toLower(file);
    if (text::hasSuffixIgnoreCase(name, ".odf"))
        name.resize(name.size() - 4);
    return name;
}

} // namespace

void TechTree::parse(std::string_view file) {
    _needs.clear();
    std::istringstream in{std::string(file)};
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string unit;
        int count = 0;
        if (!(words >> unit >> count) || count < 0)
            continue;
        std::vector<std::string> needs;
        std::string need;
        for (int i = 0; i < count && words >> need; ++i)
            needs.push_back(nameOf(need));
        _needs[nameOf(unit)] = needs;
    }
}

const std::vector<std::string> *TechTree::needsOf(const std::string &unit) const {
    auto it = _needs.find(text::toLower(unit));
    return it == _needs.end() ? nullptr : &it->second;
}

bool TechTree::allows(const std::string &unit, const std::set<std::string> &owned) const {
    const std::vector<std::string> *needs = needsOf(unit);
    if (!needs)
        return true;
    for (const std::string &n : *needs)
        if (!owned.count(n))
            return false;
    return true;
}

} // namespace reluva::storm3d
