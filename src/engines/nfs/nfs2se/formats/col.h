// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/nfs2se/formats/trk.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * A track's .col file (NFS II's "COLL" version 11): its textures, the
 * objects along the whole track, and the virtual road
 * (docs/engines/nfs/games/nfs2se/formats/col.md). NFS III's has the same
 * container, with other records in it (engines/nfs/formats/col.h).
 */
struct Collision {
    /** What a polygon's texture number stands for (extrablock 2). */
    struct Texture {
        uint16_t picture = 0;   // its place in the track's .qfs
        uint16_t alignment = 0; // not worked out
        std::array<uint8_t, 3> colour{};
        std::array<uint8_t, 3> colour2{};
    };
    /** A point of the virtual road (extrablock 15), about 8 to a block. */
    struct RoadPoint {
        Point position{};
        // Each a vector of length about 127: up, forward, right.
        std::array<int8_t, 3> up{};
        std::array<int8_t, 3> forward{};
        std::array<int8_t, 3> right{};
        uint16_t block = 0;
        uint16_t unknown = 0;
        // How far the road (or its walls) reaches either side of the point,
        // along `right`; the scale isn't checked against the game.
        uint16_t leftEdge = 0;
        uint16_t rightEdge = 0;
        uint16_t respawn = 0; // not worked out
        uint32_t unknown2 = 0;
    };

    std::vector<Texture> textures;
    std::vector<Mesh> meshes;          // extrablock 8: objects the whole track shares
    std::vector<Placement> placements; // extrablock 7
    std::vector<RoadPoint> road;
};

/** Whether `data` is NFS II's .col: a COLL file whose texture records are 10 bytes. */
bool isNfs2Collision(const std::vector<uint8_t> &data);

/** Reads NFS II's .col file. Fails if a block, a record or an index doesn't fit. */
Error parseCollision(const std::vector<uint8_t> &data, Collision &out);

} // namespace reluva::nfs::nfs2se
