// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/options.h"
#include "engines/boggle/classic/cube_renderer.h"
#include "engines/boggle/classic/projection.h"
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/results.h"
#include "engines/boggle/resources.h"
#include <optional>
#include <string_view>
namespace reluva::boggle {
std::array<std::string,3> classicArtworkPaths(ClassicVariant variant);
// Source-video coordinates; right and bottom are exclusive.
struct ClassicLidView {
    CameraParameters camera;
    std::array<int,4> clip{};
    bool visible=false;
};
ClassicLidView classicLidView(bool master,size_t frame);
class ClassicArtwork {
public:
    Error open(const Resources& resources,const std::filesystem::path& root,const ClassicOptions& options,
               const video::AviVideo1& prepared);
    bool active() const { return active_; }
    bool usesCubeMesh() const {return cubes_.active();}
    void draw(Surface& screen,const ClassicRound& round,uint64_t now,uint64_t duration,
              const image::Image& letters,const image::Image& qu,const std::vector<size_t>& selected,size_t scroll,uint32_t boardSeed=0x1997,std::string_view player="PLAYER 1",const RoundResults* review=nullptr,bool coverBoard=false) const;
    void drawTransition(Surface& screen,const Board& board,size_t frame,const image::Image& letters,
                        const image::Image& qu,uint32_t boardSeed) const;
    std::optional<size_t> cellAt(int x,int y,size_t side) const;
    ClassicPoint cellCenter(size_t cell,size_t side) const;
    ClassicPoint cellCenter(size_t cell,const Board& board,uint32_t seed) const;
    std::optional<size_t> cellAt(int x,int y,const Board& board,uint32_t seed) const;
    static size_t timerFrame(uint64_t remaining,uint64_t duration);
    static constexpr size_t visibleWords=23;
private:
    void drawCubes(Surface& screen,const Board& board,const CameraProjection& camera,
                   const image::Image& letters,const image::Image& qu,const std::vector<size_t>& selected,
                   uint32_t boardSeed,bool results) const;
    CubeRenderer cubes_;
    bool active_=false,master_=false,autoOrient_=true;
    image::Image open_,closed_,words_,names_;
    video::AviVideo1 timer_;
    font::TrueTypeAscii handwriting_;
};
} // namespace reluva::boggle
