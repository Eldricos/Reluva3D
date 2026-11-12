// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <array>
#include <cstdint>
namespace reluva::boggle {
struct ClassicPoint { int x=0,y=0; };
struct ProjectedVertex {
    int x=0,y=0,u=0,v=0;
    int64_t reciprocalDepth=0;
};
struct CameraParameters {
    double tilt=0,pan=0;
    std::array<double,3> position{};
    double fov=90;
    int width=640,height=480;
};
// A perspective camera with square pixels and horizontal field of view.
// Its local translation precedes yaw/tilt; world points are supplied as
// rational coordinates. Quantized coefficients and integer projection keep
// raster coordinates stable across platforms. Invalid/near-clipped points
// have zero reciprocal depth.
class CameraProjection {
public:
    CameraProjection() = default;
    explicit CameraProjection(const CameraParameters& parameters);
    ProjectedVertex project(int x,int y,int z,int units,int numerator=1,int denominator=1) const;
private:
    std::array<int64_t,9> basis_{};
    std::array<int64_t,3> position_{};
    int64_t focal_=0;
    int width_=0,height_=0;
};
// A plane fitted to four measured corners, ordered TL, TR, BR, BL.
// Integer homogeneous coordinates keep geometry identical across platforms.
class PlaneProjection {
public:
    explicit PlaneProjection(const std::array<ClassicPoint,4>& corners);
    ProjectedVertex project(int u,int v,int units,int scaleNumerator=1,int scaleDenominator=1) const;
private:
    std::array<int64_t,8> coefficients_{};
    int64_t determinant_=0;
};
} // namespace reluva::boggle
