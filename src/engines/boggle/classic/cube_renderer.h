// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/cube_geometry.h"
#include "engines/boggle/classic/projection.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/die_orientation.h"
#include <optional>
namespace reluva::boggle {
class CubeRenderer {
public:
    Error prepare(const CubeGeometry& geometry);
    bool active() const {return !triangles_.empty();}
    size_t triangleCount() const {return triangles_.size();}
    void draw(Surface& screen,const Board& board,const CameraProjection& camera,
              const image::Image& letters,const image::Image& qu,const std::vector<size_t>& selected,
              uint32_t seed,bool autoOrient) const;
    ClassicPoint cellCenter(const CameraProjection& camera,size_t cell,size_t side) const;
    std::optional<size_t> cellAt(const CameraProjection& camera,int x,int y,size_t side) const;
    ClassicPoint cellCenter(const CameraProjection& camera,const Board& board,size_t cell,uint32_t seed,bool autoOrient) const;
    std::optional<size_t> cellAt(const CameraProjection& camera,const Board& board,int x,int y,uint32_t seed,bool autoOrient) const;
private:
    ClassicPoint center(const CameraProjection&,size_t cell,size_t side,const Board*,uint32_t seed,bool autoOrient) const;
    std::optional<size_t> pick(const CameraProjection&,int x,int y,size_t side,const Board*,uint32_t seed,bool autoOrient) const;
    std::array<int,4> topUv(size_t face,const DiePose& base) const;
    struct Corner {std::array<int,3> position{};std::array<double,3> normal{};int u=0,v=0;};
    struct Triangle {std::array<Corner,3> corners;int face=-1;std::array<double,3> diffuse{},emissive{};};
    std::vector<Triangle> triangles_;
    std::array<std::array<int,3>,6> centers_{};
    std::array<std::array<std::array<double,3>,2>,6> tangents_{};
    bool rounded_=false;
};
} // namespace reluva::boggle
