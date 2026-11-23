// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Writes TrueType fonts for the tests, glyph by glyph, so they need no
// font file: outlines of straight lines, with hinting programs, a
// character map and the font's ascent and descent as asked.

namespace reluva::testing {

inline void put16(std::vector<uint8_t> &v, int x) {
    v.push_back(uint8_t((x >> 8) & 255));
    v.push_back(uint8_t(x & 255));
}
inline void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, int(x >> 16));
    put16(v, int(x & 0xffff));
}

/** A glyph: its points (all on the curve), contours, instructions and advance; its character, if it has one. */
struct TestGlyph {
    std::vector<std::pair<int, int>> points;
    std::vector<int> contourEnds;
    std::vector<uint8_t> instructions;
    int advance = 200;
    uint32_t codepoint = 0;
};

/**
 * A TrueType font of these glyphs (glyph 0 empty), 1,024 units to the em:
 * its control values, font program and control value program, its ascent
 * and descent (hhea's), and a character map (format 12) of the glyphs
 * that have a character.
 */
inline std::vector<uint8_t> testFont(const std::vector<TestGlyph> &glyphs, const std::vector<int> &cvt,
                                     const std::vector<uint8_t> &fpgm, const std::vector<uint8_t> &prep, int ascent = 0,
                                     int descent = 0) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> tables;
    std::vector<uint8_t> head(54, 0);
    head[18] = 1024 >> 8; // unitsPerEm
    head[19] = 1024 & 255;
    tables.push_back({"head", head});
    std::vector<uint8_t> hhea(36, 0);
    hhea[4] = uint8_t((ascent >> 8) & 255);
    hhea[5] = uint8_t(ascent & 255);
    hhea[6] = uint8_t((descent >> 8) & 255);
    hhea[7] = uint8_t(descent & 255);
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
        int yMin = 1 << 30;
        int xMax = -(1 << 30);
        int yMax = -(1 << 30);
        for (const auto &p : g.points) {
            xMin = std::min(xMin, p.first);
            yMin = std::min(yMin, p.second);
            xMax = std::max(xMax, p.first);
            yMax = std::max(yMax, p.second);
        }
        put16(hmtx, g.advance);
        put16(hmtx, xMin); // its left side bearing: the origin at x 0
        put16(glyf, int(g.contourEnds.size()));
        put16(glyf, xMin); // its bounding box
        put16(glyf, yMin);
        put16(glyf, xMax);
        put16(glyf, yMax);
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
    std::vector<std::pair<uint32_t, int>> mapped; // character, glyph
    for (size_t i = 0; i < glyphs.size(); ++i)
        if (glyphs[i].codepoint)
            mapped.push_back({glyphs[i].codepoint, int(i + 1)});
    if (!mapped.empty()) {
        std::sort(mapped.begin(), mapped.end());
        std::vector<uint8_t> cmap;
        put16(cmap, 0); // version
        put16(cmap, 1); // one subtable: Windows (3), Unicode's full range (10)
        put16(cmap, 3);
        put16(cmap, 10);
        put32(cmap, 12);
        put16(cmap, 12); // format 12: a group a character
        put16(cmap, 0);
        put32(cmap, uint32_t(16 + 12 * mapped.size()));
        put32(cmap, 0);
        put32(cmap, uint32_t(mapped.size()));
        for (const auto &[character, glyph] : mapped) {
            put32(cmap, character);
            put32(cmap, character);
            put32(cmap, uint32_t(glyph));
        }
        tables.push_back({"cmap", cmap});
    }

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

} // namespace reluva::testing
