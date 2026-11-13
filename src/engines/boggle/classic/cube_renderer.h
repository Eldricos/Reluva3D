// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/cube_geometry.h"
#include "engines/boggle/classic/projection.h"
#include "engines/boggle/classic/round.h"
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
private:
    struct Corner {std::array<int,3> position{};int u=0,v=0;Color light;};
    struct Triangle {std::array<Corner,3> corners;bool letter=false;};
    std::vector<Triangle> triangles_;
    std::array<int,3> topCenter_{};
};
} // namespace reluva::boggle
