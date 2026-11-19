// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "engines/stgenerations/canvas.h"
#include "common/stream.h"

namespace reluva::stgenerations {

// A first-person overlay animation (.OVR), e.g. Kirk's fist in
// KIRKHIT.OVR or the hand holding a plant sample in NUPLANT.OVR.
//
// Layout, worked out from NUPLANT.OVR and KIRKHIT.OVR:
//
//   char[8]  "OVERLAY\0"
//   uint32   width
//   uint32   total rows (all frames stacked vertically)
//   uint32   offset of each row, relative to the end of this table
//   rows, each a sequence of int16 values until the row is `width` wide:
//     negative n: skip -n transparent pixels
//     positive n: n palette indices follow, padded to an even length
//
// Every row of both samples decodes to exactly the width. The file does
// not say how tall a frame is: whoever loads it says (0x4029A5), and
// without that it is one frame of every row. The held items cut theirs
// into 88-row frames, the punches (SET_PUNCH) into frames as tall as the
// 3D view, kViewHeight; the view overlays (SHOW_VIEW_OVERLAY) are one
// frame.
class Overlay {
public:
    // The original's 3D view height (view word +0x7A), a punch's frame height.
    static constexpr int kViewHeight = 252;

    // Decode the rows; pixels of palette index key (if not -1) are
    // see-through too, as 255 is in the held weapons (NUPHASER.OVR,
    // NUKLIDIS.OVR), where the level palette may make it magenta.
    bool load(SeekableReadStream& stream, int key = -1);

    int getWidth() const { return _surface.w; }
    int getRowCount() const { return _surface.h; }

    // All rows decoded; transparent pixels hold 0 and are flagged in the mask.
    const Canvas& getSurface() const { return _surface; }
    bool isOpaque(int x, int y) const { return _mask[y * _surface.w + x] != 0; }

    // Copy one frame of the given height into dst at (x, y), leaving
    // transparent pixels untouched. Frames are stacked top down, but each
    // one's rows are stored bottom up: the plant sampler's nozzle lands on
    // the hotspots sgens.exe gives its poses only that way up.
    void drawFrame(Canvas& dst, int frame, int frameHeight, int x, int y) const;

private:
    Canvas _surface;
    std::vector<uint8_t> _mask;
};

} // namespace reluva::stgenerations

