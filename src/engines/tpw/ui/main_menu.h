// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/system.h"
#include "engines/tpw/players.h"

#include <filesystem>

namespace reluva::tpw {

/**
 * The front end, from after the opening to a world chosen: the camera's
 * path through the hall and boardroom (front-end.md), the globe turning
 * under the advisor's portrait until play is pressed, the Personnel File
 * over the globe turning (personnel-file.md), then choosing a world on it
 * (globe.md), in docs/engines/tpw/games/simcoaster/systems/. Without the
 * scene, the Personnel File alone, over black, then the first world the
 * player has open.
 * @param saveFolder Where the players are kept (players.md); none kept for
 *     an empty path.
 * @param mixer Where the script's sound effects play; nullptr for silence.
 * @param chosen Given the player chosen, if not nullptr.
 * @param loadSave Given the saved game the player chose to load from the
 *     Personnel File, if not nullptr; the globe's then passed by.
 * @return The world chosen, by Globe::worlds()' order (0 for a saved
 *     game, whose world is its own); −1 if the player
 *     quit, or the window was closed (or the launcher asked for).
 */
int runFrontEnd(System &sys, const SearchSet &files, const std::filesystem::path &saveFolder,
                audio::Mixer *mixer = nullptr, PlayerFile *chosen = nullptr, std::filesystem::path *loadSave = nullptr);

} // namespace reluva::tpw
