// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <array>
#include <cstddef>
namespace reluva::boggle {
using DieAxis=std::array<int,3>;
// Observed input-column axes, not embedded mesh data: -Y,+X,+Z,-Z,-X,+Y.
inline constexpr std::array<DieAxis,6> dieFaceAxes={{{0,-1,0},{1,0,0},{0,0,1},{0,0,-1},{-1,0,0},{0,1,0}}};
struct DiePose {
    std::array<DieAxis,3> rows{};
    template<class T> std::array<T,3> source(const std::array<T,3>& point) const {
        std::array<T,3> result{};
        for(size_t i=0;i<3;++i)for(size_t j=0;j<3;++j)result[i]+=T(rows[i][j])*point[j];
        return result;
    }
    // Renderer templates use the earlier canonical X quarter-turn.
    template<class T> std::array<T,3> canonical(const std::array<T,3>& point) const {
        return source(std::array<T,3>{point[0],-point[2],point[1]});
    }
};
inline DiePose spinDie(DiePose pose,unsigned clockwise) {
    for(unsigned n=0;n<(clockwise&3);++n) {
        const auto right=pose.rows[0];pose.rows[0]=pose.rows[1];
        for(size_t i=0;i<3;++i)pose.rows[1][i]=-right[i];
    }
    return pose;
}
inline DiePose canonicalDiePose(unsigned clockwise=0) {
    DiePose pose;pose.rows={{{1,0,0},{0,0,1},{0,-1,0}}};return spinDie(pose,clockwise);
}
inline DiePose settledDiePose(size_t upward,unsigned clockwise,bool rounded) {
    if(upward>=6)return canonicalDiePose(clockwise);
    // Measured upright back directions. Construct the rigid frame with a
    // cross product rather than replaying the original Euler instructions.
    DieAxis back=(upward==0 || upward==5)?DieAxis{0,0,-1}:DieAxis{0,1,0};
    if(!rounded && (upward==2 || upward==4))back[1]=-1;
    DieAxis depth;for(size_t i=0;i<3;++i)depth[i]=-dieFaceAxes[upward][i];
    DieAxis right;
    for(size_t i=0;i<3;++i)right[i]=back[(i+1)%3]*depth[(i+2)%3]-back[(i+2)%3]*depth[(i+1)%3];
    DiePose pose;pose.rows={right,back,depth};return spinDie(pose,clockwise);
}
} // namespace reluva::boggle
