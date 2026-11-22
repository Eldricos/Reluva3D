// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * A point as NFS II's tracks give them: x, height, y, in the files' unit
 * (their 16.16 fixed point's 1.0).
 */
using Point = std::array<float, 3>;

/** A quad of a track's or an object's mesh. */
struct Quad {
    uint16_t texture = 0; // into the .col's texture table (col.h)
    uint16_t second = 0;  // the record's second word; 0xFFFF on many, not worked out
    std::array<uint8_t, 4> vertices{};
};

/** One of the objects' meshes (extrablock 8, in a block or the .col): its vertices relative to where it's placed. */
struct Mesh {
    std::vector<Point> vertices;
    std::vector<Quad> quads;
};

/** Where a mesh is placed (extrablocks 7, 18 and 19, and the .col's 7). */
struct Placement {
    uint8_t type = 0;   // 1: fixed; 3: animated; 4: fixed, with `extra`
    uint8_t mesh = 0;   // into the same block's (or the .col's) meshes
    Point position{};   // for 1 and 4
    uint32_t extra = 0; // type 4's last word: 0 or 1, not worked out
    /** Type 3: a key of its animation. */
    struct Key {
        Point position{};
        std::array<int16_t, 4> rest{}; // not worked out (an orientation, perhaps)
    };
    uint16_t animationWord = 0; // after the key count, not worked out
    std::vector<Key> keys;
};

/** An extrablock as the file has it, for those not read into fields. */
struct ExtraBlock {
    uint16_t id = 0;
    uint16_t count = 0;
    std::vector<uint8_t> data; // its records, after the 8-byte header
};

/**
 * A block of road: about 32 units of the track, its ground at three levels
 * of detail, and what's on and around it (docs/engines/nfs/games/nfs2se/
 * formats/trk.md).
 */
struct TrackBlock {
    /** The level of detail's polygons: the first count's, then the second's. */
    struct Level {
        std::vector<Quad> first;
        std::vector<Quad> second; // not worked out: what sets them apart
    };
    /** The levels of detail, as `levels` and `vertexCounts` hold them. */
    enum LevelIndex { kLow, kMedium, kHigh };

    uint32_t serial = 0;
    uint16_t unknown = 0;           // the header's word at 10: 8 times the block's number in every block
    std::array<Point, 4> corners{}; // a flat rectangle around the block
    Point reference{};              // its point in the file's table of them
    // Vertices: the first fromNext are the next block's (relative to its
    // reference point), then the block's own. A level uses the first
    // fromNext + its count of them.
    uint16_t fromNext = 0;
    std::array<uint16_t, 3> vertexCounts{}; // by LevelIndex
    std::vector<Point> vertices;            // absolute
    std::array<Level, 3> levels;
    std::vector<Mesh> meshes;          // extrablock 8
    std::vector<Placement> placements; // extrablocks 7, 18 and 19
    std::vector<ExtraBlock> extra;     // every extrablock, as it is
};

/** A track (trNN.trk, "TRAC"). */
struct Track {
    std::array<uint32_t, 5> header{}; // after TRAC: 22 in all, then four numbers not worked out
    std::vector<TrackBlock> blocks;
    std::vector<uint16_t> superblockOf; // each block's superblock (8 blocks to one)
};

/**
 * Reads a .trk file. Fails if a size, offset or count doesn't fit the
 * file, or a polygon uses a vertex its level hasn't.
 */
Error parseTrack(const std::vector<uint8_t> &data, Track &out);

/**
 * Reads `count` meshes (extrablock 8's records) from `p` up to `end`. Fails
 * if one doesn't fit its size or uses a vertex it hasn't.
 */
Error readMeshes(const uint8_t *p, const uint8_t *end, uint16_t count, std::vector<Mesh> &out);

/**
 * Reads `count` placements (extrablock 7's records) from `p` up to `end`.
 * Fails if one doesn't fit, is of a type not known, or names a mesh past
 * `meshes`.
 */
Error readPlacements(const uint8_t *p, const uint8_t *end, uint16_t count, size_t meshes, std::vector<Placement> &out);

} // namespace reluva::nfs::nfs2se
