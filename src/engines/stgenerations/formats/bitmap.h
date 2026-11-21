// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/palette.h"
#include "common/stream.h"
#include "engines/stgenerations/canvas.h"

#include <cstdint>

namespace reluva::stgenerations {

/**
 * One of the game's BMPs, as the game draws it: an 8-bit one's palette
 * indices and palette, or a true colour one as 0xAARRGGBB (image/bmp.h
 * reads them).
 */
class BitmapDecoder {
public:
    /** Reads a .bmp file; false (with a warning) if it isn't one. */
    bool loadStream(SeekableReadStream &stream);
    /** Reads a bitmap without its file header, from its BITMAPINFOHEADER on. */
    bool loadDIB(SeekableReadStream &stream);

    /** The picture, and an 8-bit one's palette. */
    ///@{
    const Canvas &getSurface() const { return _surface; }
    const Palette &getPalette() const { return _palette; }
    ///@}
    /** Whether it's an 8-bit picture, with a palette. */
    bool hasPalette() const { return !_surface.empty() && _surface.isClut8(); }
    /** How many colours the file's palette holds (0 for true colour). */
    uint16_t getPaletteColorCount() const { return _paletteColors; }

private:
    bool load(SeekableReadStream &stream, bool file);

    Canvas _surface;
    Palette _palette;
    uint16_t _paletteColors = 0;
};

} // namespace reluva::stgenerations
