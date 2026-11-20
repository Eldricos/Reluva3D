// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Classes' default properties, and actors' properties with them. A placed
// actor stores only the properties that differ from its class's defaults;
// a class stores only those that differ from its parent's. The defaults
// are the last thing in a class's data, after its script (script.h reads
// it). See docs/engines/unreal/games/ds9fallen/systems/actors.md.

#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/formats/properties.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace reluva::unreal {

/**
 * An actor's properties: its own, then its class's defaults and each
 * parent's, most specific first. Object references are resolved in the
 * package each property came from.
 */
class ActorProperties {
public:
    /** One object's properties, and the package that holds them. */
    struct Layer {
        const Package *package = nullptr;
        PropertyList props;
    };
    std::vector<Layer> layers;

    /**
     * The most specific layer's property of that name, and the package it's
     * in in `from` if given; nullptr if no layer has it.
     */
    const Property *find(std::string_view name, int arrayIndex = 0, const Package **from = nullptr) const;
    /**
     * A property's value from the most specific layer that has it, read as
     * that type; `fallback` (or false, or empty) if none has it.
     */
    ///@{
    uint8_t byte(std::string_view name, uint8_t fallback = 0, int arrayIndex = 0) const;
    float number(std::string_view name, float fallback = 0) const;
    bool boolean(std::string_view name, bool fallback = false) const;
    bool vector(std::string_view name, std::array<float, 3> &out, int arrayIndex = 0) const;
    bool rotator(std::string_view name, Rotator &out) const;
    std::string nameValue(std::string_view name) const;
    ///@}
    /** An object property, resolved from the package of the layer it's in; empty if none has it. */
    ObjectRef object(const PackageSet &packages, std::string_view name, int arrayIndex = 0) const;
};

/** Classes' default properties, read once each and kept. */
class ClassDefaults {
public:
    /** `packages` must outlive it. */
    explicit ClassDefaults(const PackageSet &packages) : _packages(packages) {}

    /** A class export's own default properties; nullptr if they weren't found. */
    const PropertyList *defaults(const Package &package, int classExport);

    /**
     * An actor's properties with its classes' defaults under them; a class
     * that doesn't resolve ends the chain there.
     */
    Error actorProperties(const Package &package, int exportIndex, ActorProperties &out);

    /** How many classes' defaults were read, and how many weren't found. */
    ///@{
    int found() const { return _found; }
    int missing() const { return _missing; }
    ///@}

private:
    const PackageSet &_packages;
    std::map<std::pair<const Package *, int>, std::optional<PropertyList>> _cache;
    int _found = 0;
    int _missing = 0;
};

} // namespace reluva::unreal
