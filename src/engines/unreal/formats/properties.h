// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// An object's data starts with its properties: the values that differ
// from its class's defaults, each tagged with its name and type, ending at
// the name "None". What follows is the class's own data (a texture's
// pixels, a level's geometry). See docs/formats/unreal/package.md.

#include "engines/unreal/formats/package.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::unreal {

/** A property's type, from bits 0-3 of its tag's info byte. */
enum class PropertyType : uint8_t {
    kNone = 0,
    kByte = 1,
    kInt = 2,
    kBool = 3,
    kFloat = 4,
    kObject = 5,
    kName = 6,
    kString = 7,
    kClass = 8,
    kArray = 9,
    kStruct = 10,
    kVector = 11,
    kRotator = 12,
    kStr = 13,
    kMap = 14,
    kFixedArray = 15,
};
/** A type's name as UnrealScript has it ("Byte", "Struct"...). */
const char *typeName(PropertyType type);

/** One tagged property, its value's bytes as stored. */
struct Property {
    std::string name;
    PropertyType type = PropertyType::kNone;
    int arrayIndex = 0;
    std::string structName; // Struct properties: the struct's name ("Vector", "Rotator"...)
    bool boolValue = false; // Bool properties keep their value in the tag
    std::vector<uint8_t> data;
};

/** An orientation, in units of 65536 to a full turn. */
struct Rotator {
    int32_t pitch = 0;
    int32_t yaw = 0;
    int32_t roll = 0;
};

/** An object's properties, in the order stored. */
class PropertyList {
public:
    std::vector<Property> items;

    /** The property of that name (any case) and array index, or nullptr. */
    const Property *find(std::string_view name, int arrayIndex = 0) const;

    /**
     * A property's value read as the type asked for; `fallback` when it's
     * missing (the class's default, which packages don't store) or too short.
     * A Bool's value is in its tag: any other type gives `fallback`.
     */
    ///@{
    int32_t integer(std::string_view name, int32_t fallback = 0, int arrayIndex = 0) const;
    float number(std::string_view name, float fallback = 0, int arrayIndex = 0) const;
    uint8_t byte(std::string_view name, uint8_t fallback = 0, int arrayIndex = 0) const;
    bool boolean(std::string_view name, bool fallback = false, int arrayIndex = 0) const;
    ///@}
    /** Object and Class properties: an object reference into the package; 0 if missing. */
    int32_t object(std::string_view name, int arrayIndex = 0) const;
    /** Name properties: the name table index. */
    int32_t nameIndex(std::string_view name, int32_t fallback = -1, int arrayIndex = 0) const;
    /**
     * Struct properties holding a Vector or Rotator (or Vector and Rotator
     * properties); false if missing or too short.
     */
    ///@{
    bool vector(std::string_view name, std::array<float, 3> &out, int arrayIndex = 0) const;
    bool rotator(std::string_view name, Rotator &out, int arrayIndex = 0) const;
    ///@}
    /** Str and String properties, read as package `version` keeps them; empty if missing. */
    std::string string(std::string_view name, int version, int arrayIndex = 0) const;
};

/** Reads tagged properties up to "None"; fails on a damaged list. */
Error readProperties(const Package &package, Reader &r, PropertyList &out);

/**
 * The state an object was saved in (objects with HasStack: a saved game's
 * actors): its code's node and its state (object references), the events
 * it ignores, its latent action, and where in the node's code it is (-1
 * none).
 */
struct StateFrame {
    bool has = false;
    int32_t node = 0;
    int32_t stateNode = 0;
    int32_t latent = 0;
    int32_t offset = -1;
    uint64_t probeMask = 0;
};
/**
 * Reads the start of an export's data up to the class's own data: its
 * state frame, if it has one (into `frame` if given), and its properties.
 * Classes (whose data is laid out differently) are refused.
 */
Error beginObject(const Package &package, int index, Reader &r, PropertyList &out, StateFrame *frame = nullptr);

/** A property's value as text, for viewers and tools. */
std::string describe(const Package &package, const Property &p);

} // namespace reluva::unreal
