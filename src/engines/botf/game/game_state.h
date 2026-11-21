// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/botf/formats/save_game.h"

#include <filesystem>
#include <unordered_map>
#include <vector>

// A game in progress, gathered in one place for the screens and, later,
// the rules to work on: everything a saved game holds, with the questions
// they ask of it (docs/engines/botf/systems/game-state.md).

namespace reluva::botf {

/** A game in progress: a saved game's contents, and lookups into them. */
class GameState {
public:
    /** An empty game: no galaxy, no empires' figures. */
    GameState() = default;
    /** The game a saved game holds, already read. */
    explicit GameState(SavedGame game);
    /** Reads a saved game's every part (readSavedGame()). */
    static Error load(const std::filesystem::path &path, GameState &out);

    /** Everything read, as the save holds it. */
    const SavedGame &data() const { return _game; }
    /** The player's empire, 0 Cardassians to 4 Romulans. */
    int player() const;
    /** The turn as the game shows it, from 1. */
    int turn() const { return _game.summary.turn + 1; }
    /** The galaxy, its sectors and what's in them. */
    const SavedGalaxy &galaxy() const { return _game.galaxy; }

    /** Whether column `x` and row `y` is a sector of the grid. */
    bool onGrid(int x, int y) const;
    /** What's in a sector; each null or empty off the grid or where there's none. */
    ///@{
    const SavedSystem *systemAt(int x, int y) const;
    const SavedStellarObject *stellarObjectAt(int x, int y) const;
    std::vector<const SavedTaskForce *> taskForcesAt(int x, int y) const;
    std::vector<const SavedOutpost *> outpostsAt(int x, int y) const;
    ///@}
    /** A ship by its number (SavedShip::id), or null. */
    const SavedShip *ship(int id) const;
    /**
     * Whether `empire` may go into a sector: not one claimed by an empire
     * whose territory it has no access to (empsInfo's, as the galaxy
     * screen's ranges show).
     */
    bool mayEnter(int empire, int x, int y) const;
    /** How many of the last turn's results count as `empire`'s events. */
    int eventCount(int empire) const;

private:
    SavedGame _game;
    std::unordered_map<int, size_t> _shipById; // into _game.ships
};

} // namespace reluva::botf
