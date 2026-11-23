// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::sfc {

/**
 * A ship's, base's or asteroid's model, a `.mod` in `Assets/MODELS/<name>/`
 * (docs/engines/sfc/games/sfc2/formats/mod.md): its named points
 * (hardpoints), its materials, and one mesh per level of detail.
 */
class Model {
public:
    /** A named point on the model: where a weapon or a system is. */
    struct Point {
        std::array<float, 3> position{};
        std::string name;
    };

    /** How triangles are drawn: a colour, and maybe a texture and its illumination map. */
    struct Material {
        uint32_t flags = 0;
        uint32_t color = 0;       // 0x00RRGGBB as stored; 0xffffff in every retail model
        std::string texture;      // the texture's name, without .bmp; empty for none
        std::string illumination; // its self-illumination map's name, the same way
    };

    /** One corner of a triangle. */
    struct Corner {
        uint16_t vertex = 0;
        uint16_t piece = 0; // what the vertex index's upper half holds: 0, but in a broken (_brk) model
        std::array<float, 3> unknown{};
        float u = 0;
        float v = 0;
    };

    /** A triangle, and how it's drawn: a polygon's, a few of four corners cut into two. */
    struct Triangle {
        uint32_t material = 0;
        std::array<float, 3> normal{};
        std::array<Corner, 3> corners{};
    };

    /** One level of detail. */
    struct Mesh {
        std::vector<std::array<float, 3>> vertices;
        std::vector<Triangle> triangles;
    };

    /** Reads a whole file, checking each count against what it counts. */
    Error read(const std::vector<uint8_t> &data);

    /** The header's first number: 512 in every retail model. */
    uint32_t version() const { return _version; }
    /** The distance of the farthest vertex from the origin, as stored. */
    float radius() const { return _radius; }
    /** The header's other numbers: six floats after the level count (docs say what's guessed of them). */
    const std::array<float, 6> &settings() const { return _settings; }
    /** The named points (PNTS), in the file's order. */
    const std::vector<Point> &points() const { return _points; }
    /** The materials (MATS), which the triangles count from 0. */
    const std::vector<Material> &materials() const { return _materials; }
    /** The levels of detail, finest first (guessed from their sizes). */
    const std::vector<Mesh> &meshes() const { return _meshes; }

private:
    uint32_t _version = 0;
    float _radius = 0;
    std::array<float, 6> _settings{};
    std::vector<Point> _points;
    std::vector<Material> _materials;
    std::vector<Mesh> _meshes;
};

} // namespace reluva::sfc
