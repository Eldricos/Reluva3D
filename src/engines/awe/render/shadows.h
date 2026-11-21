// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/formats/ac2.h"

#include <array>
#include <cstdint>
#include <vector>

// The characters' shadows as motoe.exe makes them (0x428250, 0x402ba0):
// planar. Each textured mesh is flattened onto the floor (y = 0) away from
// one point light, the scene's lights' mean position raised by 1500; the
// pixels it covers are darkened to 192/255 (where shadows overlap, no
// more). See docs/engines/awe/games/motoe/systems/shadows.md.

namespace reluva::awe {

/**
 * The shadows' light: the mean position of the scene's LITEs (only those
 * marked shadowOnly, if any are), raised by 1500; (0, 1500, 0)
 * with none. The game adds the mesh's world translation to the lights'
 * sum (but not to their count); it's 0 for a skinned mesh, and `origin`
 * for a rigid one.
 */
std::array<float, 3> shadowLight(const Ac2Scene &scene, const float origin[3] = nullptr);

/**
 * Where the line from the light through a point meets the floor
 * (D3DXMatrixShadow with the plane y = 0). The point must be below the
 * light.
 */
std::array<float, 3> flattenOntoFloor(const std::array<float, 3> &light, const float point[3]);
/** The same onto the plane y = `height`. */
std::array<float, 3> flattenOntoPlane(const std::array<float, 3> &light, float height, const float point[3]);

/**
 * A screen's coverage, triangles filled in (pixel centres inside, edges
 * shared without gaps). Tracks the box it covers.
 */
class CoverageMask {
public:
    /** An empty mask of width x height pixels. */
    CoverageMask(int width, int height) : _w(width), _h(height), _bits(size_t(width) * size_t(height), 0) {}
    /** Empties it, clearing only the box covered. */
    void clear();
    /** Covers the pixels whose centres are inside the triangle (either winding), in screen pixels. */
    void fillTriangle(const float a[2], const float b[2], const float c[2]);
    /** Covers a pixel, which must be on the screen. */
    void set(int x, int y);
    /** Whether a pixel (on the screen) is covered. */
    bool at(int x, int y) const { return _bits[size_t(y) * size_t(_w) + size_t(x)] != 0; }
    /** Whether nothing is covered. */
    bool empty() const { return _maxX < _minX; }
    /** The screen's size. */
    ///@{
    int width() const { return _w; }
    int height() const { return _h; }
    ///@}
    /** The covered box, inclusive; empty() when the maxima are below the minima. */
    ///@{
    int minX() const { return _minX; }
    int minY() const { return _minY; }
    int maxX() const { return _maxX; }
    int maxY() const { return _maxY; }
    ///@}

private:
    int _w;
    int _h;
    std::vector<uint8_t> _bits;
    int _minX = 1 << 30;
    int _minY = 1 << 30;
    int _maxX = -1;
    int _maxY = -1;
};

/**
 * The game's stencil as the frame is drawn, on the CPU, for a device with
 * no stencil buffer (with one, the renderer's is used): each pixel's
 * stencil value and the depth of the models drawn
 * there so far. Triangles are screen x, y and depth (0 near, 1 far).
 */
class ShadowStencil {
public:
    /** A width x height stencil, all 0, its depth all far. */
    ShadowStencil(int width, int height)
        : _w(width), _h(height), _stencil(size_t(width) * size_t(height), 0),
          _depth(size_t(width) * size_t(height), kFar) {}
    /** Back to all 0 and far, for the next frame. */
    void clear();
    /**
     * Where the triangle passes the depth test (as near or nearer: D3D's
     * LESSEQUAL), the stencil becomes `ref`; with `writeDepth`, so does the
     * depth. A shadow writes no depth, a body does.
     */
    void fillTriangle(const float a[3], const float b[3], const float c[3], uint8_t ref, bool writeDepth);
    /** Stencil 0 wherever `cover` (a byte a pixel) isn't 0: a picture drawn over. */
    void reset(const std::vector<uint8_t> &cover);
    /** A pixel's stencil value (on the screen). */
    uint8_t at(int x, int y) const { return _stencil[size_t(y) * size_t(_w) + size_t(x)]; }
    /** The pixels whose stencil is `ref`, into `out` (cleared first). */
    void select(uint8_t ref, CoverageMask &out) const;

private:
    static constexpr float kFar = 3.0e38f;
    int _w;
    int _h;
    std::vector<uint8_t> _stencil;
    std::vector<float> _depth;
    // The box anything has touched, inclusive.
    int _minX = 1 << 30;
    int _minY = 1 << 30;
    int _maxX = -1;
    int _maxY = -1;
};

/** The shadow's darkening: a fragment's colour times this (the game's quad, black at alpha 63: 192/255). */
constexpr float kShadowShade = 192.0f / 255.0f;

} // namespace reluva::awe
