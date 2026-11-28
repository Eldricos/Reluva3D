// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/cpu.h"

#include <cstdint>
#include <vector>

namespace reluva::sfc {

/**
 * A battle as Starfleet Command II 1.0 keeps it in memory, read from the
 * running program (docs/engines/sfc/games/sfc2/systems/battle.md): the
 * world's objects by number, where each is, and which is the player's.
 */
class Battle {
public:
    /** One of the world's objects: a ship, a base, a planet, a shuttle. */
    struct Object {
        int id = -1;
        uint32_t address = 0;
        uint32_t kind = 0;  ///< the first word: its class's table of methods
        uint32_t flags = 0; ///< +0x18; bit 0 for an object that moves
        float x = 0;
        float y = 0;
        float z = 0;
        /** The way it faces, a unit vector in the plane (+0x98, +0xa0): as it moves from where it was (+0x40). */
        float headingX = 1;
        float headingY = 0;
        int target = -1;   ///< +0x28: the number of the object it has targeted
        int slot = -1;     ///< +0x60: its place among the mission's teams
        bool ship = false; ///< of the ships' class (0x71bd3c)
        /**
         * How its team stands to the player's, as the game reckons it
         * (0x424fbf): 1 the player's own, 2 allied, 5 neutral (a peace
         * conference's delegates), 6 enemy; 0 when there's no table.
         */
        int relation = 0;
        /** Whether the game counts it on the player's side, as the HUD's "Allied" (0x4a7b43). */
        bool friendly() const { return relation == 1 || relation == 2; }
        /** Whether the game counts it an enemy (seen: raiders, the ISC). */
        bool hostile() const { return relation == 6; }
    };

    /** The battle of the program running on this CPU. */
    explicit Battle(const x86::Cpu &cpu) : _cpu(cpu) {}

    /** Whether a battle is going on: the world has a player's object. */
    bool running() const;
    /** The player's object's number, or -1. */
    int playerId() const;
    /** Every object in the world now. */
    std::vector<Object> objects() const;
    /** The object with this number; its address 0 when there's none. */
    Object object(int id) const;
    /** The range between two objects as the game shows it: a tenth of their distance (0x71be90). */
    static float range(const Object &a, const Object &b);

private:
    float readFloat(uint32_t address) const;
    int relation(int slot) const;

    const x86::Cpu &_cpu;
};

} // namespace reluva::sfc
