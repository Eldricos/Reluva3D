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
        uint16_t unknown = 0;
        uint8_t flags = 0;
        uint8_t extra = 0;
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
        // Counts of the records not decoded yet, kept as stored.
        uint32_t startPosition = 0;
        std::vector<uint8_t> positions, polygonRoad, virtualRoad, objectRefs, polygonObjects, sounds, lights;
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
        int16_t keyDelay = 0;  // ticks per key, by the look of it
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
        uint32_t unknown1 = 0, unknown2 = 0;
    };

    std::vector<Block> blocks;
    std::vector<std::vector<ExtraObject>> extraObjects; // 4 groups per block, and one more
    std::vector<Texture> textures;
};

Error parseTrack(const std::vector<uint8_t>& data, Track& out);

} // namespace reluva::nfs
