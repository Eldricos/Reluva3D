// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/resources.h"
namespace reluva::boggle {
inline constexpr std::array<const char*,2> classicCubeMeshPaths={"Scripts/Rounded.x","Scripts/Die.x"};
// Validated runtime data for a future complete cube renderer. Polygon topology,
// normals and UVs are retained; body surfaces must not be discarded.
class CubeGeometry {
public:
    Error prepare(const Mesh& source,double scale);
    Mesh mesh;
    // Material groups facing +X, -X, +Y, -Y, +Z, -Z; independent of file order.
    std::array<size_t,6> letters{};
    double appliedScale=0;
};
double classicCubeMeshScale(std::string_view path);
Error readClassicCubeGeometry(const Resources& resources,std::string_view path,CubeGeometry& out);
} // namespace reluva::boggle
