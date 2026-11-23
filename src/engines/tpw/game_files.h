// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "image/fsh.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The game's `Data` folder as one set of files, named from it: each
 * `.wad` stands for the folder of its name (`levels/water/rides/TourRide.wad`
 * holds `levels/water/rides/TourRide/...`), with the loose files over them.
 * See docs/engines/tpw/games/simcoaster/formats/wad.md.
 * @param warnings Gets a line for each archive that couldn't be read,
 *     which is left out.
 * @return Fails if the install has no `Data` folder.
 */
Error mountGame(SearchSet &set, const std::filesystem::path &install, std::vector<std::string> &warnings);

/**
 * The language the game's files are in, as its folder in `Data/Language/`
 * names it ("American"), found by the character set there; empty if
 * there's none.
 */
std::string languageOf(const SearchSet &files);

/**
 * How the game's `.fsh` banks are read: their 24-bit palettes are R, G, B
 * (docs/engines/tpw/games/simcoaster/formats/fsh.md).
 */
inline constexpr image::ShapeOptions kShapeOptions{true};

} // namespace reluva::tpw
