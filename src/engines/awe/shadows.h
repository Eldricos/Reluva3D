// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ac2.h"

#include <array>
#include <cstdint>
#include <vector>

// The characters' shadows as motoe.exe makes them (0x428250, 0x402ba0):
// planar. Each textured mesh is flattened onto the floor (y = 0) away from
// one point light, the scene's lights' mean position raised by 1500; the
// pixels it covers are darkened to 192/255 (where shadows overlap, no
// more). See docs/formats/awe/shadows.md.

namespace reluva::awe {

// The shadows' light: the mean position of the scene's LITEs (only those
// marked shadowOnly, if any are), raised by 1500; (0, 1500, 0)
// with none.
std::array<float, 3> shadowLight(const Ac2Scene& scene);

// Where the line from the light through a point meets the floor
// (D3DXMatrixShadow with the plane y = 0). The point must be below the
// light.
std::array<float, 3> flattenOntoFloor(const std::array<float, 3>& light, const float point[3]);

// A screen's coverage, triangles filled in (pixel centres inside, edges
// shared without gaps). Tracks the box it covers.
class CoverageMask {
public:
    CoverageMask(int width, int height) : w_(width), h_(height), bits_(size_t(width) * size_t(height), 0) {}
    void clear();
    void fillTriangle(const float a[2], const float b[2], const float c[2]);
    bool at(int x, int y) const { return bits_[size_t(y) * size_t(w_) + size_t(x)] != 0; }
    bool empty() const { return maxX_ < minX_; }
    int width() const { return w_; }
    int height() const { return h_; }
    // The covered box, inclusive.
    int minX() const { return minX_; }
    int minY() const { return minY_; }
    int maxX() const { return maxX_; }
    int maxY() const { return maxY_; }

private:
    int w_, h_;
    std::vector<uint8_t> bits_;
    int minX_ = 1 << 30, minY_ = 1 << 30, maxX_ = -1, maxY_ = -1;
};

// The shadow's darkening: a fragment's colour times this (the game's quad,
// black at alpha 63: 192/255).
constexpr float kShadowShade = 192.0f / 255.0f;

} // namespace reluva::awe
