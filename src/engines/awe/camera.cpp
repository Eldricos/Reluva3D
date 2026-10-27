// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/camera.h"

#include <cmath>

namespace reluva::awe {

namespace {

using gfx::Mat4;

Mat4 translation(float x, float y, float z) {
    Mat4 m = gfx::identity();
    m[12] = x, m[13] = y, m[14] = z;
    return m;
}

// Rotations about an axis (column-major, applied as M * v).
Mat4 rotationX(float a) {
    Mat4 m = gfx::identity();
    m[5] = std::cos(a), m[6] = std::sin(a), m[9] = -std::sin(a), m[10] = std::cos(a);
    return m;
}
Mat4 rotationY(float a) {
    Mat4 m = gfx::identity();
    m[0] = std::cos(a), m[2] = -std::sin(a), m[8] = std::sin(a), m[10] = std::cos(a);
    return m;
}
Mat4 rotationZ(float a) {
    Mat4 m = gfx::identity();
    m[0] = std::cos(a), m[1] = std::sin(a), m[4] = -std::sin(a), m[5] = std::cos(a);
    return m;
}

} // namespace

SceneCamera SceneCamera::fromRecord(const Ac2Record& r) {
    SceneCamera c;
    c.fovY = r.f("fov");
    c.nearPlane = r.f("near");
    c.farPlane = r.f("far");
    const char* const pos[3] = {"x", "y", "z"};
    const char* const ear[3] = {"pitch", "yaw", "roll"};
    const char* const off[3] = {"offsetX", "offsetY", "offsetZ"};
    for (int k = 0; k < 3; k++) {
        c.position[k] = r.f(pos[k]);
        c.elevAzimRoll[k] = r.f(ear[k]);
        c.offset[k] = r.f(off[k]);
    }
    return c;
}

Mat4 SceneCamera::view() const {
    // The inverse of T(position) * Ry(azimuth) * Rx(elevation) * Rz(roll),
    // then back by the offset.
    Mat4 m = translation(-offset[0], -offset[1], -offset[2]);
    m = gfx::multiply(m, rotationZ(-elevAzimRoll[2]));
    m = gfx::multiply(m, rotationX(-elevAzimRoll[0]));
    m = gfx::multiply(m, rotationY(-elevAzimRoll[1]));
    return gfx::multiply(m, translation(-position[0], -position[1], -position[2]));
}

gfx::Vec3 SceneCamera::eye() const {
    Mat4 m = translation(position[0], position[1], position[2]);
    m = gfx::multiply(m, rotationY(elevAzimRoll[1]));
    m = gfx::multiply(m, rotationX(elevAzimRoll[0]));
    m = gfx::multiply(m, rotationZ(elevAzimRoll[2]));
    return gfx::transformPoint(m, {offset[0], offset[1], offset[2]});
}

bool SceneCamera::floorAt(float sx, float sy, int width, int height, float& x, float& z) const {
    // The ray through the pixel, in the camera's axes (looking down -Z),
    // turned to the world's.
    const float aspect = float(width) / float(height), t = std::tan(fovY / 2);
    const float nx = 2 * sx / float(width) - 1, ny = 1 - 2 * sy / float(height);
    Mat4 turn = rotationY(elevAzimRoll[1]);
    turn = gfx::multiply(turn, rotationX(elevAzimRoll[0]));
    turn = gfx::multiply(turn, rotationZ(elevAzimRoll[2]));
    const gfx::Vec3 dir = gfx::transformPoint(turn, {nx * t * aspect, ny * t, -1});
    const gfx::Vec3 from = eye();
    if (dir[1] >= 0)
        return false; // it doesn't come down to the floor
    const float k = -from[1] / dir[1];
    x = from[0] + k * dir[0];
    z = from[2] + k * dir[2];
    return true;
}

Mat4 SceneCamera::projection(float aspect) const {
    return gfx::perspective(fovY, aspect, nearPlane > 0 ? nearPlane : 1, farPlane > nearPlane ? farPlane : 10000);
}

Mat4 SceneCamera::viewProjection(float aspect) const { return gfx::multiply(projection(aspect), view()); }

bool SceneCamera::project(const float world[3], int width, int height, float& x, float& y) const {
    const Mat4 m = viewProjection(float(width) / float(height));
    float clip[4];
    for (int row = 0; row < 4; row++)
        clip[row] = m[size_t(row)] * world[0] + m[size_t(4 + row)] * world[1] + m[size_t(8 + row)] * world[2] +
                    m[size_t(12 + row)];
    if (clip[3] <= 0)
        return false;
    x = (clip[0] / clip[3] + 1) * 0.5f * float(width);
    y = (1 - clip[1] / clip[3]) * 0.5f * float(height);
    return true;
}

Mat4 characterMatrix(float x, float y, float z, float rotY) {
    Mat4 basis{};
    basis[0] = 1;  // MAX x -> x
    basis[6] = -1; // MAX y -> -z
    basis[9] = 1;  // MAX z -> y
    basis[15] = 1;
    return gfx::multiply(gfx::multiply(translation(x, y, z), rotationY(rotY)), basis);
}

} // namespace reluva::awe
