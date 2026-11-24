// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The TrueType hinting interpreter (graphics/truetype_hinting.h), on a font
// made here: at 1024 units to the em and 16 pixels to the em a font unit is
// a 64th of a pixel, the interpreter's own unit, so what's expected can be
// read straight off the outline.
//
//   reluva-tests common.unit.truetype

#include "graphics/truetype.h"
#include "graphics/truetype_hinting.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

void put16(std::vector<uint8_t> &v, int x) {
    v.push_back(uint8_t((x >> 8) & 255));
    v.push_back(uint8_t(x & 255));
}
void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, int(x >> 16));
    put16(v, int(x & 0xffff));
}

struct TestGlyph {
    std::vector<std::pair<int, int>> points; // all on the curve
    std::vector<int> contourEnds;
    std::vector<uint8_t> instructions;
    int advance = 200;
};

// A TrueType font of these glyphs (glyph 0 empty), its control values,
// font program and control value program.
std::vector<uint8_t> testFont(const std::vector<TestGlyph> &glyphs, const std::vector<int> &cvt,
                              const std::vector<uint8_t> &fpgm, const std::vector<uint8_t> &prep,
                              std::vector<std::pair<std::string, std::vector<uint8_t>>> tables = {}) {
    std::vector<uint8_t> head(54, 0);
    head[18] = 1024 >> 8; // unitsPerEm
    head[19] = 1024 & 255;
    tables.push_back({"head", head});
    std::vector<uint8_t> hhea(36, 0);
    hhea[4] = 1024 >> 8; // ascent: an em, with no descent
    hhea[34] = uint8_t((glyphs.size() + 1) >> 8);
    hhea[35] = uint8_t((glyphs.size() + 1) & 255);
    tables.push_back({"hhea", hhea});
    std::vector<uint8_t> maxp;
    put32(maxp, 0x00010000);
    put16(maxp, int(glyphs.size() + 1));
    for (int i = 0; i < 13; i++)
        put16(maxp, i == 5 ? 4 : i == 6 ? 16 : i == 7 ? 4 : 0); // twilight 4, storage 16, functions 4
    tables.push_back({"maxp", maxp});
    std::vector<uint8_t> hmtx;
    std::vector<uint8_t> glyf;
    std::vector<uint8_t> loca;
    put16(hmtx, 200);
    put16(hmtx, 0);
    put16(loca, 0);
    for (const TestGlyph &g : glyphs) {
        int xMin = 1 << 30;
        for (const auto &p : g.points)
            xMin = std::min(xMin, p.first);
        put16(hmtx, g.advance);
        put16(hmtx, xMin); // its left side bearing: the origin at x 0
        put16(glyf, int(g.contourEnds.size()));
        put16(glyf, xMin);
        put16(glyf, 0);
        put16(glyf, 0);
        put16(glyf, 0);
        for (int e : g.contourEnds)
            put16(glyf, e);
        put16(glyf, int(g.instructions.size()));
        glyf.insert(glyf.end(), g.instructions.begin(), g.instructions.end());
        for (size_t k = 0; k < g.points.size(); k++)
            glyf.push_back(1); // on the curve, coordinates as 16-bit deltas
        int last = 0;
        for (const auto &p : g.points)
            put16(glyf, p.first - last), last = p.first;
        last = 0;
        for (const auto &p : g.points)
            put16(glyf, p.second - last), last = p.second;
        if (glyf.size() % 2)
            glyf.push_back(0);
        put16(loca, int(glyf.size() / 2));
    }
    // An empty glyph 0 first: its offsets all 0 until the first glyph's.
    std::vector<uint8_t> loca0;
    put16(loca0, 0);
    loca0.insert(loca0.end(), loca.begin(), loca.end());
    tables.push_back({"hmtx", hmtx});
    tables.push_back({"loca", loca0});
    tables.push_back({"glyf", glyf});
    std::vector<uint8_t> cvtTable;
    for (int c : cvt)
        put16(cvtTable, c);
    tables.push_back({"cvt ", cvtTable});
    tables.push_back({"fpgm", fpgm});
    tables.push_back({"prep", prep});

    // The directory's tables in their tags' order, as fonts have them.
    std::sort(tables.begin(), tables.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<uint8_t> font;
    put32(font, 0x00010000);
    put16(font, int(tables.size()));
    put16(font, 0);
    put16(font, 0);
    put16(font, 0);
    size_t offset = 12 + 16 * tables.size();
    std::vector<uint8_t> body;
    for (const auto &[tag, data] : tables) {
        font.insert(font.end(), tag.begin(), tag.end());
        put32(font, 0);
        put32(font, uint32_t(offset + body.size()));
        put32(font, uint32_t(data.size()));
        body.insert(body.end(), data.begin(), data.end());
        while (body.size() % 4)
            body.push_back(0);
    }
    font.insert(font.end(), body.begin(), body.end());
    return font;
}

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

// A font's name, as Windows finds it, and its letters' average width.
void testNames() {
    std::vector<uint8_t> cmap; // a Windows Unicode map, of nothing
    put16(cmap, 0);
    put16(cmap, 1);
    put16(cmap, 3);
    put16(cmap, 1);
    put32(cmap, 12);
    for (int v : {4, 24, 0, 2, 2, 0, 0, 0xffff, 0, 0xffff, 1, 0})
        put16(cmap, v);
    std::vector<uint8_t> name;
    const std::string family = "Plain Test";
    put16(name, 0);
    put16(name, 1);
    put16(name, 6 + 12);
    for (int v : {3, 1, 0x409, 1, int(family.size() * 2), 0})
        put16(name, v);
    for (char c : family)
        put16(name, c);
    std::vector<uint8_t> os2(78, 0);
    os2[2] = 512 >> 8; // xAvgCharWidth: half an em
    os2[3] = 512 & 255;
    std::vector<uint8_t> data = testFont({}, {}, {}, {}, {{"cmap", cmap}, {"name", name}, {"OS/2", os2}});
    ttf::Font font;
    CHECK(!font.load(data));
    CHECK(font.familyName() == family);
    CHECK(std::fabs(font.averageWidth(20) - 10) < 0.01f);
    ttf::Font none;
    CHECK(none.familyName().empty() && none.averageWidth(10) == 0);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.truetype",
                                        {
                                            {"hinting", [](const std::filesystem::path &) { testHinting(); }},
                                            {"names", [](const std::filesystem::path &) { testNames(); }},
                                        });
