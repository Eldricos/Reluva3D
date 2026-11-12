// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "engines/boggle/classic/round.h"
#include "engines/boggle/classic/projection.h"
#include "engines/boggle/classic/ai_motion.h"
#include "engines/boggle/formats/cursor_image.h"
namespace reluva::boggle {
struct ComputerCursorFrame {
    bool active=false,pressed=false;
    size_t cubes=0;
    ClassicPoint point;
};
// Read-only presentation of the plan's press/release/movement timeline.
// A null timeline retains the legacy preview used by standalone callers.
ComputerCursorFrame computerCursorFrame(const AcceptedWord& word,const std::vector<ClassicPoint>& centers,
                                        ClassicPoint origin,uint64_t elapsed,uint64_t due,ClassicPoint submitPoint,const ComputerMotion* motion=nullptr);
class ClassicCursorArtwork {
public:
    Error read(const std::vector<PeResource>& resources);
    bool active() const {return up_.depth!=0;}
    void draw(Surface& screen,ClassicPoint point,bool pressed) const;
private:
    CursorImage up_,down_;
};
void drawComputerCursor(Surface& screen,ClassicPoint point);
} // namespace reluva::boggle
