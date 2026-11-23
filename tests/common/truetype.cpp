// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The TrueType hinting interpreter (graphics/truetype_hinting.h), on a font
// made here: at 1024 units to the em and 16 pixels to the em a font unit is
// a 64th of a pixel, the interpreter's own unit, so what's expected can be
// read straight off the outline.
//
//   reluva-tests common.unit.truetype

#include "graphics/truetype_hinting.h"
#include "tests/support/test.h"
#include "tests/support/ttf_writer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using reluva::testing::TestGlyph;
using reluva::testing::testFont;

namespace {

void testHinting() {
    // Glyph 1: a stem 70 units (1.09 pixels) wide at x 40, 500 high. Its
    // left edge to the grid (MDAP[r]), its right a control value away
    // (MIRP[r]: 70, rounded to a pixel), the rest of it moved with them
    // (IUP), then its top to the grid in y and the rest with it.
    TestGlyph stem;
    stem.points = {{40, 0}, {40, 500}, {110, 500}, {110, 0}};
    stem.contourEnds = {3};
    stem.instructions = {0x01,       // SVTCA[x]
                         0xb0, 0,    // PUSHB 0
                         0x2f,       // MDAP[r]
                         0xb1, 3, 0, // PUSHB 3 0
                         0xe4,       // MIRP[r]: point 3 from rp0, cvt 0
                         0x31,       // IUP[x]
                         0x00,       // SVTCA[y]
                         0xb0, 1,    // PUSHB 1
                         0x2f,       // MDAP[r]
                         0x30};      // IUP[y]
    // Glyph 2: four points put at control values 1 to 4 (MIAP), which
    // the control value program changed through a function: +1 pixel to
    // 1, and to 2 at 16 pixels to the em but to 3 at any other size; +3
    // pixels to 4 by a LOOPCALL.
    TestGlyph probe;
    probe.points = {{0, 0}, {0, 100}, {0, 200}, {0, 300}};
    probe.contourEnds = {0, 1, 2, 3};
    probe.instructions = {0x01, 0xb7, 0, 1, 1, 2, 2, 3, 3, 4, 0x3e, 0x3e, 0x3e, 0x3e};
    // Glyph 3: a delta moving point 0 a pixel right at 16 pixels only.
    TestGlyph delta;
    delta.points = {{100, 0}, {100, 100}};
    delta.contourEnds = {1};
    delta.instructions = {0x01,                // SVTCA[x]
                          0xb0, 0,    0x5f,    // SDS 0: whole steps
                          0xb2, 0x78, 0,    1, // arg (16 - 9 = 7, step +1), point 0, n 1
                          0x5d};               // DELTAP1
    // Glyph 4: the vectors set from (3, 4) by SPVFS, the freedom vector to
    // it, then point 0 shifted a pixel along it (SHPIX): (0.6, 0.8) in 2.14
    // is 9830 and 13107 of 16384, so 64 moves it 38 across and 51 up.
    TestGlyph diagonal;
    diagonal.points = {{100, 0}, {100, 100}};
    diagonal.contourEnds = {1};
    diagonal.instructions = {0xb1, 3, 4,  0x0a,  // SPVFS (3, 4)
                             0x0e,               // SFVTPV
                             0xb1, 0, 64, 0x38}; // SHPIX point 0 by 64
    // fpgm: function 0 adds a pixel to the control value whose index is
    // on the stack.
    const std::vector<uint8_t> fpgm = {0xb0, 0, 0x2c, 0x20, 0x45, 0xb0, 64, 0x60, 0x44, 0x2d};
    const std::vector<uint8_t> prep = {0xb1, 1,    0,  0x2b,              // CALL 0 on cvt 1
                                       0x4b, 0xb0, 16, 0x54,              // MPPEM 16 EQ
                                       0x58,                              // IF
                                       0xb1, 2,    0,  0x2b,              //   CALL 0 on cvt 2
                                       0x1b,                              // ELSE
                                       0xb1, 3,    0,  0x2b,              //   CALL 0 on cvt 3
                                       0x59,                              // EIF
                                       0xb4, 4,    4,  4,    3, 0, 0x2a}; // LOOPCALL 0, 3 times, on cvt 4
    const std::vector<uint8_t> font = testFont({stem, probe, delta, diagonal}, {70, 100, 200, 300, 400}, fpgm, prep);

    ttf::Hinter h;
    CHECK(h.load(font) && h.hasPrograms() && h.unitsPerEm() == 1024);
    ttf::HintedGlyph g;
    CHECK(h.glyph(1, 16, g) && g.hinted && g.x.size() == 4);
    if (g.x.size() == 4) {
        // x: 40 to 64, 110 to 64 + 64; y: 500 to 512, all shifted with it.
        CHECK(g.x[0] == 64 && g.x[1] == 64 && g.x[2] == 128 && g.x[3] == 128);
        CHECK(g.y[1] == 512 && g.y[2] == 512 && g.y[0] == 12 && g.y[3] == 12);
        CHECK(g.advance == 3); // 200 units: 3.125 pixels
    }
    CHECK(h.glyph(2, 16, g) && g.x.size() == 4);
    if (g.x.size() == 4)
        CHECK(g.x[0] == 164 && g.x[1] == 264 && g.x[2] == 300 && g.x[3] == 592);
    // At 12 pixels a unit is 0.75 of a 64th: the ELSE's control value instead.
    CHECK(h.glyph(2, 12, g) && g.x.size() == 4);
    if (g.x.size() == 4)
        CHECK(g.x[0] == 139 && g.x[1] == 150 && g.x[2] == 289 && g.x[3] == 492);
    CHECK(h.glyph(3, 16, g) && g.x.size() == 2 && g.x[0] == 164 && g.x[1] == 100);
    CHECK(h.glyph(3, 12, g) && g.x.size() == 2 && g.x[0] == 75);
    CHECK(h.glyph(4, 16, g) && g.x.size() == 2 && g.x[0] == 138 && g.y[0] == 51 && g.x[1] == 100);
    // An empty glyph: no points, its advance.
    CHECK(h.glyph(0, 16, g) && g.x.empty() && g.advance == 3);
    // Not a font.
    ttf::Hinter none;
    CHECK(!none.load(std::vector<uint8_t>(64, 0)));
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.truetype",
                                        {
                                            {"hinting", [](const std::filesystem::path &) { testHinting(); }},
                                        });
