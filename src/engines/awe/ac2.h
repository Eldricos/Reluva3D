// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Scene files (.ac2, Locations/<area>/<scene>.ac2): what a scene holds,
// as the game's loader reads it. See docs/formats/awe/ac2.md.
//
// A file is a stream of records, each a four-character tag (stored as a
// little-endian number, so "SCEN" reads "NECS") and its fields. Most
// records hold a count of the fields that follow, read in a fixed order
// until the count runs out, so later versions could add fields at the
// end. Text is UTF-16, in fixed buffers of 256 (or 32) characters.

namespace reluva::awe {

// A field's value: a number (4 bytes, integer or float, 2 bytes
// integer), text, or a list of text pairs or number pairs.
struct Ac2Value {
    enum class Type : uint8_t { U16, U32, F32, Text, TextPairs, NumberPairs, Bytes } type = Type::U32;
    uint32_t raw = 0; // U16, U32, F32 (the float's bits)
    std::string text; // UTF-8
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::pair<uint32_t, uint32_t>> numbers;
    std::vector<uint8_t> bytes;

    float f() const;
    int32_t i() const { return int32_t(raw); }
    // For display: the value as text.
    std::string str() const;
};

struct Ac2Record {
    std::string tag;
    // Fields in file order, named where their use is known, else by
    // where the original keeps them ("b54": offset 0xb54 in its object).
    std::vector<std::pair<std::string, Ac2Value>> fields;
    // Sub-records that follow an object's own fields (SPRT, MODL, ANIM...).
    std::vector<Ac2Record> parts;

    const Ac2Value* field(std::string_view name) const;
    const Ac2Record* part(std::string_view tag) const;
    float f(std::string_view name) const;
    int32_t i(std::string_view name) const;
    std::string text(std::string_view name) const;
};

struct Ac2Scene {
    std::string path; // the scene's own name, as SCEN gives it
    // Everything in the file, in order, scene settings (LOAD, CAMR,
    // MUSC...) and objects (2DOB, SPOT, NPCX...) alike.
    std::vector<Ac2Record> records;

    const Ac2Record* camera() const;
    // The objects (records the game creates an object for).
    std::vector<const Ac2Record*> objects() const;
};

// Reads a scene. Stops with an error at a tag it doesn't know, keeping
// what came before.
Error parseAc2(const std::vector<uint8_t>& data, Ac2Scene& out);

// Whether a tag names an object (2DOB, SPOT, NPCX...) rather than a scene
// setting.
bool isAc2Object(std::string_view tag);

// Writes the scene as indented text, for tools.
std::string dumpAc2(const Ac2Scene& scene);

} // namespace reluva::awe
