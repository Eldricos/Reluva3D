// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

/**
 * One entity from a map's entity lump: its key/value pairs in file order.
 * Keys are matched ignoring case; a repeated key keeps every value.
 */
struct Entity {
    std::vector<std::pair<std::string, std::string>> pairs;

    /** The first value for the key, or `fallback`. */
    std::string get(std::string_view key, std::string_view fallback = {}) const;
    /** Whether it has the key. */
    bool has(std::string_view key) const;
    /** Its "classname" (what kind of entity it is), or empty. */
    std::string classname() const { return get("classname"); }
    /** A key's "x y z" as three floats; false if the key is missing or malformed. */
    bool vector(std::string_view key, float out[3]) const;
};

/**
 * Parses the entity lump: `{ "key" "value" ... }` blocks, up to a NUL if
 * there is one. See docs/engines/idtech3/formats/bsp.md.
 * @return An error giving the line where it stopped making sense.
 */
Error parseEntities(std::string_view text, std::vector<Entity> &out);

} // namespace reluva::idtech3
