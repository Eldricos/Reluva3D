// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A saved game, in a file of Reluva's own (the game's `.TPWS` being its
 * memory written out, not worked out): whose it is, its name, the world
 * and the date, then the park's state and the screen's. See
 * docs/engines/tpw/games/simcoaster/formats/reluva-saves.md.
 */
struct SavedGame {
    std::string name;            // as the player named it
    std::string player;          // whose
    std::string world;           // its folder in Data: levels/science
    int mission = 0;             // the player's objective as it was saved, which the park's set up for
    uint32_t day = 0;            // its date, the park's day
    std::vector<uint8_t> park;   // Park::saveState()
    std::vector<uint8_t> screen; // ParkScreen::saveState()
};

/** A save in the folder, as its list shows it. */
struct SaveSlot {
    std::filesystem::path file;
    std::string name;
    uint32_t day = 0;
};

/** The folder of saved games, in the game's save folder. */
std::filesystem::path savesFolder(const std::filesystem::path &saveFolder);

/** Writes a save, over any there. */
Error writeSave(const std::filesystem::path &file, const SavedGame &save);

/** Reads one; with `headerOnly`, all but its states. Fails on a file that isn't one, or is cut short. */
Error readSave(const std::filesystem::path &file, SavedGame &out, bool headerOnly = false);

/** A player's saves in the folder, newest first. */
std::vector<SaveSlot> listSaves(const std::filesystem::path &folder, const std::string &player);

/** A file for a new save in the folder: the first `save<n>.rsv` not there. */
std::filesystem::path newSaveFile(const std::filesystem::path &folder);

} // namespace reluva::tpw
