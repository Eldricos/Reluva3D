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
