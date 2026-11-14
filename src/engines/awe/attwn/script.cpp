// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/script.h"

#include "common/fs.h"
#include "engines/awe/game/conditions.h"

#include <cstdlib>

namespace reluva::awe::attwn {

namespace {

// A tag's value as the game's store gives it (0x412700): -99 if it's unset,
// which these words take as 0.
int tagOr0(const GameState &state, const std::string &name) {
    const auto it = state.tags.find(name);
    return it == state.tags.end() || it->second == -99 ? 0 : it->second;
}

} // namespace

bool condition(const GameState &state, const std::vector<std::string> &words, bool &holds) {
    if (words.empty() || words[0] != "suspicion")
        return false;
    const int value = tagOr0(state, "suspicion_" + fs::toLower(words.size() > 1 ? words[1] : std::string()));
    const std::string n = words.size() > 2 ? words[2] : std::string();
    if (!n.empty() && n[0] == '<')
        holds = value < std::atoi(n.c_str() + 1);
    else if (!n.empty() && n[0] == '>')
        holds = value > std::atoi(n.c_str() + 1);
    else
        holds = value == std::atoi(n.c_str());
    return true;
}

bool verb(GameState &state, const std::vector<std::string> &words) {
    if (words.empty() || fs::toLower(words[0]) != "inc")
        return false;
    // The act's name as the game writes it ("%02d%c", 0x4028c0), against
    // the third to fifth words, whatever their case (0x402a20).
    bool named = false;
    for (size_t i = 2; i < words.size() && i < 5; i++)
        named |= !state.act.empty() && fs::toLower(words[i]) == fs::toLower(state.act);
    if (named) {
        const bool needed = words.size() > 1 && fs::toLower(words[1]) == "needed";
        const std::string tag = fs::toLower(state.act) + (needed ? "_neededcounter" : "_possiblecounter");
        state.tags[tag] = tagOr0(state, tag) + 1;
    }
    return true;
}

} // namespace reluva::awe::attwn
