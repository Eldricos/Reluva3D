// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations' unit tests: its fonts, subtitles, overlays, sprite
// sets and script files, built in code to the layouts seen in the game.

#include "tests/engines/stgenerations/support.h"

#include "common/bytes.h"

#include "engines/stgenerations/formats/font.h"
#include "engines/stgenerations/formats/overlay.h"
#include "engines/stgenerations/formats/resources.h"
#include "engines/stgenerations/formats/script.h"
#include "engines/stgenerations/formats/sprite.h"

using namespace reluva;
using namespace reluva::stgenerations;

static void stgFontFormat() {
    // Built to the layout seen in COMMON.DAT:FONTS/ACTION.FNT: header, then
    // glyphs of code, size (w*h+2), width, height and pixels.
    MemoryWriteStream glyphs;
    auto addGlyph = [&](uint16_t code, uint8_t w, uint8_t h, std::vector<uint8_t> pixels) {
        glyphs.writeUint16LE(code);
        glyphs.writeUint16LE((uint16_t)(w * h + 2));
        glyphs.writeByte(w);
        glyphs.writeByte(h);
        glyphs.write(pixels.data(), (uint32_t)pixels.size());
    };
    addGlyph(' ', 2, 3, {0, 0, 0, 0, 0, 0});
    addGlyph('A', 2, 2, {5, 0, 0, 6});

    MemoryWriteStream file;
    file.writeUint32BE(tagBE('F', 'O', 'N', 'T'));
    file.writeUint32LE(8 + glyphs.size());
    file.writeUint16LE(2);
    file.writeByte(0); // transparent
    file.writeByte(3); // height
    file.writeUint32LE(0);
    file.write(glyphs.data().data(), glyphs.size());

    MemoryReadStream stream(file.data());
    Font font;
    CHECK(font.load(stream));
    CHECK_EQUAL(font.getGlyphCount(), 2);
    CHECK_EQUAL(font.getHeight(), 3);
    CHECK_EQUAL(font.getStringWidth("A A"), 6);
    CHECK_EQUAL(font.getCharWidth('a'), 2); // falls back to upper case

    Canvas s;
    s.create(8, 4, Canvas::kClut8);
    s.fillRect(Rect(0, 0, 8, 4), 9);
    font.drawString(s, " A", 0, 0);
    CHECK_EQUAL(s.getPixel(0, 0), 9u); // space is transparent
    CHECK_EQUAL(s.getPixel(2, 0), 5u);
    CHECK_EQUAL(s.getPixel(3, 0), 9u);
    CHECK_EQUAL(s.getPixel(3, 1), 6u);
    uint8_t remap[256];
    for (int i = 0; i < 256; i++)
        remap[i] = (uint8_t)(i + 100);
    font.drawString(s, "A", 4, 0, remap);
    CHECK_EQUAL(s.getPixel(4, 0), 105u); // glyph colour 5 remapped
    CHECK_EQUAL(s.getPixel(5, 1), 106u);
    CHECK_EQUAL(s.getPixel(5, 0), 9u); // transparent stays untouched

    CHECK_EQUAL(font.wordWrap("A A A A", 7).size(), (size_t)2);

    // A glyph whose size field disagrees with its dimensions is rejected.
    std::vector<uint8_t> bad = file.data();
    bad[20] = 99;
    MemoryReadStream badStream(bad);
    CHECK(!font.load(badStream));
}

static void stgSubtitles() {
    // Loose files on a CD, beside the installed game.
    stgtest::TempDir dir;
    dir.writeFile("disc1/GENS/SOUNDS/RIKE0107.WAV", "RIFF");
    dir.writeFile("disc1/GENS/SOUNDS/rike0107.txt", "RIKER: \"CAPTAIN, WE'VE GOT\r\nA SERIOUS SITUATION.\"\r\n");
    ResourceManager resources;
    resources.addSearchPaths(dir.path(), {}, {});
    CHECK_EQUAL(resources.loadSubtitle({"", "SOUNDS/RIKE0107.WAV"}),
                std::string("RIKER: \"CAPTAIN, WE'VE GOT A SERIOUS SITUATION.\""));
    CHECK(resources.loadSubtitle({"", "SOUNDS/MISC0001.WAV"}).empty());
}

static void stgOverlayFormat() {
    // Width 4, 4 rows = two frames of 2 rows. Rows are int16 runs: negative
    // skips transparent pixels, positive is followed by pixels padded to even.
    std::vector<std::vector<int>> rows = {
        {-4},              // all transparent
        {-1, 2, 7, 8, -1}, // .78.
        {3, 1, 2, 3, -1},  // 123. (odd run, padded)
        {4, 9, 9, 9, 9},   // 9999
    };
    MemoryWriteStream data;
    std::vector<uint32_t> offsets;
    for (const std::vector<int> &row : rows) {
        offsets.push_back(data.size());
        for (size_t i = 0; i < row.size();) {
            int n = row[i++];
            data.writeUint16LE((uint16_t)(int16_t)n);
            if (n > 0) {
                for (int k = 0; k < n; k++)
                    data.writeByte((uint8_t)row[i++]);
                if (n & 1)
                    data.writeByte(0);
            }
        }
    }

    MemoryWriteStream file;
    file.write("OVERLAY\0", 8);
    file.writeUint32LE(4);
    file.writeUint32LE((uint32_t)rows.size());
    for (uint32_t offset : offsets)
        file.writeUint32LE(offset);
    file.write(data.data().data(), data.size());

    MemoryReadStream stream(file.data());
    Overlay overlay;
    CHECK(overlay.load(stream));
    CHECK_EQUAL(overlay.getWidth(), 4);
    CHECK_EQUAL(overlay.getRowCount(), 4);
    CHECK(!overlay.isOpaque(0, 0));
    CHECK(!overlay.isOpaque(0, 1));
    CHECK(overlay.isOpaque(1, 1));
    CHECK_EQUAL(overlay.getSurface().getPixel(2, 1), 8u);
    CHECK_EQUAL(overlay.getSurface().getPixel(2, 2), 3u);
    CHECK(!overlay.isOpaque(3, 2));

    // Frame 1 (rows 2-3, stored bottom up) drawn over a filled surface keeps
    // the transparent pixel.
    Canvas dst;
    dst.create(4, 2, Canvas::kClut8);
    dst.fillRect(Rect(0, 0, 4, 2), 5);
    overlay.drawFrame(dst, 1, 2, 0, 0);
    CHECK_EQUAL(dst.getPixel(0, 1), 1u);
    CHECK_EQUAL(dst.getPixel(3, 1), 5u);
    CHECK_EQUAL(dst.getPixel(3, 0), 9u);

    // A row whose runs overflow the width is rejected.
    std::vector<uint8_t> bad = file.data();
    bad[16 + 4 * 4] = 0x7F; // first row: skip becomes a 127 pixel run
    MemoryReadStream badStream(bad);
    CHECK(!overlay.load(badStream));
}

static void stgSpriteFormat() {
    // Two frames: 2 columns x 3 rows and 1 column x 2 rows, stored
    // column-major, bottom row first, 0xFF transparent, then 3 bytes of
    // per-column tables per column.
    MemoryWriteStream file;
    file.writeUint32LE(2); // version
    file.writeUint32LE(2); // frames
    file.writeUint32LE(0);
    file.writeUint32LE(3); // sequences
    file.writeUint32LE(0);
    file.writeUint16LE(64);
    file.writeUint16LE(128);
    const uint8_t sequences[] = {2, 0, 1, 0, 1, 1}; // [0,1], [], [1]
    file.write(sequences, sizeof(sequences));

    file.writeUint32LE(4 << 8); // half width 1/64
    file.writeUint32LE(2);
    file.writeUint32LE(3);
    const uint8_t frame0[] = {1, 2, 0xFF, 4, 5, 6}; // column 0 bottom-up, column 1 bottom-up
    file.write(frame0, sizeof(frame0));
    // Column 0 is one run (class 2); column 1 is marked empty (class 0),
    // so its pixels aren't drawn.
    const uint8_t tables0[6] = {0, 0, 0, 0, 2, 0};
    file.write(tables0, sizeof(tables0));

    file.writeUint32LE(2 << 8);
    file.writeUint32LE(1);
    file.writeUint32LE(2);
    const uint8_t frame1[] = {7, 8, 0, 0, 3};
    file.write(frame1, sizeof(frame1));

    MemoryReadStream stream(file.data());
    SpriteSet sprites;
    CHECK(sprites.load(stream));
    CHECK_EQUAL(sprites.getFrameCount(), 2);
    CHECK_EQUAL(sprites.getSequences().size(), (size_t)3);
    CHECK(sprites.getSequences()[1].empty());
    CHECK_EQUAL(sprites.getMaxWidth(), 2);
    CHECK_EQUAL(sprites.getMaxHeight(), 3);

    const Canvas &s0 = sprites.getFrame(0).surface;
    CHECK_EQUAL(sprites.getFrame(0).halfWidth, 1024 / 65536.0);
    CHECK_EQUAL(s0.getPixel(0, 2), 1u); // bottom row first
    CHECK_EQUAL(s0.getPixel(0, 1), 2u);
    CHECK_EQUAL(s0.getPixel(0, 0), 0xFFu);
    CHECK_EQUAL(s0.getPixel(1, 0), 0xFFu);
    CHECK_EQUAL(sprites.getFrame(1).surface.getPixel(0, 0), 8u);

    // Truncated data is rejected.
    MemoryReadStream truncated(std::vector<uint8_t>(file.data().begin(), file.data().end() - 8));
    CHECK(!sprites.load(truncated));
}

static void stgScriptContainer() {
    // One object with one state whose enter block does "push 5, push 7, SET_GLOBAL".
    const uint16_t code[] = {1, 9, 0x8000, 2, 0xFFFF, 0x0002, 5, 0x0002, 7, 0x0019, 0, 0x3000, 0x2000, 0x1000};
    const uint8_t sections[6][2] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}, {9, 10}, {11, 12}};
    const char names[] = "PHASER.BMP\0KIRKHIT.OVR\0";

    MemoryWriteStream body;
    body.writeUint32LE(sizeof(code));
    for (int i = 0; i < 6; i++)
        body.writeUint32LE(2);
    for (uint16_t word : code)
        body.writeUint16LE(word);
    for (int i = 0; i < 6; i++)
        body.write(sections[i], 2);
    body.write(names, sizeof(names) - 1);

    MemoryWriteStream file;
    file.writeUint32BE(tagBE('S', 'G', 'E', 'N'));
    file.writeUint16LE(22);
    file.writeUint16LE(2);
    file.writeUint32LE(0); // file size - 12, patched below
    file.write(body.data().data(), body.size());
    std::vector<uint8_t> bytes = file.data();
    writeLE32(&bytes[8], (uint32_t)bytes.size() - 12);

    MemoryReadStream stream(bytes);
    ScriptFile script;
    CHECK(script.load(stream));
    CHECK_EQUAL(script.getVersion(), 22);
    CHECK_EQUAL(script.getCode().size(), (size_t)14);
    CHECK_EQUAL(script.getCode()[9], 0x19);
    CHECK_EQUAL(script.getObjects().size(), (size_t)1);
    CHECK_EQUAL(script.getSection(5)[1], 12);
    CHECK_EQUAL(script.getResourceNames().size(), (size_t)2);
    CHECK_EQUAL(script.getResourceNames()[1], std::string("KIRKHIT.OVR"));

    // Sizes that don't match the file are rejected.
    writeLE32(&bytes[8], 1234);
    MemoryReadStream bad(bytes);
    CHECK(!script.load(bad));
}

static const reluva::test::Suite
    kSuite("stgenerations.unit.formats",
           {
               {"stg font format", [](const std::filesystem::path &) { stgFontFormat(); }},
               {"stg subtitles", [](const std::filesystem::path &) { stgSubtitles(); }},
               {"stg overlay format", [](const std::filesystem::path &) { stgOverlayFormat(); }},
               {"stg sprite format", [](const std::filesystem::path &) { stgSpriteFormat(); }},
               {"stg script container", [](const std::filesystem::path &) { stgScriptContainer(); }},
           });
