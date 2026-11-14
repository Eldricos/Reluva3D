// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/engine.h"
#include "engines/stgenerations/game/generations.h"

#include <functional>
#include <memory>

namespace reluva::stgenerations {

/** Makes what an Engine runs: the game, or the data viewer, a Generations too. */
using MakeGame = std::function<std::unique_ptr<Generations>(Platform *platform, Options options)>;

/**
 * Runs what `make` makes for `target` as the launcher's settings for the
 * game set it up (reluva.ini): its options, the console output, the mixer
 * and its volumes. The game's Engine runs the game with it, the data
 * viewer's the viewer.
 */
Error runGame(System &system, const GameTarget &target, const MakeGame &make);

} // namespace reluva::stgenerations
