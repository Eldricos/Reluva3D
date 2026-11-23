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
    /** The polygon flags the program draws by (0x46da60, 0x461e5c). */
    enum Flag : uint8_t {
        kTriangle = 0x01, // only vertices 0 to 2 drawn
        kQuad = 0x02,     // set whenever kTriangle isn't
        kReversed = 0x04, // facing the other way: the mirrored, left-hand parts
        kKeyed = 0x08,    // the picture's palette entry 255 not drawn
        kTwoSided = 0x10, // never culled
    };
    /** A polygon, a quad or a triangle; its picture by name in the car's .qfs. */
    struct Polygon {
        uint8_t flags = 0; // Flag, and the frame count above them (frames())
        std::array<uint8_t, 4> vertices{};
        std::string texture; // 4 characters

        /**
         * How many pictures the polygon takes in turn (1, 2 or 4): the
         * file's, then each with the frame's number in its second
         * character (m000, m100, m200, m300: a wheel's turning).
         */
        int frames() const { return flags >> 5 ? flags >> 5 : 1; }
    };
    /** One of the 32 parts: a piece of the model, at a level of detail. */
    struct Part {
        Point position{};            // in the model, 16.16
        std::vector<Point> vertices; // from `position`
        std::vector<Polygon> polygons;
    };

    uint32_t unknown = 0; // 0, 1 or 2; the program doesn't read it
    std::array<Part, 32> parts;
};

/**
 * Reads a .geo file. Fails if a part runs past the end, a polygon uses a
 * vertex its part hasn't, or bytes are left after the 32 parts.
 */
Error parseCarModel(const std::vector<uint8_t> &data, CarModel &out);

} // namespace reluva::nfs::nfs2se
