// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * A map (`bzn\*.bzn`): the game's saved-game format, holding where a
 * mission or skirmish starts. A text header, then typed fields with no
 * names: the map's settings, its objects, its scripts and its paths. See
 * docs/engines/storm3d/games/armada/formats/bzn.md.
 */
struct BznMap {
    /** A field's kinds of data, by the type in its first byte. */
    enum FieldType : uint8_t {
        kRaw = 0,     // 4 bytes, or 64 of text after a path's points
        kBool = 1,    // one byte
        kText = 2,    // characters, nul-padded to the field's size
        kInt = 4,     // 32-bit, little-endian
        kFloat = 5,   // 32-bit
        kClass = 7,   // an object's definition (.odf), 8 characters
        kPointer = 8, // 4 bytes from the program's memory, meaningless in the file
        kVector = 9,  // floats, 3 a point (a path's are all its points)
        kMatrix = 11, // 12 floats: right, up, front, then position
    };
    /** A field as stored: its type and its bytes. */
    struct Field {
        uint8_t type = 0;
        std::vector<uint8_t> data;
    };
    using Vec3 = std::array<float, 3>;

    /** An object, by the fields every object starts with. */
    struct Object {
        std::string odf;                   // its definition, without ".odf": "fente"
        uint32_t handle = 0;               // a number for it, its top bit set
        Vec3 position{};                   // also the transform's last three
        int32_t team = 0;                  // 0 none, then 1 to 8 (the player's is 1 in the missions)
        std::string label;                 // its name in the game: "Enterprise"
        std::array<float, 12> transform{}; // right, up, front, position
        size_t firstField = 0;             // its fields: from here,
        size_t fieldCount = 0;             // this many (to the next object, or the map's tail)
    };

    /**
     * A player's counters as the map saves them, read for a campaign
     * mission (0x446640): slot 0, then 1 to 8.
     */
    struct PlayerStart {
        float dilithium = 0;
        float maxDilithium = 0;
        int32_t officerLimit = 0;
        float crew = 0;
        float maxCrew = 0;
        int32_t officersUsed = 0; // what its units take, as saved (fed1's player's 19)
        int32_t other = 0;        // not known: 1, 6, then each slot's own number in fed1
    };

    /** A named path for a mission's script (or a skirmish's start). */
    struct Path {
        std::string name;
        std::vector<Vec3> points;
        uint32_t kind = 0; // the word after its points: 0, 2 or 3 (meaning not known)
    };

    int version = 0;             // the header's: 2051 (2050 in one map)
    std::string description;     // the header's, from its hex: the mission's title
    std::string name;            // as saved: "fad1.bzn" (the file's name when it was made)
    std::array<float, 6> area{}; // x, y and z from and to (inferred: most objects lie within)
    Vec3 light{};                // three floats, 0 in most maps (guessed: a light's colour)
    std::string background;      // the background's model: "mbgstars.sod"
    std::string techTree;        // "fed1.tt"
    std::string race;            // the player's: "federation"
    std::vector<Object> objects;
    std::vector<PlayerStart> players; // the players' counters, after the objects; none if not found
    std::vector<std::string> scripts; // a mission's: "Federation1S.dsl", "Federation1R.drl"
    std::vector<Path> paths;
    std::vector<Field> fields; // every field, in order

    /**
     * Reads a map.
     * @return a failure if the header or a field's framing is wrong, or
     *         the objects or paths aren't as their counts say
     */
    Error parse(const std::vector<uint8_t> &file);
};

} // namespace reluva::storm3d
