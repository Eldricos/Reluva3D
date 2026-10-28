// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Small vector and 3x3 matrix helpers for the simulation and collisions.

#include "engines/age/pkg.h"

#include <cmath>

namespace reluva::age {

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
inline Vec3 operator-(const Vec3& a) { return {-a[0], -a[1], -a[2]}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { return a = a + b; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { return a = a - b; }
inline float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalized(const Vec3& a) {
    const float l = length(a);
    return l > 1e-9f ? a * (1 / l) : Vec3{0, 0, 0};
}

// A rotation (or any 3x3 matrix) as its three columns: where X, Y and Z go.
struct Mat3 {
    Vec3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1};

    Vec3 operator*(const Vec3& v) const { return x * v[0] + y * v[1] + z * v[2]; }
    // The transpose's product: a world direction in this frame.
    Vec3 inverseTimes(const Vec3& v) const { return {dot(x, v), dot(y, v), dot(z, v)}; }

    static Mat3 rotationY(float angle) {
        const float c = std::cos(angle), s = std::sin(angle);
        return {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
    }
    // Turned by `angle` (radians) about the unit `axis`.
    static Mat3 rotation(const Vec3& axis, float angle) {
        const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
        const float x = axis[0], y = axis[1], z = axis[2];
        return {{t * x * x + c, t * x * y + s * z, t * x * z - s * y},
                {t * x * y - s * z, t * y * y + c, t * y * z + s * x},
                {t * x * z + s * y, t * y * z - s * x, t * z * z + c}};
    }
    Mat3 operator*(const Mat3& m) const { return {*this * m.x, *this * m.y, *this * m.z}; }
    // Square again after rounding errors build up.
    void orthonormalize() {
        x = normalized(x);
        z = normalized(cross(x, y));
        y = cross(z, x);
    }
};

} // namespace reluva::age
