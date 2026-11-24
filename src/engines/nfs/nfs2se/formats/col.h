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
    /** Texture::flags (0x433bd0, 0x435090). */
    enum TextureFlag : uint8_t {
        kAnimated = 0x04,   // frames pictures from `picture` on, in turn
        kTwoSided = 0x10,   // never culled
        kSeeThrough = 0x20, // its keyed colour not drawn
        // Drawn as any other by both programs; on quads off the drivable road.
        kOffRoad = 0x40,
    };
    /** Texture::orientation: how its picture is turned on a quad (0x433c34). */
    enum Orientation : uint8_t {
        kUnmirrored = 0x01, // set whenever neither mirror is; not read
        kQuarterTurn = 0x02,
        kHalfTurn = 0x04,
        kThreeQuarterTurn = 0x08,
        kMirrorAcross = 0x10, // the picture's left and right swapped
        kMirrorDown = 0x40,   // its top and bottom swapped
    };
    /** What a polygon's texture number stands for (extrablock 2). */
    struct Texture {
        uint16_t picture = 0; // its place in the track's .qfs
        uint8_t flags = 0;    // TextureFlag
        uint8_t orientation = 0;
        std::array<uint8_t, 3> colour{}; // RGB, 178, 178, 178 in most; neither program reads it
        uint8_t frames = 0;              // with kAnimated
        uint8_t ticksPerFrame = 0;       // of the game's clock, with kAnimated
    };
    /** A point of the virtual road (extrablock 15), about 8 to a block. */
    struct RoadPoint {
        Point position{};
        // Vectors of length about 127: up (mostly), toward the next point,
        // and across the road, toward the right edge.
        std::array<int8_t, 3> up{};
        std::array<int8_t, 3> forward{};
        std::array<int8_t, 3> across{};
        uint16_t block = 0;
        // Where a car can drive, a bit a lane's width across: 7 the first
        // right of the point, 8 the first left, outward from there (0x42a450).
        uint16_t laneMask = 0;
        // From the point to the road's edges, in 256ths of the unit.
        uint16_t leftEdge = 0;
        uint16_t rightEdge = 0;
        // Each edge's wall's kind, left in the high nibble: 0 none, 2 a
        // tunnel's (col.md, The walls).
        uint8_t edges = 0;
        uint8_t lanes = 0; // the lanes each way, left in the high nibble
        // Each side's lanes' width, in halves of the unit.
        uint8_t leftLaneWidth = 0;
        uint8_t rightLaneWidth = 0;
    };

    std::vector<Texture> textures;
    std::vector<Mesh> meshes;          // extrablock 8: objects the whole track shares
    std::vector<Placement> placements; // extrablock 7
    std::vector<RoadPoint> road;
};

/**
 * The picture corner each of a quad's four vertices takes, for a texture's
 * orientation: 0 bottom left, 1 bottom right, 2 top right, 3 top left, as
 * the program lays them (0x4735a4): unturned, top left, top right, bottom
 * right, bottom left.
 */
std::array<uint8_t, 4> pictureCorners(uint8_t orientation);

/** Whether `data` is NFS II's .col: a COLL file whose texture records are 10 bytes. */
bool isNfs2Collision(const std::vector<uint8_t> &data);

/** Reads NFS II's .col file. Fails if a block, a record or an index doesn't fit. */
Error parseCollision(const std::vector<uint8_t> &data, Collision &out);

} // namespace reluva::nfs::nfs2se
