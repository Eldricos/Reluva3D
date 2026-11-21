// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// A saved game (game0.sav to game11.sav, one per slot, and auto.sav): what
// the load and save screens show of it, and its galaxy, the map the game is
// played on (docs/engines/botf/formats/sav.md).

namespace reluva::botf {

/** A saved game, as its slot shows it. */
struct SaveSummary {
    std::string name;        // up to 15 characters, UTF-8
    std::string description; // up to 50 characters, UTF-8
    int turn = 0;            // 0 is the first
    int empire = -1;         // the player's, 0 Cardassians to 4 Romulans; -1 not known
    bool multiplayer = false;
};

/** Reads `summary` from the saved game at `path`. */
Error readSaveSummary(const std::filesystem::path &path, SaveSummary &summary);

/** A planet of a star system, as the galaxy was made. */
struct SavedPlanet {
    std::string name;      // UTF-8: "Acamar I"
    std::string animation; // its picture's file in stbof.res: "vsal.ani"
    int size = 0;          // 0 small, 1 medium, 2 large
    int type = 0;          // the planet's type, a number (docs/engines/botf/formats/sav.md)
    int atmosphere = 0;    // its atmosphere, a number
};

/** A star system: a sector's star and its planets. */
struct SavedSystem {
    std::string name;          // UTF-8: "Acamar"
    std::string starAnimation; // the star's pictures' name: "s-o-"
    int starType = 0;          // the star's type, a number (docs/engines/botf/formats/sav.md)
    int control = 0;           // 2 for the player's; 0 or 1 for the others, by rules not yet known
    int population = 0;        // as the game counts it
    int residentRace = -1;     // race.rst's number of the people living there, or -1
    int controllingRace = -1;  // race.rst's number of who holds it, or -1 for none
    int dilithium = 0;         // natural dilithium, 0 to 5
    /** Where it's drawn on the galaxy's small map, in its pixels: 5 a sector. */
    ///@{
    int mapX = 0;
    int mapY = 0;
    ///@}
    std::vector<SavedPlanet> planets;
};

/** A stellar object: a nebula, a black hole, a pulsar or a wormhole. */
struct SavedStellarObject {
    int type = 0;     // 0 black hole, 1 X-ray pulsar, 2 nebula, 10 wormhole, 11 radio pulsar
    std::string name; // UTF-8: "Black Hole C4", by its sector's column and row
    /** Where it's drawn on the galaxy's small map, as SavedSystem's. */
    ///@{
    int mapX = 0;
    int mapY = 0;
    ///@}
};

/** One square of the galaxy's grid. */
struct SavedSector {
    int system = -1;        // an index into SavedGalaxy::systems, or -1
    int stellarObject = -1; // into SavedGalaxy::stellarObjects, or -1
    int starbase = -1;      // the outpost or starbase there, or -1
    uint16_t claims = 0;    // the empires claiming it: bit 0 Cardassians to bit 4 Romulans
    uint64_t explored = 0;  // the races that have explored it, a bit each by race.rst's number
};

/** A saved game's galaxy: its grid of sectors, and what's in them. */
struct SavedGalaxy {
    int size = 0; // 0 small, 1 medium, 2 large
    /** The grid's size, in sectors. */
    ///@{
    int width = 0;
    int height = 0;
    ///@}
    std::vector<SavedSector> sectors; // row by row from the top left
    std::vector<SavedSystem> systems;
    std::vector<SavedStellarObject> stellarObjects;

    /** The sector at column `x` and row `y`, which must be in the grid. */
    const SavedSector &at(int x, int y) const { return sectors[size_t(y * width + x)]; }
};

/**
 * Reads the galaxy of the saved game at `path`: its galInfo, sector.lst,
 * systInfo and stellInfo files.
 * @return An error if one is missing, or they don't agree with each other.
 */
Error readSavedGalaxy(const std::filesystem::path &path, SavedGalaxy &galaxy);

} // namespace reluva::botf
