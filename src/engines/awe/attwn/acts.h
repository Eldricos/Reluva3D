// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <string>
#include <vector>

// And Then There Were None's chapters and places: its acts' rows from
// Text/Chapter<NN>.txt, and the place a scene is in. See
// docs/engines/awe/games/attwn/systems/acts.md.

namespace reluva::awe {
struct ActRow;
} // namespace reluva::awe

namespace reluva::awe::attwn {

/**
 * The acts' rows of Text/Chapter01.txt on (ac.exe 0x402be0): Act, Event
 * (run as the act starts), Time of Day and Weather (numbers), Characters
 * in Act, Completion Conditions, Event, Music. No place: the start script
 * shows the first scene.
 */
Error readChapters(const Archive &files, std::vector<ActRow> &out);

/**
 * A scene's place, its folder in Locations (0x403310): its name up to its
 * first '_', less its last two characters ("kitchen01_closeup02":
 * "kitchen").
 */
std::string placeOf(const std::string &scene);

} // namespace reluva::awe::attwn
