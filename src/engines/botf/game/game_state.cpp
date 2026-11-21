// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/game/game_state.h"

#include <algorithm>
#include <utility>

namespace reluva::botf {

namespace {

// result.lst's contents for a task force and a ship.
constexpr int kTaskForceResult = 4;
constexpr int kShipResult = 5;

} // namespace

GameState::GameState(SavedGame game) : _game(std::move(game)) {
    for (size_t i = 0; i < _game.ships.size(); ++i)
        _shipById.emplace(_game.ships[i].id, i);
}

Error GameState::load(const std::filesystem::path &path, GameState &out) {
    SavedGame game;
    if (Error err = readSavedGame(path, game))
        return err;
    out = GameState(std::move(game));
    return Error::success();
}

int GameState::player() const {
    return std::clamp(_game.summary.empire, 0, 4);
}

bool GameState::onGrid(int x, int y) const {
    return x >= 0 && y >= 0 && x < _game.galaxy.width && y < _game.galaxy.height;
}

const SavedSystem *GameState::systemAt(int x, int y) const {
    if (!onGrid(x, y))
        return nullptr;
    const int s = _game.galaxy.at(x, y).system;
    return s >= 0 && size_t(s) < _game.galaxy.systems.size() ? &_game.galaxy.systems[size_t(s)] : nullptr;
}

const SavedStellarObject *GameState::stellarObjectAt(int x, int y) const {
    if (!onGrid(x, y))
        return nullptr;
    const int o = _game.galaxy.at(x, y).stellarObject;
    return o >= 0 && size_t(o) < _game.galaxy.stellarObjects.size() ? &_game.galaxy.stellarObjects[size_t(o)] : nullptr;
}

std::vector<const SavedTaskForce *> GameState::taskForcesAt(int x, int y) const {
    std::vector<const SavedTaskForce *> out;
    for (const SavedTaskForce &t : _game.taskForces)
        if (t.x == x && t.y == y)
            out.push_back(&t);
    return out;
}

std::vector<const SavedOutpost *> GameState::outpostsAt(int x, int y) const {
    std::vector<const SavedOutpost *> out;
    if (!onGrid(x, y))
        return out;
    const int number = y * _game.galaxy.width + x;
    for (const SavedOutpost &o : _game.galaxy.outposts)
        if (o.sector == number)
            out.push_back(&o);
    return out;
}

const SavedShip *GameState::ship(int id) const {
    const auto it = _shipById.find(id);
    return it == _shipById.end() ? nullptr : &_game.ships[it->second];
}

bool GameState::mayEnter(int empire, int x, int y) const {
    if (!onGrid(x, y))
        return false;
    if (empire < 0 || empire >= int(_game.empires.size()))
        return true;
    const SavedEmpire &e = _game.empires[size_t(empire)];
    const uint16_t claims = _game.galaxy.at(x, y).claims;
    for (int other = 0; other < int(e.territoryAccess.size()); ++other)
        if (other != empire && (claims >> other & 1) && !e.territoryAccess[size_t(other)])
            return false;
    return true;
}

int GameState::eventCount(int empire) const {
    // The empire's results, but for a new game's own fleets (a task force's
    // or a ship's, with an event value of 0): as the galaxy screen's panel
    // counts them (docs/engines/botf/systems/galaxy-screen.md).
    int count = 0;
    for (const SavedResult &r : _game.results)
        if (r.race == empire && !(r.eventValue == 0 && (r.content == kTaskForceResult || r.content == kShipResult)))
            ++count;
    return count;
}

} // namespace reluva::botf
