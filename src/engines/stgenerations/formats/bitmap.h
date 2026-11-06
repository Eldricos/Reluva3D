// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/palette.h"
#include "common/stream.h"
#include "engines/stgenerations/canvas.h"

#include <cstdint>

namespace reluva::stgenerations {

// One of the game's BMPs, as the game draws it: an 8-bit one's palette
// indices and palette, or a true colour one as 0xAARRGGBB (common/bmp.h
// reads them).
class BitmapDecoder {
public:
    // A .bmp file; false (with a warning) if it isn't one.
    bool loadStream(SeekableReadStream& stream);
    // From its BITMAPINFOHEADER on.
    bool loadDIB(SeekableReadStream& stream);

    const Canvas& getSurface() const { return surface_; }
    const Palette& getPalette() const { return palette_; }
    bool hasPalette() const { return !surface_.empty() && surface_.isClut8(); }
    uint16_t getPaletteColorCount() const { return paletteColors_; }

private:
    bool load(SeekableReadStream& stream, bool file);

    Canvas surface_;
    Palette palette_;
    uint16_t paletteColors_ = 0;
};

} // namespace reluva::stgenerations
