// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/nfs2se/formats/trk.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * A car or other vehicle's model (a .geo file in cardata.viv): 32 parts
 * in a fixed order (docs/engines/nfs/games/nfs2se/formats/geo.md). Parts 0
 * to 19 are a racing car's detailed model (12 to 19 its wheels), 21 a
 * model at medium detail, 24 and 25 at low; the traffic and the other
 * vehicles have only parts among 20 to 25.
 */
struct CarModel {
    /** A polygon: always a quad, its picture by name in the car's .qfs. */
    struct Polygon {
        uint32_t flags = 0; // 0x4 is set on mirrored (left-hand) parts' polygons
        std::array<uint8_t, 4> vertices{};
        std::string texture; // 4 characters
    };
    /** One of the 32 parts: a piece of the model, at a level of detail. */
    struct Part {
        Point position{};                  // in the model, 16.16
        std::array<uint16_t, 4> unknown{}; // not worked out
        std::vector<Point> vertices;       // from `position`
        std::vector<Polygon> polygons;
    };

    uint32_t version = 0; // 0, 1 or 2: not worked out
    std::array<Part, 32> parts;
};

/**
 * Reads a .geo file. Fails if a part runs past the end, a polygon uses a
 * vertex its part hasn't, or bytes are left after the 32 parts.
 */
Error parseCarModel(const std::vector<uint8_t> &data, CarModel &out);

} // namespace reluva::nfs::nfs2se
