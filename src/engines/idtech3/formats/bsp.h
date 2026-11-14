// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::idtech3 {

// A compiled map: Quake III's ("IBSP" version 46) or Raven's ("RBSP"
// version 1, Jedi Outcast's), which adds light styles. Field names follow
// the lumps described in docs/engines/idtech3/formats/bsp.md and rbsp.md;
// all values are little-endian in the file and converted on load.

// Raven's maps light a surface with up to four styles, each a lightmap (or
// vertex colours) scaled by its style's current colour; Quake III's have
// one, style 0, always full.
constexpr int kLightStyles = 4;
constexpr uint8_t kNoStyle = 255;

struct BspShader {
    std::string name;
    int32_t surfaceFlags = 0;
    int32_t contentFlags = 0;
};

struct BspPlane {
    std::array<float, 3> normal{};
    float dist = 0;
};

struct BspNode {
    int32_t plane = 0;
    std::array<int32_t, 2> children{}; // negative: -(leaf + 1)
    std::array<int32_t, 3> mins{};
    std::array<int32_t, 3> maxs{};
};

struct BspLeaf {
    int32_t cluster = 0; // -1: outside the map
    int32_t area = 0;
    std::array<int32_t, 3> mins{};
    std::array<int32_t, 3> maxs{};
    int32_t firstLeafSurface = 0;
    int32_t numLeafSurfaces = 0;
    int32_t firstLeafBrush = 0;
    int32_t numLeafBrushes = 0;
};

// Model 0 is the world; the others are brush entities' ("*1", "*2"...).
struct BspModel {
    std::array<float, 3> mins{};
    std::array<float, 3> maxs{};
    int32_t firstSurface = 0;
    int32_t numSurfaces = 0;
    int32_t firstBrush = 0;
    int32_t numBrushes = 0;
};

struct BspBrush {
    int32_t firstSide = 0;
    int32_t numSides = 0;
    int32_t shader = 0;
};

struct BspBrushSide {
    int32_t plane = 0;
    int32_t shader = 0;
    int32_t surface = -1; // RBSP: the drawn surface the side made; -1 if none
};

// Lightmap coordinates and colours for each of its surface's styles (only
// the first in Quake III's maps).
struct BspVertex {
    std::array<float, 3> pos{};
    std::array<float, 2> st{}; // texture coordinates
    std::array<std::array<float, 2>, kLightStyles> lightmap{};
    std::array<float, 3> normal{};
    std::array<std::array<uint8_t, 4>, kLightStyles> color{};
};

struct BspFog {
    std::string shader;
    int32_t brush = 0;       // RBSP: -1 for the whole level's fog (the worldspawn's "fog")
    int32_t visibleSide = 0; // -1: none
};

enum class SurfaceType : int32_t { kBad = 0, kPlanar = 1, kPatch = 2, kTriangleSoup = 3, kFlare = 4 };

struct BspSurface {
    int32_t shader = 0;
    int32_t fog = 0; // -1: none
    SurfaceType type = SurfaceType::kBad;
    int32_t firstVertex = 0;
    int32_t numVertices = 0;
    int32_t firstIndex = 0;
    int32_t numIndices = 0;
    // Each style's number (kNoStyle: unused), for its lightmaps and its
    // vertex colours; then each style's lightmap (-1: none) and where in it.
    std::array<uint8_t, kLightStyles> lightmapStyles{0, kNoStyle, kNoStyle, kNoStyle};
    std::array<uint8_t, kLightStyles> vertexStyles{0, kNoStyle, kNoStyle, kNoStyle};
    std::array<int32_t, kLightStyles> lightmap{0, -1, -1, -1};
    std::array<std::array<int32_t, 2>, kLightStyles> lightmapPos{};
    std::array<int32_t, 2> lightmapSize{};
    std::array<float, 3> lightmapOrigin{};
    std::array<std::array<float, 3>, 3> lightmapVecs{}; // s, t, normal; patches: bounds, bounds, normal
    // patch control points
    int32_t patchWidth = 0;
    int32_t patchHeight = 0;
};

constexpr int kLightmapSize = 128; // lightmaps are 128x128 RGB

struct Bsp {
    bool raven = false; // RBSP
    int32_t version = 0;
    std::string entities; // the entity lump's text, see entities.h
    std::vector<BspShader> shaders;
    std::vector<BspPlane> planes;
    std::vector<BspNode> nodes;
    std::vector<BspLeaf> leafs;
    std::vector<int32_t> leafSurfaces;
    std::vector<int32_t> leafBrushes;
    std::vector<BspModel> models;
    std::vector<BspBrush> brushes;
    std::vector<BspBrushSide> brushSides;
    std::vector<BspVertex> vertices;
    std::vector<int32_t> indices; // relative to a surface's firstVertex
    std::vector<BspFog> fogs;
    std::vector<BspSurface> surfaces;
    std::vector<uint8_t> lightmaps; // kLightmapSize^2 * 3 bytes each
    // The light grid's points (see bsp.md), each the one in gridPoints that
    // gridIndex names: in Quake III's maps a point each, in order; Raven's
    // share points that are the same.
    struct GridPoint {
        std::array<std::array<uint8_t, 3>, kLightStyles> ambient{};
        std::array<std::array<uint8_t, 3>, kLightStyles> directed{};
        std::array<uint8_t, kLightStyles> styles{0, kNoStyle, kNoStyle, kNoStyle};
        std::array<uint8_t, 2> latLong{}; // the direction: longitude, latitude
    };
    std::vector<GridPoint> gridPoints;
    std::vector<uint32_t> gridIndex;
    int32_t visClusters = 0;
    int32_t visClusterBytes = 0;
    std::vector<uint8_t> visData; // visClusters * visClusterBytes bits

    // Values the original engine tolerates and parseBsp() corrected, as
    // described in bsp.md: how many surfaces had each.
    struct Fixes {
        int lightmaps = 0; // lightmap past the end: now -1 (vertex lighting, or that style unlit)
        int fogs = 0;      // fog that doesn't exist: now -1
        int flares = 0;    // flare or Bad surface with vertices or indices: now none
        int types = 0;     // unknown surface type: now Bad
        int total() const { return lightmaps + fogs + flares + types; }
    } fixes;

    size_t lightmapCount() const { return lightmaps.size() / (kLightmapSize * kLightmapSize * 3); }
    // Whether cluster `to` can be seen from cluster `from`. Without vis data
    // (or for clusters outside the map) everything is visible.
    bool clusterVisible(int from, int to) const;
};

// Parse a whole .bsp file. Checks the header, that every lump lies inside
// the file and holds whole records, and that every index between lumps is
// in range, so the rest of the engine can trust the result. Out-of-range
// values the original engine tolerates are corrected instead (see `fixes`);
// errors name the record, the field and its value.
Error parseBsp(const std::vector<uint8_t> &data, Bsp &out);

} // namespace reluva::idtech3
