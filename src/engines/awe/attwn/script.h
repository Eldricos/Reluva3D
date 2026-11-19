// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <string>
#include <vector>

// And Then There Were None's own script words, which Orient Express hasn't:
// the shared evaluator and runner (game/conditions.cpp, game/script.cpp)
// ask these for words they don't know. See
// docs/engines/awe/games/attwn/systems/script.md.

namespace reluva::awe {
struct GameState;
} // namespace reluva::awe

namespace reluva::awe::attwn {

/**
 * "suspicion <who> [<|>]n" (ac.exe 0x40a6f1): the tag suspicion_<who>
 * (0 if unset) compared with n; without '<' or '>', equal to it.
 * @param words The clause's words, the first lower case.
 * @return False if the word isn't one of the game's own.
 */
bool condition(const GameState &state, const std::vector<std::string> &words, bool &holds);

/**
 * Whether the machine named is put together (ac.exe 0x40e150), as the
 * condition "test <machine>" asks: its tags all set. Only the apple press
 * and the two radios can be.
 */
bool machineReady(const GameState &state, const std::string &machine);

/**
 * "inc needed|possible <act>..." (ac.exe 0x40bdad): if the act is one of
 * those named, its counter <act>_neededcounter or <act>_possiblecounter
 * (0 if unset) goes up by 1. The chapters' tables end an act on them.
 * "suspicion <who> [+|-]n" (0x40d3df): the tag suspicion_<who> set to n,
 * or moved by it, kept to 0 or 1.
 * "test <machine> [<part>]" (0x40e2d0): the machine tried, by what's been
 * done to it; `then` gets what it runs.
 * @return False if the verb isn't one of the game's own.
 */
bool verb(GameState &state, const std::vector<std::string> &words, std::string &then);

/**
 * "movie coda" (0x40c302): the ending's, by who was saved (the tags
 * 9a_lombardsaved and 10a_verasaved); any other name as it is.
 */
std::string movie(const GameState &state, const std::string &name);

} // namespace reluva::awe::attwn
