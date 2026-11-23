// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/system.h"

#include <filesystem>

namespace reluva::tpw {

/** The screen the opening is shown on, as the game's at 640x480. */
constexpr int kOpeningWidth = 640;
constexpr int kOpeningHeight = 480;

/**
 * The game's opening, as a recording of it shows it
 * (docs/engines/tpw/games/simcoaster/research/footage.md): the EA GAMES
 * movie, stretched to the screen, with its sound; then the Bullfrog
 * splash picture, until a key or a click or 3 seconds. Either is left out
 * if its file isn't there or can't be read.
 * @param mixer The sound's; nullptr to play the movie silently.
 * @return False if the window was closed (or the launcher asked for) meanwhile.
 */
bool playOpening(System &sys, audio::Mixer *mixer, const std::filesystem::path &install, const SearchSet &files);

} // namespace reluva::tpw
