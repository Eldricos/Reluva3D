// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "engines/stgenerations/formats/resources.h"

namespace reluva::stgenerations {

// The away-team locations, as sgens.exe lists them (0x545600: records of
// 132 bytes, a game class number, the PROCEED button's rectangle and the
// folder). Each is a folder of a data archive holding GAME.LV2, GAME.BIN
// and the rest.
struct Location {
    // The folder, e.g. "VERID1".
    const char *folder;
    // The game class the original builds for it (0x4331FA).
    int type;
    // Where PROCEED.BMP goes on its BRIEFING.BMP (0x461F24): the top left.
    int proceedX, proceedY;
};

const std::vector<Location> &getLocations();

// Find a location's GAME.LV2 among the resources; false if it isn't installed.
bool findLocationLevel(const ResourceManager &resources, const Location &location, ResourceRef &ref);

} // namespace reluva::stgenerations
