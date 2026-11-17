// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/raven/savegame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3::eliteforce {
class SpGameHost;
}

namespace reluva::idtech3::jk2 {

/**
 * Jedi Outcast single player's saved games, as jk2sp.exe 1.02a writes
 * them (docs/engines/idtech3/games/jk2/systems/sp-saves.md): Raven's
 * chunks, compressed where that makes them shorter, in the engine's two
 * steps each: what comes before the level starts, then the level.
 */
struct SaveHeader {
    std::string comment; // up to 63 letters
    int32_t time = 0;    // when it was saved (seconds since 1970)
    std::string map;
    std::vector<uint8_t> shot; // the picture: a JPEG, 512 x 512
    bool autosave = false;
};

/// The picture's size, and its JPEG quality, as the original's.
constexpr int kShotSize = 512;
constexpr int kShotQuality = 95;

/**
 * The chunks before the level: the version, `header`, the cvars kept with
 * a save (flagged 2), and for an autosave the player carried over (its
 * cvars, which must be set already: SpGameHost::savePlayer()).
 */
void writeSaveHeader(raven::SaveWriter &w, const CvarStore &cvars, const SaveHeader &header);
/// Reads them back, setting the cvars as the original does.
Error readSaveHeader(raven::SaveReader &r, CvarStore &cvars, SaveHeader &out);
/**
 * The level: but for an autosave, its time, area portals and
 * configstrings; then the game's WriteLevel.
 */
Error writeSaveLevel(raven::SaveWriter &w, eliteforce::SpGameHost &game, bool autosave);
/// Reads it back into a level just started from the save (the game reading
/// from `r`: SpGameHost::setSaveStream).
Error readSaveLevel(raven::SaveReader &r, eliteforce::SpGameHost &game, bool autosave);

} // namespace reluva::idtech3::jk2
