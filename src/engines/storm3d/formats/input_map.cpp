// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/formats/input_map.h"

#include "common/text.h"

#include <sstream>

namespace reluva::storm3d {

Error InputMap::parse(std::string_view text) {
    bindings.clear();
    problems.clear();
    bool inBlock = false;
    size_t lineNumber = 0;
    size_t opened = 0;
    for (const std::string &raw : text::split(text, '\n')) {
        ++lineNumber;
        std::string line = raw;
        if (const size_t hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        line = text::trim(line);
        if (line.empty())
            continue;
        std::istringstream words(line);
        std::vector<std::string> w;
        for (std::string word; words >> word;)
            w.push_back(word);

        if (!inBlock && w.size() == 2 && w[1] == "{") {
            bindings.push_back({w[0], {}});
            inBlock = true;
            opened = lineNumber;
        } else if (inBlock && w.size() == 1 && w[0] == "}") {
            inBlock = false;
        } else if (inBlock && w.size() == 3 && (w[0] == "+" || w[0] == "-")) {
            bindings.back().conditions.push_back({w[0] == "+", w[1], w[2]});
        } else {
            problems.push_back(std::to_string(lineNumber) + ": " + line);
        }
    }
    if (inBlock)
        return Error::fail("the block at line " + std::to_string(opened) + " isn't closed");
    return Error::success();
}

const InputMap::Binding *InputMap::find(std::string_view action) const {
    for (const Binding &b : bindings)
        if (b.action == action)
            return &b;
    return nullptr;
}

} // namespace reluva::storm3d
