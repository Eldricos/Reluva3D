// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>

// The conditions the game's scenes and tables test: when a scene's object
// is shown ("moving >0"), which of an object's scripts runs ("act 04a,
// !tag micheladmonish"), which music plays. See docs/formats/awe/ac2.md.
//
//   condition := alternative ('|' alternative)*
//   alternative := clause (',' clause)*
//   clause := ['!'] word [argument]
//
// Words the game's evaluator knows (motoe.exe 0x407d00..0x408a00) include
// act, chapter, debug, has, location, master, moving, rand, scene, tag,
// talk, time, timer, use, used, wearing and weather. A number argument
// may start with '<' or '>'; without either it must be equal.

namespace reluva::awe {

struct GameState {
    std::string act;                 // "06a"; empty before the first
    std::map<std::string, int> values; // moving, weather, master...
    std::set<std::string> tags;      // story flags set by scripts
    std::set<std::string> items;     // what Poirot carries
};

// Whether `condition` holds. An empty one does; "0" never does. Words not
// known here count as 0 (or as false, for those taking a name).
bool testCondition(std::string_view condition, const GameState& state);

} // namespace reluva::awe
