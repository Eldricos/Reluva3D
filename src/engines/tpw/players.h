// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <vector>
#include <filesystem>
#include <string>

namespace reluva::tpw {

/** A player's record, as the Personnel File shows it. */
struct PlayerFile {
    /** A world's line: its name's number in `THEMENAMES.str`, and how its challenges stand. */
    struct World {
        int theme = 0;
        bool open = false;
        int won = 0;
        int challenges = 0;
    };

    std::string name; // empty for a slot with no player
    int rank = 1;     // in PROMOTIONLEVELS.str: 1 "Assistant Manager"
    int goldenTickets = 0;
    std::vector<int> challengesWon; // by their numbers in Challenges.sam
    int challengesFailed = 0;
    std::vector<int> ridesActivated; // special rides, by their objects' numbers
    int sharesOwned = 0;
    int sharesIssued = 0;
    int sharePrice = 0;
    std::array<World, 3> worlds{};
    int objective = -1;     // in OPERATION.str, 0 "Operation Expand!"; −1 none yet
    int objectiveTheme = 0; // where it is, in THEMENAMES.str

    bool exists() const { return !name.empty(); }
};

/** How many players the Personnel File has room for: its tabs. */
constexpr int kPlayerSlots = 4;

using PlayerSlots = std::array<PlayerFile, kPlayerSlots>;

/**
 * The players are kept by Reluva in the game's save folder, in a file of
 * its own, as the game's own player files aren't known. See
 * docs/engines/tpw/games/simcoaster/systems/players.md.
 */
constexpr const char *kPlayersFile = "players.txt";

/**
 * Reads the players from `folder` (none for an empty path, or without the
 * file): a slot with no player is `fresh` with no name; a player's numbers
 * the file hasn't are `fresh`'s.
 */
PlayerSlots readPlayers(const std::filesystem::path &folder, const PlayerFile &fresh);

/** Writes the players to `folder`, made if need be. Fails for an empty path, or if it can't write. */
Error writePlayers(const std::filesystem::path &folder, const PlayerSlots &players);

} // namespace reluva::tpw
