// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
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
    /** Terraforming: the points it needs and those done; it's terraformed once done reaches needed. */
    ///@{
    int terraformNeeded = 0;
    int terraformDone = 0;
    ///@}
};

/** A star system: a sector's star and its planets. */
struct SavedSystem {
    std::string name;          // UTF-8: "Acamar"
    std::string starAnimation; // the star's pictures' name: "s-o-"
    int starType = 0;          // the star's type, a number (docs/engines/botf/formats/sav.md)
    int control = 0;           // 2 for the player's, 1 or 0 for the others (docs/engines/botf/formats/sav.md)
    int population = 0;        // as the system screen shows it: Sol 310 at the start
    int maxPopulation = 0;     // the same: Sol 335
    int residentRace = -1;     // race.rst's number of the people living there, or -1
    int controllingRace = -1;  // race.rst's number of who holds it, or -1 for none
    int dilithium = 0;         // natural dilithium, 0 to 5
    /**
     * The population working in industry, energy, food, research and
     * intelligence, in that order: 10 for each building manned.
     */
    std::array<int, 5> workers{};
    int buildOrder = 0;  // what it's building: 1 a building, 3 a ship, 5 trade goods (others not yet seen)
    int building = 0;    // with build order 1, the building's edifice.bst number
    int ship = 0;        // with build order 3, the ship's shiplist.sst number
    bool buying = false; // the player's: the item being built is to be bought this turn
    /** Where it's drawn on the galaxy's small map, in its pixels: 5 a sector. */
    ///@{
    int mapX = 0;
    int mapY = 0;
    ///@}
    std::vector<SavedPlanet> planets;
};

/** A stellar object: a nebula, a black hole, a pulsar or a wormhole. */
struct SavedStellarObject {
    int type = 0;     // 0 black hole, 1 X-ray pulsar, 2 nebula, 3 neutron star, 10 wormhole, 11 radio pulsar
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

/** A major empire's state, as the galaxy screen's empire panel shows it. */
struct SavedEmpire {
    std::string name;   // UTF-8: "the United Federation of Planets"
    std::string leader; // UTF-8: "the Federation President"
    int credits = 0;    // its treasury now, after what's been bought this turn
    int creditsPerTurn = 0;
    int dilithiumPerTurn = 0;
    int researchPoints = 0; // a turn's research, all fields together
    int population = 0;     // all its systems'
};

/** Reads the five major empires, Cardassians to Romulans, from the saved game at `path` (its empsInfo file). */
Error readSavedEmpires(const std::filesystem::path &path, std::array<SavedEmpire, 5> &empires);

/** A task force: ships moving together, a single ship being one. */
struct SavedTaskForce {
    int id = 0;
    int owner = 0; // race.rst's number of the race it belongs to
    int ships = 0;
    std::vector<int> shipIds; // its ships, by SavedShip::id
    /** The sector it's in, column and row. */
    ///@{
    int x = 0;
    int y = 0;
    ///@}
    /** The sector it's ordered to, column and row; -1 for none. */
    ///@{
    int destinationX = -1;
    int destinationY = -1;
    ///@}
    int mission = 0; // its orders' number (docs/engines/botf/formats/sav.md)
};

/** A ship. */
struct SavedShip {
    std::string name;   // UTF-8: "USS Martynov A"
    int id = 0;         // its number: ShipCntr counts them, so ships lost leave gaps
    int race = 0;       // race.rst's number of the race it belongs to
    int shipClass = 0;  // its class's shiplist.sst number: 35, the Oberth class Science Vessel
    int taskForce = 0;  // SavedTaskForce::id of the task force it's in
    int shields = 0;    // its class's shields when whole
    int hull = 0;       // its hit points, its class's when whole
    int experience = 0; // its crew's: 350 for the Klingons' at the start, 0 for the others'
};

/** Reads the ships of the saved game at `path` (its GShipList file). */
Error readSavedShips(const std::filesystem::path &path, std::vector<SavedShip> &ships);

/**
 * Reads the task forces of the saved game at `path` (its GWTForce file),
 * placing their destinations on a grid `width` sectors wide (the galaxy's).
 */
Error readSavedTaskForces(const std::filesystem::path &path, int width, std::vector<SavedTaskForce> &taskForces);

/** An order given this turn, by the player or the computer, carried out when the turn ends. */
struct SavedOrder {
    /** What it orders; the numbers are the game's. */
    enum class Type { kEconomy = 0, kResearch = 1, kMilitary = 2, kDiplomacy = 5, kLabor = 7 };
    int id = 0;
    int turn = 0;       // the turn it was given, 0 the first
    int race = 0;       // race.rst's number of the race that gave it
    int type = 0;       // a Type, or another number
    int system = -1;    // economy and labor: the system's index in SavedGalaxy::systems
    int buy = 0;        // economy: 0 to build, 1 to buy what's being built
    int buildOrder = 0; // economy, building: as SavedSystem's
    int building = -1;  // and its building, or -1
    int ship = -1;      // and its ship, or -1
    int taskForce = -1; // military: the task force's id
    int mission = 0;    // and its mission, as SavedTaskForce's (0 to move)
    /** Military: the sector it's sent to, column and row. */
    ///@{
    int destinationX = -1;
    int destinationY = -1;
    ///@}
    /**
     * Labor: the system's workers, as SavedSystem::workers (the last 0);
     * research: the empire's shares, as SavedResearch's.
     */
    std::array<int, 6> values{};
};

/** Reads the orders of the turn the saved game at `path` was saved in (its ordInfo file). */
Error readSavedOrders(const std::filesystem::path &path, std::vector<SavedOrder> &orders);

/**
 * How each major empire, Cardassians to Romulans, shares its research out
 * among biotech, computers, construction, energy, propulsion and weapons:
 * a percentage each, as the research screen shows them.
 */
using SavedResearch = std::array<std::array<int, 6>, 5>;

/** Reads each major empire's research shares from the saved game at `path` (its techinfo file). */
Error readSavedResearch(const std::filesystem::path &path, SavedResearch &research);

/** What the galaxy screen shows of a saved game. */
struct SavedGame {
    SaveSummary summary;
    SavedGalaxy galaxy;
    std::array<SavedEmpire, 5> empires;
    std::vector<SavedShip> ships;
    std::vector<SavedTaskForce> taskForces;
};

/** Reads `game` from the saved game at `path`: each of its parts, as the functions above. */
Error readSavedGame(const std::filesystem::path &path, SavedGame &game);

} // namespace reluva::botf
