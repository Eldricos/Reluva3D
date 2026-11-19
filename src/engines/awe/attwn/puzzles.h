// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/formats/ac2.h"

#include <array>
#include <string>

// And Then There Were None's puzzle screens that Orient Express hasn't:
// what each keeps while it's open. ScenePage's puzzle hooks run them
// (attwn/puzzles.cpp); the safe is Orient Express's with this game's
// rules (GameInfo::safe). See docs/engines/awe/games/attwn/systems/puzzles.md.

namespace reluva::awe::attwn {

/**
 * The safe room's deposit boxes (ac.exe 0x408de0): twelve drawers with a
 * letter each; two opened swap places.
 */
struct DepositBox {
    std::array<std::string, 12> letters; ///< each slot's picture ("r.jpg"), in the grid's order
    std::array<bool, 12> closed{};       ///< its letter shows
    float timer = 0;                     ///< till two open drawers swap
};

} // namespace reluva::awe::attwn
