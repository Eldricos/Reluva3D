// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace reluva::boggle {
// Independent presentation-only mixing; never consumes the board generator.
// Replaying a board keeps its orientations. A new board uses its new seed.
inline unsigned cubeQuarterTurns(uint32_t boardSeed,size_t cell,bool autoOrient) {
    if(autoOrient)return 0;
    uint32_t n=boardSeed ^ (uint32_t(cell)*0x9e3779b9u);
    n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;n^=n>>16;
    return n>>30;
}
// Inverse sample mapping for clockwise display rotation of a square. Both
// atlas renderers use this before scaling into the (possibly nonsquare) cell.
inline std::array<int,2> cubeTexturePoint(int x,int y,int maximum,unsigned turns) {
    switch(turns&3) {
    case 1:return {y,maximum-x};
    case 2:return {maximum-x,maximum-y};
    case 3:return {maximum-y,x};
    default:return {x,y};
    }
}
} // namespace reluva::boggle
