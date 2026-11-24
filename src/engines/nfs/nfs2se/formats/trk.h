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
    // Four 3-bit values above the low 4 bits, one for each of vertices 1,
    // 0, 3 and 2 from the low end: light baked by corner, 7 the most, which
    // the 3dfx program draws as greys (cornerGrey). The low 4 bits are
    // 0xF, 1 or 2, read by neither program.
    uint16_t shades = 0;
    std::array<uint8_t, 4> vertices{};
};

/**
 * The grey the 3dfx program draws a quad's vertex `k` (0-3) in, its texture
 * times it: 24 × its shade + 50, 50 to 218 (NFS2SEA.EXE 0x429cea).
 */
inline uint8_t cornerGrey(const Quad &quad, int k) {
    constexpr int kShift[4] = {7, 4, 13, 10}; // vertices 0-3's shades
    return static_cast<uint8_t>(24 * ((quad.shades >> kShift[k]) & 7) + 50);
}

/** One of the objects' meshes (extrablock 8, in a block or the .col): its vertices relative to where it's placed. */
struct Mesh {
    std::vector<Point> vertices;
    std::vector<Quad> quads;
};

/** Where a mesh is placed (extrablocks 7, 18 and 19, and the .col's 7). */
struct Placement {
    uint8_t type = 0;      // 1: fixed; 3: animated; 4: an obstacle, `obstacle` its record
    uint8_t mesh = 0;      // into the same block's (or the .col's) meshes
    Point position{};      // for 1 and 4
    uint32_t obstacle = 0; // type 4's: its place in the block's obstacles
    /** Type 3: a key of its animation. */
    struct Key {
        Point position{};
        std::array<int16_t, 4> rest{}; // not worked out (an orientation, perhaps)
    };
    uint16_t animationWord = 0; // after the key count, not worked out
    std::vector<Key> keys;
};

/** A cross-section of a block's road (extrablock 6), one for each virtual road point. */
struct RoadRow {
    uint16_t firstQuad = 0; // into the high level's first group
    uint8_t quads = 0;      // the rows together cover the group, in order
    uint8_t music = 0;      // the music's section for this stretch, 2-6 (0x436440) *(inferred)*
    // The road points of a stretch running beside this one, a car off its
    // row being put on the nearer (0x437270); kNoRoad for none.
    uint16_t leftRoad = kNoRoad;
    uint16_t rightRoad = kNoRoad;

    static constexpr uint16_t kNoRoad = 0xffff;
};

/** A point of a painted line on the road (extrablock 9), drawn at the high level only (0x435260). */
struct RoadLine {
    uint8_t vertex = 0;    // of the block
    uint8_t roadPoint = 0; // from the block's first
    uint8_t type = 0;      // which of the lin0-lin7 pictures and how; 0xFF ends a line
    uint8_t quad = 0;      // of the high level, the line's ground
};

/** Something a car can hit (extrablock 11): a type 4 placement's (0x44042c). */
struct Obstacle {
    Point position{};      // its placement's
    uint16_t radius = 0;   // in 256ths of the unit
    uint16_t serial = 0;   // over the track: its slot in the program's table of obstacles' states
    uint8_t height = 0;    // in halves of the unit, rounded down
    uint8_t placement = 0; // its placement's place in its extrablock
    uint8_t kind = 0;      // 1 fixed, 2 knocked over when hit (0x433650)
};

/** A sound placed by the road (extrablock 17), from the track's TRAMBnn.BNK. */
struct AmbientSound {
    Point position{};
    uint16_t sound = 0;  // its place in the bank
    uint16_t random = 0; // 1: played now and then; 0: looped
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
        // See-through upright things (fences, trees, signs), drawn with the
        // objects (0x434c48).
        std::vector<Quad> second;
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
    std::vector<RoadRow> roadRows;     // extrablock 6
    std::vector<RoadLine> roadLines;   // extrablock 9
    std::vector<Obstacle> obstacles;   // extrablock 11
    std::vector<AmbientSound> sounds;  // extrablock 17
    std::vector<ExtraBlock> extra;     // every extrablock, as it is
};

/** A track (trNN.trk, "TRAC"). */
struct Track {
    // After TRAC: 22 in all; then the largest superblock's size, the
    // largest block's, the most a block's neighbourhood (extrablock 4)
    // sums to, and a fourth not worked out. Neither program uses them.
    std::array<uint32_t, 5> header{};
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
