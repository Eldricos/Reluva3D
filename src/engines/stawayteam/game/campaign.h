// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/stawayteam/game/objectives.h"
#include "engines/stawayteam/game/team.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * How the game's going from mission to mission: the mission it's at, the
 * score, and the items found (docs/engines/stawayteam/systems/objectives.md).
 */
struct Campaign {
    int mission = 1;
    int score = 0;
    float time = 0.0f;                   // seconds of the missions done
    int difficulty = 2;                  // 1 easy, 2 normal, as the difficulty window has them
    std::vector<std::string> discovered; // items found, by their definitions' names
    std::vector<std::string> toDescribe; // found since the last briefing, for the next to describe
};

/**
 * What a mission completed adds to the score (0x50ca70): its Base Score
 * for Mission, and each objective that isn't required and is complete
 * its Points.
 */
int missionScore(const MissionObjectives &mission, const std::vector<Objective> &objectives);

/**
 * The items not found yet whose Points to Discover the score's reached
 * (0x5b90c0; -1 is found otherwise), in the definitions' order.
 */
std::vector<std::string> foundByScore(const std::map<std::string, ItemDefinition> &items, const Campaign &campaign);

/**
 * A game saved: its name, when (seconds since 1970), the mission's name as
 * shown, and the campaign as the mission began (Reluva saves the mission's
 * start, not the map as it stands).
 */
struct SaveGame {
    std::string name;
    int64_t savedAt = 0;
    std::string missionName;
    Campaign campaign;
};

/** Writes a save to `path`, in Reluva's own text. */
Error writeSaveGame(const std::filesystem::path &path, const SaveGame &save);
/** Reads one written by writeSaveGame. */
Error readSaveGame(const std::filesystem::path &path, SaveGame &out);
/** The saves in `folder` (its `*.sav`), newest first. */
std::vector<std::pair<std::filesystem::path, SaveGame>> listSaveGames(const std::filesystem::path &folder);

} // namespace reluva::stawayteam
