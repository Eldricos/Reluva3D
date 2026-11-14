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
 * "inc needed|possible <act>..." (ac.exe 0x40bdad): if the act is one of
 * those named, its counter <act>_neededcounter or <act>_possiblecounter
 * (0 if unset) goes up by 1. The chapters' tables end an act on them.
 * @return False if the verb isn't one of the game's own.
 */
bool verb(GameState &state, const std::vector<std::string> &words);

} // namespace reluva::awe::attwn
