// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace reluva::stawayteam {

/**
 * How a property's value is saved in binary, by the property's writer in
 * Star Trek.exe (docs/engines/stawayteam/formats/binary-object.md).
 */
enum class Saved : uint8_t {
    kInt,        // 4 bytes, signed
    kRef,        // 4 bytes: an object of the same file, by its number
    kFloat,      // a 32-bit float
    kBool,       // 1 byte
    kFlag,       // 1 byte, 0 or 1: one bit of the object's flags
    kPoint,      // 8 bytes: two 4-byte whole numbers
    kFloatPoint, // 8 bytes: two 32-bit floats
    kDouble,     // 8 bytes
    kColour,     // 3 bytes
    kString,     // a 4-byte length, then its bytes: names, text, choices from lists
    kObject,     // a string, the object's class's name (empty for none), then the object
    kInline,     // an object held in place, of a class the file doesn't name
    kObjects,    // a count, then objects held in place, of the property's class
    kArray,      // a count, then the items (each after a string, if named)
    kPolygon,    // "POLY2D", 0, 7, a 4-byte count, then 8 bytes for each point
    kUnknown,    // not worked out: a file holding one can't be read
};

/** What a class saves after its properties: the five the engine reads. */
enum class OwnData : uint8_t {
    kNone,
    kSequence,         // CSequence: a box, then its frames
    kRotationSequence, // CRotationSequence: CSequence's, then 8 bytes a frame
    kModel,            // CModel: its named rotation sequences
    kWayPointMap,      // CWayPointMap: its waypoints, then its lines
    kWayPoint,         // CPathWayPoint: the lines it's on
    kOther,            // some other data, not worked out
};

/** A property a class adds, in the order the class lists them. */
struct PropertySchema {
    const char *name; // "" for those with no name, saved in binary only
    Saved saved;
    Saved element; // an array's items
    bool named;    // an array whose items each have a name first
    int16_t cls;   // kObjects' class, as its index in allClasses(); else -1
    uint16_t size; // kInline: the object's size in its owner, which helps tell its class
};

/** A class as Star Trek.exe registers it. */
struct ClassSchema {
    const char *name;
    float version;  // as the game works it out from the properties: what its objects carry
    int16_t parent; // its index in allClasses(); -1 for none
    OwnData own;    // the class's own, or kNone for its nearest parent's
    uint16_t first; // its own properties: allProperties()[first, first + count)
    uint16_t count;
    uint16_t size; // its objects' size in the game
};

/** Every class, sorted by name (generated: formats/class_table.cpp). */
std::span<const ClassSchema> allClasses();
/** Every class's own properties, one class after another. */
std::span<const PropertySchema> allProperties();

/** A class by its name, or nullptr. */
const ClassSchema *findClass(std::string_view name);
/** Every class with a version, as the files' objects carry it. */
std::vector<const ClassSchema *> classesOfVersion(float version);
/** A class's properties, its parents' first. */
std::vector<const PropertySchema *> propertiesOf(const ClassSchema &cls);
/** The own data a class saves: its own, or its nearest parent's. */
OwnData ownDataOf(const ClassSchema &cls);

} // namespace reluva::stawayteam
