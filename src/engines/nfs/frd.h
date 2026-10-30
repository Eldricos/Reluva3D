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
