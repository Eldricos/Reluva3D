// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"
#include "engines/awe/ac2.h"
#include "engines/awe/model.h"

// A scene's camera (its CAMR record), and where its characters stand.
// The game hands the record's fields to Granny's camera
// (GrannyBuildCameraMatrices); the matrices here are the ones granny2.dll
// builds, matched to within a thousandth (docs/formats/awe/ac2.md).

namespace reluva::awe {

struct SceneCamera {
    float fovY = 0.785f, nearPlane = 10, farPlane = 10000; // vertical, radians
    float position[3] = {0, 0, 0};     // the point the camera turns about
    float elevAzimRoll[3] = {0, 0, 0}; // radians
    float offset[3] = {0, 0, 0};       // from that point, in the camera's axes

    static SceneCamera fromRecord(const Ac2Record& camr);

    // World to camera: back by the offset, the rotation's inverse, then
    // the position's inverse. The rotation is azimuth about Y, then
    // elevation about X, then roll about Z; the camera looks down -Z.
    gfx::Mat4 view() const;
    // For gfx's depth range (0 near, 1 far).
    gfx::Mat4 projection(float aspect = 4.0f / 3.0f) const;
    gfx::Mat4 viewProjection(float aspect = 4.0f / 3.0f) const;
    // Where the camera is: the position, plus the offset turned.
    gfx::Vec3 eye() const;
    // Where a world point lands on a width x height screen (pixels, y
    // down); false if it's behind the camera.
    bool project(const float world[3], int width, int height, float& x, float& y) const;
};

// The game turns every model to its own axes when it loads it
// (GrannyComputeBasisConversion: right +X, up +Y, back +Z, the file's own
// units), from 3D Studio MAX's (up +Z, back -Y): (x, y, z) -> (x, z, -y).
// Then a character stands at its position, turned by rotY about Y.
gfx::Mat4 characterMatrix(float x, float y, float z, float rotY);

} // namespace reluva::awe
