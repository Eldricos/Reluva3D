// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "graphics/gfx3d.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A world's placed objects (`scape.omp`, "OBJ_"), as SimCoaster's
 * `Data/levels/arabian/scape.omp`: points in the world, each with a kind,
 * a number and a radius. Sound and particle emitters, it seems: see
 * docs/engines/tpw/games/simcoaster/formats/omp.md.
 */
class ObjectMap {
public:
    /** An object placed in the world. */
    struct Object {
        uint32_t kind = 0;    // 1, 2 or 3: a sound at a point, particles, a sound at the entrance (inferred)
        uint32_t variant = 0; // 0 or 1 for kinds 1 and 3; 20 for kind 2: not worked out
        uint32_t number = 0;  // a sound or effect's, perhaps: 3, 7, 139...
        gfx::Vec3 position{}; // in the models' units, from the park's corner
        float radius = 0;     // in the models' units
        /** Two more numbers, in thousandths as the others: 100 and -100 for most of kind 3. */
        float extra[2] = {};
    };

    /** Reads the objects. Fails without "OBJ_", or on a record or name past the end. */
    Error parse(const std::vector<uint8_t> &data);

    const std::vector<Object> &objects() const { return _objects; }
    /** The file names in its INCL chunk, as stored: where the numbers were defined. */
    const std::vector<std::string> &includes() const { return _includes; }

private:
    std::vector<Object> _objects;
    std::vector<std::string> _includes;
};

} // namespace reluva::tpw
