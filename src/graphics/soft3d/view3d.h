// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/soft3d/rasterizer.h"

#include <cmath>
#include <vector>

// Small 3D helpers for software-drawn views: vectors, an orbiting camera, and
// near-plane clipping. World space is right-handed with z up.

namespace reluva::soft3d {

/** A point or direction in world or view space. */
struct Vec {
    float x;
    float y;
    float z;
};
/** Vector arithmetic. */
///@{
inline Vec operator+(Vec a, Vec b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec operator-(Vec a, Vec b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec operator*(Vec a, float s) {
    return {a.x * s, a.y * s, a.z * s};
}
inline float dot(Vec a, Vec b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec cross(Vec a, Vec b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
///@}
/** `a` at unit length; the zero vector stays zero. */
inline Vec normalize(Vec a) {
    const float len = std::sqrt(dot(a, a));
    return len > 0 ? a * (1.0f / len) : Vec{0, 0, 0};
}

/** A polygon corner in view space (x right, y up, z away from the camera). */
struct ViewVertex {
    float x;
    float y;
    float z;
    float u;
    float v;
    float s = 1; // light level
    // a colour to light by, each channel 0 to 1
    float r = 1;
    float g = 1;
    float b = 1;
};

/**
 * A camera looking at `centre` from `distance` away, turned by yaw about z and
 * raised by pitch, with a perspective projection onto a w x h screen.
 */
struct OrbitCamera {
    Vec eye;
    Vec right;
    Vec up;
    Vec forward;
    float focal;
    float nearZ;
    float halfW;
    float halfH;

    /**
     * Places the camera.
     * @param yaw Radians about z; 0 puts the camera on the -y side.
     * @param pitch Radians up from level.
     */
    OrbitCamera(Vec centre, float yaw, float pitch, float distance, int w, int h)
        : focal(float(h) * 1.2f), nearZ(distance * 0.01f), halfW(float(w) / 2), halfH(float(h) / 2) {
        const Vec toCamera{std::cos(pitch) * std::sin(yaw), -std::cos(pitch) * std::cos(yaw), std::sin(pitch)};
        eye = centre + toCamera * distance;
        forward = toCamera * -1.0f;
        right = normalize(cross(forward, Vec{0, 0, 1}));
        up = cross(right, forward);
    }
    /** A world point in view space, with its texture coordinates. */
    ViewVertex toView(Vec p, float u = 0, float v = 0) const {
        const Vec d = p - eye;
        return {dot(d, right), dot(d, up), dot(d, forward), u, v};
    }
    /** A world direction in view space. */
    Vec directionToView(Vec n) const { return {dot(n, right), dot(n, up), dot(n, forward)}; }
    /** A view-space point on the screen; z must be above 0 (clipNear first). */
    ScreenVertex project(const ViewVertex &p) const {
        return {halfW + focal * p.x / p.z, halfH - focal * p.y / p.z, 1.0f / p.z, p.u, p.v, p.s, p.r, p.g, p.b};
    }
};

/** The part of the polygon with z >= nearZ (Sutherland-Hodgman). */
inline std::vector<ViewVertex> clipNear(const std::vector<ViewVertex> &in, float nearZ) {
    std::vector<ViewVertex> out;
    for (size_t i = 0; i < in.size(); ++i) {
        const ViewVertex &a = in[i];
        const ViewVertex &b = in[(i + 1) % in.size()];
        const bool aIn = a.z >= nearZ;
        const bool bIn = b.z >= nearZ;
        if (aIn)
            out.push_back(a);
        if (aIn != bIn) {
            const float t = (nearZ - a.z) / (b.z - a.z);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, nearZ, a.u + (b.u - a.u) * t,
                           a.v + (b.v - a.v) * t, a.s + (b.s - a.s) * t, a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                           a.b + (b.b - a.b) * t});
        }
    }
    return out;
}

} // namespace reluva::soft3d
