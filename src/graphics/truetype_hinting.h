// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace reluva::ttf {

/**
 * A glyph's outline fitted to the pixel grid by the font's own hinting
 * program, as Windows fits it: in 26.6 fixed point (64ths of a pixel), y
 * up, the origin at the glyph's left side bearing point.
 */
struct HintedGlyph {
    std::vector<int32_t> x;
    std::vector<int32_t> y;
    std::vector<bool> onCurve;
    std::vector<int> contourEnds; // each contour's last point
    int advance = 0;              // whole pixels
    bool hinted = false;          // false: the outline as scaled (no program for it, or it failed)
};

/**
 * The TrueType instruction interpreter: runs a font's font program
 * (fpgm), its control value program (prep) at each size, and each
 * glyph's instructions, written from the TrueType specification (Apple's
 * TrueType Reference Manual, Microsoft's OpenType "glyf" and instruction
 * pages). See docs/common/truetype.md.
 */
class Hinter {
public:
    Hinter();
    ~Hinter();
    Hinter(const Hinter &) = delete;
    Hinter &operator=(const Hinter &) = delete;

    /** Reads the font's tables; false if it has no TrueType outlines. `font` must outlive the hinter. */
    bool load(const std::vector<uint8_t> &font);
    /** Whether the font has a program to run (fpgm, prep or glyphs' instructions). */
    bool hasPrograms() const;
    /** The font's design units to the em (its 'head' table's); 0 before load(). */
    int unitsPerEm() const { return _unitsPerEm; }
    /**
     * Glyph `index` at `ppem` pixels an em, hinted. False if it can't be
     * read; a program that fails leaves the outline as scaled.
     */
    bool glyph(int index, int ppem, HintedGlyph &out);

    struct Tables;
    struct Size;

private:
    std::unique_ptr<Tables> _tables;
    std::map<int, std::unique_ptr<Size>> _sizes; // by ppem
    int _unitsPerEm = 0;

    Size &size(int ppem);
};

} // namespace reluva::ttf
