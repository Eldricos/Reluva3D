// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/config_file.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * Where a computer player sends its ships: the strategic map's goals,
 * scored by its personality and matched to its ships, as the game's own
 * description of its scheduler (AI/AIPs/Scheduler_README.txt) has it.
 * See docs/engines/storm3d/games/armada/systems/computer-player.md.
 */
class Strategy {
public:
    /** A personality's priorities and modifiers (an .aip's), by the README's names. */
    struct Personality {
        double attackTroops = 1000;     // attack_troops_priority
        double attackEnemyBase = 30;    // attack_enemy_base_priority
        double defendBuildings = 100;   // defend_buildings_priority (defend_priority without it)
        double exploration = 500;       // exploration_priority
        double perimeter = 10;          // perimeter_priority
        double distanceFromHome = 0;    // distance_from_home_priority_modifier
        double distanceFromEnemy = 10;  // distance_from_enemy_priority_modifier
        double distanceFromGoal = -0.5; // distance_from_goal_match_modifier
    };
    /** A personality from its file; the defaults where it says nothing. */
    static Personality personalityOf(const std::string &file, const ConfigFile::Loader &loader);

    /** The strategic map's cell, 400 across (strategic_AI_config.txt's goal_map_resolution). */
    static constexpr float kCell = 400;

    /** What a cell of the strategic map holds, for one player. */
    struct Cell {
        int ourStations = 0;
        int enemyStations = 0;
        int enemyShips = 0;      // the threat there, counted (the README doesn't say how it's measured)
        double enemyShields = 0; // and their shields, which a goal there wants beaten
        bool explored = false;
    };
    enum class Kind { kAttack, kDefend, kAttackBase, kExplore };
    /** A goal at a cell, and its raw priority. */
    struct Goal {
        Kind kind = Kind::kExplore;
        int x = 0;
        int z = 0;
        double priority = 0;
        double needed = 0; // shields it wants committed: the enemy's there; exploring wants one ship
    };
    /**
     * The goals on a map of `across` by `down` cells (row by row), each
     * cell's valid ones and their raw priorities: the sum of its distance
     * in cells to our nearest station and to the enemy's, times their
     * modifiers; the threat times attack_troops_priority (attack, defend,
     * attack base); the perimeter's priority on the map's edge; and
     * defending, our stations times defend_buildings_priority; attacking
     * a base, theirs times attack_enemy_base_priority; exploring,
     * exploration_priority.
     */
    static std::vector<Goal> goalsOf(const std::vector<Cell> &cells, int across, int down, const Personality &p);

    /** A ship to send: where it is (x and z), and its shields. */
    struct Ship {
        float x = 0;
        float z = 0;
        double shields = 0;
    };
    /**
     * Each ship's goal (into `goals`), or none: matches of every ship and
     * goal, by the goal's priority and the ship's distance to it in cells
     * times distance_from_goal_match_modifier, best first; each ship
     * committed to its best goal while one wants more, until the goal has
     * shields enough (one ship to explore); then those committed to goals
     * that didn't get enough, taken back.
     */
    static std::vector<std::optional<size_t>> assign(const std::vector<Ship> &ships, const std::vector<Goal> &goals,
                                                     const Personality &p);
};

} // namespace reluva::storm3d
