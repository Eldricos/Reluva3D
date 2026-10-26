// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs {

// A track's .col file: the virtual road the game drives on, and the 3D
// structures and paths of its moving objects, as docs/formats/nfs/col.md
// describes. Coordinates are the track's (.frd).
struct TrackCollision {
    using Vec3 = std::array<float, 3>;

    // A point along the road, about eight per track block.
    struct RoadPoint {
        Vec3 position{};                // 16.16 fixed point in the file
        Vec3 normal{}, forward{}, right{}; // unit vectors (signed bytes / 127)
        float leftWall = 0, rightWall = 0; // distances from the point, along right
        uint32_t unknown = 0;
        std::array<uint8_t, 3> extra{}; // the vectors' fourth bytes
    };

    struct Structure {
        struct Polygon {
            uint16_t texture = 0;
            std::array<uint8_t, 4> vertices{};
        };
        std::vector<Vec3> vertices;
        std::vector<uint32_t> vertexUnknown;
        std::vector<Polygon> polygons;
    };

    // An object (type 3: moving, by keys as .frd's; type 1: still).
    struct Object {
        struct Key {
            std::array<int32_t, 3> position{}; // 16.16 fixed point
            std::array<int16_t, 4> rotation{}; // 2.14 fixed point quaternion
        };
        uint8_t type = 0;
        uint8_t structure = 0;
        Vec3 reference{};
        int16_t delay = 0;
        std::vector<Key> keys;
    };

    std::vector<std::array<uint8_t, 8>> textures; // xbid 2, not decoded yet
    std::vector<Structure> structures;            // xbid 8
    std::vector<Object> objects, objects2;        // xbid 7 and 18
    std::vector<RoadPoint> road;                  // xbid 15
};

Error parseCollision(const std::vector<uint8_t>& data, TrackCollision& out);

} // namespace reluva::nfs
