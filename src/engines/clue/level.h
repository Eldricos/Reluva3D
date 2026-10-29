// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Clue's objects (.o) and its mansion (clue.lvl). See docs/formats/clue/level.md.

#include "common/types.h"
#include "engines/clue/resources.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::clue {

// A 7-byte timing item, in objects and placements: perhaps "after time
// milliseconds, go to state". Only the readme's clickable objects have them.
struct Timing {
    uint16_t time = 0;
    uint16_t state = 0;
    std::array<uint8_t, 7> raw{};
};

// An object type, from a .o file: its states, each an animation.
struct ObjectDef {
    // What an object is, by its type byte, as far as the objects of each
    // type tell (3, 5, 9 and 12 are all furniture of some kind).
    enum Type : uint8_t { Wall = 0, Floor = 1, Character = 6, SecretPassage = 8, TileNumber = 11 };
    struct State {
        // 0 the usual one; 1-16 a character's eight idle and eight "mult"
        // ones (a direction each); 19 its pawn; 20-24 an object's others.
        uint16_t id = 0;
        std::string animation; // "c_scarlet:Amsidlee_"
    };

    std::string name; // "ballroom:Oblcouch_ad"
    uint8_t type = 0;
    uint32_t unknown = 0;
    std::vector<State> states;
    std::vector<Timing> timings;

    const State* state(uint16_t id) const;
};
Error parseObject(const std::vector<uint8_t>& record, ObjectDef& out);

// An object placed in the mansion.
struct Placement {
    uint8_t flags = 0;    // 0x40 always; 0x02: tag given; 0x20: timings given
    uint16_t tag = 0;     // when flags has 0x02; not understood
    uint16_t object = 0;  // into Level::objects
    int16_t x = 0, y = 0; // the object's anchor, in the level's units: twice the world's
    int16_t id = 0;       // -1 for floors; 1 for most things; not understood
    uint8_t layer = 0;    // 0-1 floors, 3 walls, 4 the walls nearest the camera, 5 furniture,
                          // 6 the black outside, 7 characters
    std::vector<Timing> timings;

    int worldX() const { return x / 2; }
    int worldY() const { return y / 2; }
};

struct Level {
    std::string title;                // "Unnamed"
    std::vector<std::string> objects; // "ballroom:Oblcouch_ad", by number
    std::vector<Placement> placements;
    std::vector<uint8_t> info, workspace; // the level editor's, it seems
};
// clue.lvl is a DDRV archive of /World.dat, /Objects.dat, /Info.dat and
// /Workspace.dat.
Error parseLevel(const DataDrive& drive, Level& out);

} // namespace reluva::clue
