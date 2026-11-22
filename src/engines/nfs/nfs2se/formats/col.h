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
        uint16_t picture = 0; // its place in the track's .qfs
        // Not worked out: a word and two groups of three bytes.
        uint16_t unknown = 0;
        std::array<uint8_t, 3> unknown4{};
        std::array<uint8_t, 3> unknown7{};
    };
    /** A point of the virtual road (extrablock 15), about 8 to a block. */
    struct RoadPoint {
        Point position{};
        // Vectors of length about 127: up (mostly), toward the next point,
        // and across the road (at right angles to both; which side isn't known).
        std::array<int8_t, 3> up{};
        std::array<int8_t, 3> forward{};
        std::array<int8_t, 3> across{};
        uint16_t block = 0;
        // Not worked out: the words at 24, 26, 28 and 30, and the u32 at 32.
        std::array<uint16_t, 4> unknown{};
        uint32_t unknown32 = 0;
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
