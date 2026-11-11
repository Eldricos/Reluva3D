// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/computer.h"
#include <functional>
namespace reluva::boggle {
// Dictionary/path candidates in board discovery order. The injected random
// source permits behavioral comparisons without embedding original code/data.
struct AiChoice {size_t word=0;unsigned credit=0;bool followup=false;size_t remaining=0;};
struct AiDifficulty {
    int lengths[11];
    unsigned plural,past,ending,prefix;
    uint64_t minimum,span,minimumStep,spanStep,rebate,scarcity,penalty;
    unsigned cubeSpeed,submitSpeed,hold,gap; // speeds in thousandths of a pixel/ms
};
const AiDifficulty& aiDifficulty(ComputerLevel level);
Error chooseComputerWords(const std::vector<AcceptedWord>& pool,ComputerLevel level,
                          const std::function<uint32_t()>& draw,std::vector<AiChoice>& out);
} // namespace reluva::boggle
