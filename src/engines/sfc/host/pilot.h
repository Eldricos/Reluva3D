// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/sfc/host/battle.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace reluva::sfc {

/**
 * Flies the player's ship in a battle by the game's own keys and clicks,
 * choosing them from what the battle holds each moment (Battle): for
 * testing the missions headless, where inputs fixed in advance lose any
 * fight that needs answering. It targets the game's nearest enemy and
 * steers for it, turns away when too close, reinforces the shield facing
 * it and fires when it's ahead and in range.
 * (docs/engines/sfc/games/sfc2/systems/battle.md, The pilot)
 */
class Pilot {
public:
    /** One input: a key (Reluva's key code), or a click on the screen where key is 0. */
    struct Action {
        uint32_t key = 0;
        int x = 0;
        int y = 0;
    };

    /** The pilot of the program running on this CPU. */
    explicit Pilot(const x86::Cpu &cpu) : _battle(cpu) {}

    /**
     * The input for this frame, pressed now and released at the next;
     * called each frame the game shows.
     * @param frame frames shown since the program started
     */
    std::optional<Action> step(uint64_t frame);
    /** A line saying what it saw and chose last, for a log; empty when nothing changed. */
    const std::string &note() const { return _note; }

private:
    /** What to do now, by what the battle holds; queued, as step gives one a frame. */
    std::vector<Action> decide(uint64_t frame);
    /** Ships the game counts against the player's side. */
    static std::set<int> hostiles(const std::vector<Battle::Object> &ships, int player);

    Battle _battle;
    std::deque<Action> _queue;
    std::string _note;
    std::string _lastNote;
    int _playerId = -1;    ///< the ship it's flying
    bool _started = false; ///< red alert and speed given
    bool _away = false;    ///< turned away from the target, until it's far enough again
    int _shield = -1;      ///< the shield last reinforced: 0 forward, round to the right
    uint64_t _lastFire = 0;
    uint64_t _lastSelect = 0;
    uint64_t _lastSteer = 0;
    uint64_t _lastSpeed = 0;
};

} // namespace reluva::sfc
