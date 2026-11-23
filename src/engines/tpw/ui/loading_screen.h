// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "image/image.h"

#include <string>

namespace reluva::tpw {

/**
 * The loading screen, as the game shows it while a park loads, put
 * together from its pieces in `Data/Init/640/`: the scene (`welcome.png`),
 * the game's logo over its top left (`welcome_<language>.png`), the legal
 * text over its bottom right (`Legal_<language>.png`), and, for a
 * language that has one, the Bullfrog and QSound logos over its bottom
 * left (`bull_<language>.png`). See
 * docs/engines/tpw/games/simcoaster/systems/loading-screen.md.
 * @param language As languageOf gives it, "American".
 * @return Fails without the scene; a piece that is missing is left out.
 */
Error composeLoadingScreen(const SearchSet &files, const std::string &language, image::Image &out);

} // namespace reluva::tpw
