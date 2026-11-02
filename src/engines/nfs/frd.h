// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs {

// A track's geometry (.frd), as docs/formats/nfs/frd.md describes.
// Coordinates are as stored: +y up, left-handed like the models (fce.md).
struct Track {
    using Vec3 = std::array<float, 3>;

    // A quad of four vertices (a triangle repeats one), textured by an
    // entry of the track's texture table.
    struct Polygon {
        std::array<uint16_t, 4> vertices{};
        uint16_t texture = 0; // into `textures`
        // Animated (kAnimated): the first frame's texture entry, and the
        // delay in ticks (top 5 bits) and number of frames (low 3).
        uint16_t animationBase = 0;
        uint8_t flags = 0; // kTwoSided, kAnimated (frd.md, Polygons)
        uint8_t animation = 0;
        static constexpr uint8_t kAnimated = 0x04, kTwoSided = 0x10;
        int frameDelay() const { return animation >> 3; }
        int frameCount() const { return animation & 7; }
        // The texture entry shown at a game tick (64 a second; 0x41b52b).
        uint16_t textureAt(uint64_t tick) const;
    };

    // Polygons grouped as an object within a block (`type` 1); type 4
    // entries hold none.
    struct BlockObject {
        uint32_t type = 0;
        std::vector<Polygon> polygons;
    };

    // The part of the track in one piece of the road, and what stands
    // beside it.
    struct Block {
        Vec3 centre{};
        std::array<Vec3, 4> corners{}; // its outline on the ground
        // As stored: all vertices, then the counts of their groups. The
        // vertices are the objects' (objectVertices), then the road's
        // (highResVertices), then more (lane markings').
        uint32_t highResVertices = 0, lowResVertices = 0, mediumResVertices = 0, duplicateVertices = 0,
                 objectVertices = 0;
        std::vector<Vec3> vertices;
        std::vector<uint32_t> colours; // per vertex, 0xAARRGGBB: baked lighting
        std::vector<int16_t> neighbours; // blocks drawn with this one
        // Seven polygon lists: 0, 2 and 4 the road at low, medium and high
        // detail; 1, 3 and 5 roadside polygons at the same levels (on a
        // few tracks); 6 the lane markings.
        std::array<std::vector<Polygon>, 7> polygons;
        std::array<std::vector<BlockObject>, 4> objectLayers;
        // The records after the polygons' counts, as stored (frd.md); the
        // lights and sounds are also decoded below.
        uint32_t startPosition = 0;
        std::vector<uint8_t> positions, polygonRoad, virtualRoad, objectRefs, polygonObjects, sounds, lights;
        // A light: where (16.16 fixed point in the file) and its type,
        // which picks how it looks (lightLook).
        struct Light {
            Vec3 position{};
            uint32_t type = 0;
        };
        // A sound placed on the track: where, the slot of the track's
        // tr3dNN.bnk it plays, and its kind (0 a loop, 1 a one-shot fired
        // at random; frd.md).
        struct PlacedSound {
            Vec3 position{};
            uint16_t slot = 0, kind = 0;
        };
        std::vector<Light> lightList;
        std::vector<PlacedSound> soundList;
    };

    // An object placed anywhere on the track: still (type 4, at
    // `reference`) or moving (type 3, by its keys).
    struct ExtraObject {
        struct Key {
            std::array<int32_t, 3> position{}; // 16.16 fixed point
            std::array<int16_t, 4> rotation{}; // 2.14 fixed point
        };
        uint32_t type = 0;
        Vec3 reference{};
        std::vector<Key> keys; // world positions; the vertices are relative to them
        int16_t keyDelay = 0;  // game ticks (64 a second) per key
        std::vector<Vec3> vertices; // relative to the reference (still) or the key (moving)
        std::vector<uint32_t> colours;
        std::vector<Polygon> polygons;
    };

    // A texture table entry: a picture of the track's .qfs sheet (or of
    // the lane markings') and where each corner of a polygon falls on it.
    struct Texture {
        int width = 0, height = 0;
        std::array<float, 8> corners{}; // u, v of polygon corners 0-3
        bool lane = false;
        uint16_t picture = 0; // the shape's number in the sheet
        uint32_t handle = 0; // room the game fills at load; leftovers
        uint32_t flags = 0;  // low byte: kSeeThrough, 0x04 (frd.md, Textures)
        static constexpr uint32_t kSeeThrough = 0x02;
    };

    std::vector<Block> blocks;
    std::vector<std::vector<ExtraObject>> extraObjects; // 4 groups per block, and one more
    std::vector<Texture> textures;
};

Error parseTrack(const std::vector<uint8_t>& data, Track& out);

// Which of a polygon's four vertices the game draws as corner k (0-3):
// the corner that takes Texture::corners[k] and the k-th in its winding.
// A block's lists keep the file's order (0x41b300); the placed objects'
// loop (0x41b640) queues vertices 1, 0, 3, 2, and the renderer (0x433840)
// and the back-face test go by the queue. So a placed object's picture is
// flipped left to right and its winding reversed against the file's
// (frd.md, Polygons).
inline int polygonSlot(int k, bool placedObject) { return placedObject ? k ^ 1 : k; }

// How a light of a type looks at a game tick (64 a second), from
// nfs3.exe's table at 0x55e3dc (frd.md, Lights): drawn with glw0 in its
// colour (ARGB), size units from its centre to its edges, unless it's
// blinking off.
struct LightLook {
    uint32_t color = 0;
    float size = 0;
    bool shown = true;
};
LightLook lightLook(uint32_t type, uint64_t tick);
// A light's ARGB colour dimmed by the effects intensity (the Direct3D
// renderer's SFX_INTENSITY option, 0-1; 1 leaves it as it is), each byte
// alpha included, as 0x491223 does.
uint32_t dimLight(uint32_t color, float intensity);

// Which road detail the game draws a block at (0 low, 1 medium, 2 high),
// or -1 for not at all, by the distance on the ground from the camera to
// the block's centre (0x41cdc0, 0x41aa60). The distances are set by
// 0x41df10 and 0x41d620 for the main view of a one-player race: 500 to
// draw, 150 for medium and 110 for high, times 1, 0.55 or 0.4 by day and
// 0.75, 0.41 or 0.3 by night for view levels 0-1, 2 and 3 (0x6fbc28; the
// highest, 1, is what a fresh config.dat gets on a fast machine).
int blockDetailAt(float distance, bool night, int viewLevel = 1);

// The game drops a polygon whose first queued corner (polygonSlot(0))
// is more than the view's +0x30 ahead along the view (0x41b3bf, and the
// same in the placed objects' loop and the headlight pass). That's the
// view's first distance (0x5dd104), 500 for the main view of a one-player
// race (0x41df10; 400 split screen), which 0x41d620 doesn't scale: by
// night and at every view level it stays 500, while the blocks' reach
// (+0x34) shrinks.
constexpr float kPolygonReach = 500;
inline bool polygonInReach(float depthAhead) { return depthAhead <= kPolygonReach; }

// A car's headlights (frd.md, the headlights), as nfs3.exe sets them up
// (0x42d160) and projects them (0x41a550, 0x41a3e0): a light frame whose
// forward is the car's tilted down, and the picture glw2 of sfx.fsh
// projected through a 90-degree square frustum along it, coloured by how
// far off the axis and how far along it a point is. Positions and vectors
// in the track's coordinates.
struct Headlight {
    std::array<float, 3> at{}, right{}, up{}, forward{};
    float nearPlane = 1, fadeFrom = 10, reach = 40; // fading from fadeFrom to nothing at reach
    uint32_t colour = 0;                             // ARGB, alpha too
};
// beams: the car's light state (0 off, 1 low, 2 high; the headlight key
// steps through them, 0x40de50). projected: the projtex option is on with
// the z-buffer (0x6fbc3c, 0x7a3a60). Low beams fade from 10 to 40 in
// 0xB0B0B0B0, tilted down 1/16 of a turn; high from 20 to 60 in
// 0xE0E0E0E0, 1/32 of a turn; without projection 20 or 40 to 40 or 60, in
// white. The car's heading in radians (atan2(x, z) of its forward) and
// intensity (1 for the first car the pass lights, 0x41a800).
Headlight carHeadlight(int beams, bool projected, const std::array<float, 3>& at, float heading,
                       float intensity = 1);
// Where nfs3.exe puts a car's light (0x42d160): the car's position
// (+0x98) plus its up row (+0xCC) times +0x10C (its up . the road's
// normal, 1 on level road), plus a quarter of its half length (+0x11C)
// along +0x67C. That is the forward row of the light's own frame
// (+0x664), the car's rows turned down by the tilt, which the same
// function rewrites only after taking the place: the frame of the step
// before, the light's forward when the car turns slowly. Here the car
// is upright: its up the track's.
std::array<float, 3> headlightPlace(const Headlight& light, const std::array<float, 3>& carAt, float halfLength,
                                    float upright = 1);
// A point in the light: its place on the picture, its colour (0 outside
// the reach) and the game's outcodes: 1 left, 2 right, 4 below, 8 above
// the frustum, 0x10 before the near plane or behind, 0x20 past the reach.
struct HeadlightPoint {
    float u = 0, v = 0;
    uint32_t colour = 0;
    uint8_t outside = 0;
};
HeadlightPoint headlightAt(const Headlight& light, const std::array<float, 3>& point);
// Whether the game draws a triangle of the projected picture (0x41a360,
// 0x41a120): not all its corners outside the same side, none past the
// reach, and one at least with alpha 8 or more.
bool headlightTriangle(const HeadlightPoint& a, const HeadlightPoint& b, const HeadlightPoint& c);
// Each byte of an ARGB colour times k / 256 (k 0-255), as the game scales
// light colours (0x41a3e0, 0x491223).
uint32_t scaleArgb(uint32_t colour, uint32_t k);

// A placed sound's volume (0-127) at a distance in track units, as
// 0x41d210 has it: 2^16 over (distance in 1/16 units)^2 / 2^10, at most 127.
int placedSoundVolume(float distance);

// When the game fires a one-shot placed sound (kind 1; 0x41d27d): on a
// frame whose (count & 63) >> 3 is the number picked, 0 to 7, which is
// then picked afresh. One pick shared by all, from rand().
struct OneShotTimer {
    int pick = 0;
    template <typename Random>
    bool fires(uint32_t frame, Random& random) {
        if (static_cast<int>((frame & 63) >> 3) != pick)
            return false;
        pick = random.next() & 7;
        return true;
    }
};

} // namespace reluva::nfs
