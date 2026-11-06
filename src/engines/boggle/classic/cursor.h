// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/projection.h"
namespace reluva::boggle {
struct ComputerCursorFrame {
    bool active=false;
    size_t cubes=0;
    ClassicPoint point;
};
// Read-only presentation: the last second before a scheduled word, with the
// final quarter moving to the word list. Does not choose words or change clocks.
ComputerCursorFrame computerCursorFrame(const AcceptedWord& word,const std::vector<ClassicPoint>& centers,
                                        ClassicPoint origin,uint64_t elapsed,uint64_t due,ClassicPoint submitPoint);
void drawComputerCursor(Surface& screen,ClassicPoint point);
} // namespace reluva::boggle
