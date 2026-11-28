// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/play/strategy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace reluva::storm3d {

Strategy::Personality Strategy::personalityOf(const std::string &file, const ConfigFile::Loader &loader) {
    return personalityOf(file, loader, Personality{});
}

Strategy::Personality Strategy::personalityOf(const std::string &file, const ConfigFile::Loader &loader,
                                              const Personality &base) {
    Personality p = base;
    ConfigFile config;
    if (config.load(file, loader))
        return p;
    p.attackTroops = config.number("attack_troops_priority", p.attackTroops);
    p.attackEnemyBase = config.number("attack_enemy_base_priority", p.attackEnemyBase);
    p.defendBuildings = config.number("defend_buildings_priority", config.number("defend_priority", p.defendBuildings));
    p.exploration = config.number("exploration_priority", p.exploration);
    p.perimeter = config.number("perimeter_priority", p.perimeter);
    p.distanceFromHome = config.number("distance_from_home_priority_modifier", p.distanceFromHome);
    p.distanceFromEnemy = config.number("distance_from_enemy_priority_modifier", p.distanceFromEnemy);
    p.distanceFromGoal = config.number("distance_from_goal_match_modifier", p.distanceFromGoal);
    return p;
}

std::vector<Strategy::Goal> Strategy::goalsOf(const std::vector<Cell> &cells, int across, int down,
                                              const Personality &p) {
    // Each cell's distance, in cells, to the nearest with our stations and
    // the nearest with the enemy's; none of them, none.
    auto nearest = [&](bool ours, int x, int z) {
        double best = std::numeric_limits<double>::max();
        for (int j = 0; j < down; ++j)
            for (int i = 0; i < across; ++i) {
                const Cell &c = cells[size_t(j * across + i)];
                if (ours ? c.ourStations > 0 : c.enemyStations > 0)
                    best = std::min(best, std::hypot(double(i - x), double(j - z)));
            }
        return best == std::numeric_limits<double>::max() ? 0.0 : best;
    };
    std::vector<Goal> goals;
    for (int z = 0; z < down; ++z)
        for (int x = 0; x < across; ++x) {
            const Cell &c = cells[size_t(z * across + x)];
            const bool edge = x == 0 || z == 0 || x == across - 1 || z == down - 1;
            const double base = nearest(true, x, z) * p.distanceFromHome + nearest(false, x, z) * p.distanceFromEnemy +
                                (edge ? p.perimeter : 0);
            const double threat = c.enemyShips * p.attackTroops;
            if (c.enemyStations == 0 && c.enemyShips > 0)
                goals.push_back({Kind::kAttack, x, z, base + threat, c.enemyShields});
            if (c.ourStations > 0)
                goals.push_back(
                    {Kind::kDefend, x, z, base + threat + c.ourStations * p.defendBuildings, c.enemyShields});
            if (c.enemyStations > 0)
                goals.push_back(
                    {Kind::kAttackBase, x, z, base + threat + c.enemyStations * p.attackEnemyBase, c.enemyShields});
            if (!c.explored)
                goals.push_back({Kind::kExplore, x, z, base + p.exploration, 0});
        }
    return goals;
}

std::vector<std::optional<size_t>> Strategy::assign(const std::vector<Ship> &ships, const std::vector<Goal> &goals,
                                                    const Personality &p) {
    struct Match {
        double utility;
        size_t ship;
        size_t goal;
    };
    std::vector<Match> matches;
    for (size_t s = 0; s < ships.size(); ++s)
        for (size_t g = 0; g < goals.size(); ++g) {
            const double cx = (goals[g].x + 0.5) * kCell;
            const double cz = (goals[g].z + 0.5) * kCell;
            const double cells = std::hypot(cx - ships[s].x, cz - ships[s].z) / kCell;
            matches.push_back({goals[g].priority + cells * p.distanceFromGoal, s, g});
        }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const Match &a, const Match &b) { return a.utility > b.utility; });
    std::vector<std::optional<size_t>> to(ships.size());
    std::set<std::pair<size_t, size_t>> refused; // a ship taken back from a goal isn't offered it again
    auto enough = [&](size_t g, const std::vector<double> &shields, const std::vector<int> &count) {
        return goals[g].kind == Kind::kExplore ? count[g] >= 1 : count[g] > 0 && shields[g] > goals[g].needed;
    };
    for (int round = 0; round < 8; ++round) {
        std::vector<double> shields(goals.size(), 0);
        std::vector<int> count(goals.size(), 0);
        for (size_t s = 0; s < ships.size(); ++s)
            if (to[s]) {
                shields[*to[s]] += ships[s].shields;
                ++count[*to[s]];
            }
        // Best matches first: a ship not yet committed goes to a goal that
        // still wants more.
        for (const Match &m : matches) {
            if (to[m.ship] || refused.count({m.ship, m.goal}) || enough(m.goal, shields, count))
                continue;
            to[m.ship] = m.goal;
            shields[m.goal] += ships[m.ship].shields;
            ++count[m.goal];
        }
        // Those at goals still short are taken back, to try elsewhere.
        bool changed = false;
        for (size_t s = 0; s < ships.size(); ++s)
            if (to[s] && !enough(*to[s], shields, count)) {
                refused.insert({s, *to[s]});
                to[s].reset();
                changed = true;
            }
        if (!changed)
            break;
    }
    return to;
}

} // namespace reluva::storm3d
