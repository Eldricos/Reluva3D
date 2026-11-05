// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "common/palette.h"
#include "engines/stgenerations/canvas.h"
#include "common/stream.h"

namespace reluva {
class Serializer;
}

namespace reluva::stgenerations {

// A 3D level (GAME.LV2), from the loader at 0x4BD130 in sgens.exe.
//
//   uint32   1 (version)
//   uint32   record sizes[9]: 1024, 8, 292, 152, 140, 276, 196, 64, 24.
//            The original refuses files whose sizes differ from its own.
//   uint32   directory entry count
//   uint32   directory size in bytes (count * 16)
//   entries: uint32 size, uint32 file offset, uint16 type, uint16 id,
//            uint16 count A, uint16 count B
//   chunk data
//
// Chunks are loaded on demand by (type, id). The level uses id 1 of the
// table types, where count B is the number of records:
//
//   type 1   palette: 256 x (R, G, B, 0)
//   type 2   vertices: (x, y) in 16.16 fixed point, the floor plan
//   type 3   flats (292 bytes): horizontal convex polygons of 3-8
//            vertices, each a floor or a ceiling at one height
//   type 4   walls (152 bytes): vertical quads between two vertices
//   type 5   billboards (140 bytes): sprites that always face the camera
//   type 6   actors (276 bytes), placed and moved by the scripts
//   type 7   (196 bytes), empty in verid1
//   type 12  the sky: a 64-byte header and a row-major image
//   type 13  textures, one chunk each, id = texture number
//
// Every byte of verid1's file belongs to the header, directory or a chunk.
//
// World space: x and y are the floor plan, z is height and grows upwards.
// All coordinates are 16.16 fixed point. Fields were identified from the
// renderer in sgens.exe (0x4C13F0 flats, 0x4C7D00 walls).
class LevelFile {
public:
    enum ChunkType {
        kChunkPalette = 1,
        kChunkVertices = 2,
        kChunkSectors = 3,
        kChunkWalls = 4,
        kChunkBillboards = 5,
        kChunkActors = 6,
        kChunkSky = 12,
        kChunkTexture = 13
    };

    static const uint32_t kVersion = 1;
    static const int kRecordSizeCount = 9;
    static const uint32_t kRecordSizes[kRecordSizeCount];
    // Palette index of the transparent colour (magenta) in textures.
    static const uint8_t kTransparent = 0xFF;

    struct ChunkEntry {
        uint32_t size;
        uint32_t offset;
        uint16_t type;
        uint16_t id;
        uint16_t countA;
        uint16_t countB;
    };

    // Floor plan coordinates, 16.16 fixed point.
    struct Vertex {
        int32_t x;
        int32_t y;
    };

    // How a wall, flat or billboard is lit, from offsets 0x64-0x68.
    //
    // The renderer darkens each surface by one of 8 levels, 0 being fully
    // lit and 7 nearly black (see buildShadeTables()). A surface in a light
    // group gets its level from its brightness and the group's brightness,
    // which the scripts can change; any other surface keeps the level
    // stored in the file.
    struct LightInfo {
        // Light group, or 0.
        int16_t group;
        // Brightness when the group is fully on; 3200 per light level.
        uint16_t brightness;
        // Fixed light level, used when group is 0.
        uint8_t level;

        // The light level for a group brightness of percent (100 = on).
        int getLevel(int percent) const;
        void sync(Serializer& s);
    };

    static const int kLightLevels = 8;

    struct Wall {
        uint16_t vertex1;
        uint16_t vertex2;
        // Line equation: normalX * x + normalY * y + distance = 0, with a unit
        // normal (16.16), from offsets 28, 36 and 40. The renderer uses it
        // to cull walls facing away unless kWallTwoSided is set.
        int32_t normalX;
        int32_t normalY;
        int32_t distance;
        // Heights of the top and bottom edges.
        int32_t top;
        int32_t bottom;
        // Texture coordinates in texels (16.16): u runs from uStart at
        // vertex1 to uStart + uLength at vertex2, v from vStart at the
        // bottom edge up to vStart + vLength at the top, counting rows from
        // the bottom of the texture.
        int32_t uStart;
        int32_t uLength;
        // u offsets from uStart for the ends, when either is non-zero (0x70,
        // 0x74), for a texture that runs on along a strip of walls. The
        // renderer works out which end gets which.
        int32_t uEnd1 = 0;
        int32_t uEnd2 = 0;
        int32_t vStart;
        int32_t vLength;
        uint16_t texture;
        // Animation frames: textures texture .. texture + frames - 1.
        uint16_t frames;
        // Animation speed: each frame shows for 65536 / speed ms.
        uint16_t speed;
        uint32_t flags;
        LightInfo light;
        // Zones the wall belongs to (offset 0x3C); see Sector::activeZones.
        uint64_t zones;
        // Script object (id | 0x4000) told when the player touches it, from offset 0x14.
        uint16_t objectId;
        // Drawn at full brightness, as set by a script.
        bool fullbright = false;
        // SET_TEXTURE_SCROLL (0xAB): u and v speeds.
        int16_t uScroll = 0, vScroll = 0;
        // Doors (see Movers): the mover this wall is part of (0x8A, 0 for
        // none), how it moves (0x8C), the mover's speed and wait (0x8E,
        // 0x90), and the mover walking into it starts, with how (0x92, 0x94).
        int16_t moverId = 0, moverMode = 0;
        uint16_t moverSpeed = 0, moverWait = 0;
        int16_t bumpMover = 0, bumpMode = 0;
    };

    // Wall and flat flag: a mover's follower part, not a leader.
    static const uint32_t kFlagMoverFollower = 0x2000;
    // Wall flag: a closing door that meets the player opens again.
    static const uint32_t kWallMoverReverses = 0x80000;

    // Wall flag: drawn from behind too (0x4C936A); otherwise only from the
    // front. From behind, 0x4000 alone only hides what is past the wall.
    static const uint32_t kWallTwoSided = 0x8000;
    // The texture runs the other way (0x4C9393). From behind it runs the
    // other way again unless exactly one of these is set.
    static const uint32_t kWallMirror = 0x20;
    // Columns that are one run of texels are drawn over that run alone
    // (0x4C0AD0), so a silhouette such as a cliff top isn't repeated above
    // itself when the wall is taller than one copy of its texture.
    static const uint32_t kWallTrimColumns = 0x2;
    static const uint32_t kWallMirrorBack = 0x40000;
    // Wall flag: drawn on the automap (SET_ZONE_ON_MAP).
    static const uint32_t kWallOnMap = 0x800;
    // Wall flag: blocks movement from behind as well as from the front.
    static const uint32_t kWallSolidBothSides = 0x8000;
    // Wall flag: left out of the collision grid (0x4BE860), so nothing runs
    // into it: drawn openings such as epsion's hanging plants.
    static const uint32_t kWallNoCollision = 0x1000;

    // A floor or ceiling polygon. The original stores these in the same
    // table and draws a floor only from above and a ceiling from below.
    struct Sector {
        // Vertex indices of the convex polygon, from offsets 266/268.
        std::vector<uint16_t> vertices;
        // Flags word at offset 108; see kFlag* below.
        uint32_t flags;
        int32_t height;
        // World-aligned texture mapping in texels (16.16):
        // u = x * uScale + uOffset, v = y * vScale + vOffset, rotated by
        // rotation (a 16-bit angle) when that is non-zero.
        int32_t uOffset;
        int32_t uScale;
        int32_t vOffset;
        int32_t vScale;
        uint16_t rotation;
        uint16_t texture;
        uint16_t frames;
        uint16_t speed;
        LightInfo light;
        // Script object (id | 0xC000) told when the player steps on it, from offset 0x14.
        uint16_t objectId;
        bool fullbright = false;
        // The zone the flat belongs to (offset 0x3C, one bit set).
        uint64_t zones;
        // The zones in play while standing on this floor (offset 0x74).
        // Only walls, flats and billboards in those zones are drawn and
        // collided with, which keeps rooms stacked above one another apart.
        uint64_t activeZones;
        // The raw 292-byte record, for the fields still being decoded.
        std::vector<uint8_t> raw;
        int16_t uScroll = 0, vScroll = 0;
        // A mover follower: the mover (0x106) and mode 6 or 7 (0x108).
        int16_t moverId = 0, moverMode = 0;
        // ADD_FLAT_ZONE_BAND (0xC1): zones in play by height, highest first;
        // standing here, the first at or below the flat's height counts.
        std::vector<std::pair<int32_t, uint64_t>> zoneBands;

        // The zones in play while the player stands here.
        uint64_t getActiveZones() const {
            for (const auto& band : zoneBands) {
                if (band.first <= height)
                    return band.second;
            }
            return activeZones;
        }

        bool isCeiling() const { return (flags & kFlagCeiling) != 0; }
    };

    static const uint32_t kFlagVisible = 0x100;
    // Left out of the zone draw lists (0x4BF79E), so never drawn: used for
    // invisible floors the player stands on.
    static const uint32_t kFlagNotDrawn = 0x1;
    static const uint32_t kFlagCeiling = 0x100000;
    // See-through drawing (0x4C0AD0, 0x4C80B4): texels 0xF0-0xFE are
    // averaged with what is behind them. Walls need only this bit;
    // billboards and actors also need kFlagColumnTests.
    static const uint32_t kFlagTranslucent = 0x8;
    static const uint32_t kFlagColumnTests = 0x4;
    // Floors nothing rests on: the player (0x4B4904), actors (0x4B6AB0) and
    // dropped items (0x4B7AA0) all pass through to the floor below. They
    // are trigger floors (stepping through still tells their script) and
    // water surfaces (SET_FLAT_GROUPS).
    static const uint32_t kFlagPassThrough = 0x1000;

    // A camera-facing sprite, standing on (x, y, z).
    struct Billboard {
        int32_t x;
        int32_t y;
        int32_t z;
        // Half the width, and the height.
        int32_t halfWidth;
        int32_t height;
        uint16_t texture;
        uint16_t frames;
        uint16_t speed;
        uint32_t flags;
        LightInfo light;
        uint64_t zones;
        // The box around (x, y) that fires proximity events, and its height.
        int32_t radius;
        int32_t boxHeight;
        // Script object (id | 0x2000), from offset 0x14.
        uint16_t objectId;
        bool hidden = false;
        bool fullbright = false;
    };

    // Billboard flags. Items rest on the floor below them: the original
    // drops them at the start (0x4BDD70) and wherever scripts put them, and
    // they take the floor's zones and light.
    static const uint32_t kBillboardItem = 0x400000;
    static const uint32_t kBillboardUnpickable = 0x400;
    // Keeps its height when dropped, as actors with the same flag do.
    static const uint32_t kBillboardFlyer = 0x1000000;

    // One of an actor's animations (8 bytes at 0xC4 + 8 * entry).
    struct Animation {
        // Distance walked per animation frame, 8.8 world units.
        uint16_t speed;
        // Frames per second, roughly (see Actors).
        uint8_t rate;
        // Frames per view direction.
        uint8_t frames;
        // Frame the animation loops back to; frames - 1 plays it once.
        uint8_t loopStart;
        // View directions stored: 8, or 1 for the same picture from all sides.
        uint8_t directions;
        // Directions 5-7 are drawn as 3-1 mirrored.
        uint8_t mirror;
    };

    static const int kAnimationCount = 7;

    // An actor (type 6): a character or object the scripts control, drawn
    // from a sprite set. The original works on these records directly
    // (0x4C3C10). Actors sharing an id are parts of one actor.
    struct Actor {
        // Script object (id | 0xA000), from offset 0x14.
        uint16_t objectId;
        int32_t x;
        int32_t y;
        int32_t z;
        // Distance at which "walk to" has arrived.
        int32_t reach;
        // Added to z for the sprite's bottom edge.
        int32_t drawOffset;
        // Sprite scale: the drawn width is 2 * frame half width * scale.
        int32_t scale;
        // Drawn height in world units.
        int32_t height;
        // Collision cylinder.
        int32_t radius;
        int32_t bodyHeight;
        uint32_t flags;
        /** Wander: turn back beyond outerRadius, walk home beyond homeRadius,
         * turn on crossing innerRadius inwards. */
        int32_t outerRadius;
        int32_t innerRadius;
        int32_t homeRadius;
        // Wander: walk this far (a random distance in the range) between turns.
        int32_t minWalk;
        int32_t maxWalk;
        int32_t stepHeight;
        // Where shots leave from: across (x), up from the sprite's bottom (z) and ahead (y).
        int32_t muzzleX, muzzleZ, muzzleY;
        Animation animations[kAnimationCount];
        // Sprite set number (NNN.gs2), which is also the actor's class.
        uint16_t spriteSet;
        // Wander turn range, in 1/65536 turns.
        uint16_t minTurn;
        uint16_t maxTurn;
        // Turning speed: heading units per 1024 ms.
        uint16_t turnSpeed;
        // 65536 = a full turn; 0 faces +y, 16384 faces -x.
        uint16_t heading;
        // AI mode (see Actors::Mode).
        uint8_t mode;
        // Wander around the player rather than home.
        uint8_t followPlayer;
        LightInfo light;
    };

    struct Texture {
        uint16_t id = 0;
        uint16_t flags = 0;
        // Upright image; pixels equal to kTransparent are see-through.
        Canvas surface;
    };

    bool load(SeekableReadStream& stream);

    const std::vector<ChunkEntry>& getDirectory() const { return _directory; }
    const ChunkEntry* findChunk(uint16_t type, uint16_t id) const;
    // A chunk's bytes, or an empty vector if it is missing.
    std::vector<uint8_t> getChunkData(uint16_t type, uint16_t id) const;

    const Palette& getPalette() const { return _palette; }
    const std::vector<Vertex>& getVertices() const { return _vertices; }
    const std::vector<Wall>& getWalls() const { return _walls; }
    const std::vector<Sector>& getSectors() const { return _sectors; }
    const std::vector<Billboard>& getBillboards() const { return _billboards; }
    const std::vector<Actor>& getActors() const { return _actors; }

    // Writable tables, for the scripts to change the level as it is played.
    // Save or load what scripts change: the walls', flats' and billboards'
    // textures, flags, lights and visibility, and where billboards are.
    void syncState(Serializer& s);

    std::vector<Wall>& getWalls() { return _walls; }
    // Doors move vertices.
    std::vector<Vertex>& getVertices() { return _vertices; }
    // Recompute a wall's normal and distance from its vertices, as 0x4B76F0 does.
    void updateWallGeometry(int index);
    std::vector<Sector>& getSectors() { return _sectors; }
    std::vector<Billboard>& getBillboards() { return _billboards; }

    // The sky image, if the level has one. The header holds the image size
    // at offset 16 and its height at offset 24; rows are stored in order.
    bool loadSky(Canvas& sky) const;
    // The rows of a sky whose header height doesn't fit its size, or 0.
    uint32_t guessSkyHeight(const uint8_t* pixels, uint32_t size) const;

    // Ids of all texture chunks, in directory order.
    std::vector<uint16_t> getTextureIds() const;
    bool loadTexture(uint16_t id, Texture& texture) const;

private:
    bool loadTables();

    std::vector<uint8_t> _data;
    std::vector<ChunkEntry> _directory;
    Palette _palette;
    std::vector<Vertex> _vertices;
    std::vector<Wall> _walls;
    std::vector<Sector> _sectors;
    std::vector<Billboard> _billboards;
    std::vector<Actor> _actors;
};

// Draw a level's floor plan from above into dst: sectors filled, walls on
// top, scaled to fit the surface. Colour indices refer to the level palette.
void drawLevelMap(const LevelFile& level, Canvas& dst);

} // namespace reluva::stgenerations

