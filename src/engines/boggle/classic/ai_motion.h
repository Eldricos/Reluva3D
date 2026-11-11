// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/projection.h"
#include "engines/boggle/classic/round.h"
namespace reluva::boggle {
struct ComputerGeometry {
    std::vector<ClassicPoint> centers;
    ClassicPoint origin,submit;
    unsigned scale=1000;
};
struct ComputerMotion {
    std::vector<uint64_t> presses,releases;
    uint64_t duration=0;
};
ComputerGeometry defaultComputerGeometry(size_t side);
Error makeComputerMotion(const AcceptedWord& word,const ComputerGeometry& geometry,ClassicPoint origin,
                         unsigned cubeSpeed,unsigned submitSpeed,const std::vector<uint64_t>& holds,ComputerMotion& out);
} // namespace reluva::boggle
