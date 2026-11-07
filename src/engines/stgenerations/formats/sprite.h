// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "engines/stgenerations/canvas.h"
#include "common/stream.h"

namespace reluva::stgenerations {

// A set of billboard sprites for the 3D away missions (.GS2), e.g. a
// crew member walking (verid1/001.gs2) or a flame (008.gs2).
//
// Layout, worked out from verid1/001.gs2 and 008.gs2:
//
//   uint32   2 (version; .GSF files have 1 and a different layout)
//   uint32   frame count
//   uint32   0
//   uint32   sequence count (16 in both samples)
//   uint32   0
//   uint16   ?, uint16 ?  (64, 128 / 128, 256 ... a size hint)
//   sequences: uint8 n, then n frame indices
//   frames:
//     uint32 half width at scale 1.0, 16.16 (0.609 for a 78 column frame)
//     uint32 column count
//     uint32 column height
//     uint8  pixels[columns][height]   column-major, bottom row first,
//                                      0xFF is transparent
//     uint8  tables[3][columns]        per-column hints: first and last
//                                      solid rows (scaled), then a class,
//                                      0 for an empty column; empty
//                                      columns aren't drawn whatever their
//                                      pixels hold
//
// Both samples parse to exactly their end with the advertised frame count.
class SpriteSet {
public:
    static const uint8_t kTransparent = 0xFF;

    struct Frame {
        // Upright image; transparent pixels hold kTransparent.
        Canvas surface;
        // Half the width in world units for a sprite at scale 1.0, from the
        // header's 16.16 value. Drawn actors scale it (0x4C3C10).
        double halfWidth = 0;
    };

    bool load(SeekableReadStream& stream);

    int getFrameCount() const { return (int)_frames.size(); }
    const Frame& getFrame(int index) const { return _frames[index]; }
    const std::vector<std::vector<uint8_t>>& getSequences() const { return _sequences; }

    // Largest frame size, for laying frames out on screen.
    int getMaxWidth() const;
    int getMaxHeight() const;

private:
    std::vector<Frame> _frames;
    std::vector<std::vector<uint8_t>> _sequences;
};

} // namespace reluva::stgenerations

