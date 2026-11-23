// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command's unit tests: its file formats, on files built here.

#include "common/bytes.h"
#include "engines/sfc/formats/bft_font.h"
#include "engines/sfc/formats/model.h"
#include "engines/sfc/formats/sprite_archive.h"
#include "engines/sfc/formats/text_tables.h"

#include "tests/support/test.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::sfc;

namespace {

void put16(std::vector<uint8_t> &d, uint16_t v) {
    d.push_back(uint8_t(v));
    d.push_back(uint8_t(v >> 8));
}

void put32(std::vector<uint8_t> &d, uint32_t v) {
    put16(d, uint16_t(v));
    put16(d, uint16_t(v >> 16));
}

void putFloat(std::vector<uint8_t> &d, float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    put32(d, bits);
}

void putTag(std::vector<uint8_t> &d, const char *tag) {
    d.insert(d.end(), tag, tag + 4);
}

// An image's header, then its rows' table and codes.
void putImage(std::vector<uint8_t> &d, uint16_t w, uint16_t h, uint16_t bits, const std::vector<uint8_t> &palette,
              const std::vector<uint32_t> &rows, const std::vector<uint8_t> &codes) {
    for (const uint16_t v : {w, h, uint16_t(w / 2), uint16_t(h / 2), uint16_t(0), bits, uint16_t(255), uint16_t(2)})
        put16(d, v);
    put32(d, uint32_t(palette.size() + 4 * rows.size() + codes.size()));
    d.insert(d.end(), palette.begin(), palette.end());
    for (const uint32_t r : rows)
        put32(d, r);
    d.insert(d.end(), codes.begin(), codes.end());
}

std::vector<uint8_t> makeSprites() {
    std::vector<uint8_t> d;
    putTag(d, "MDM!");
    put16(d, 0x99);
    put16(d, 0x100);
    put16(d, 0x10);
    put32(d, 0); // the objects' offset, set below
    // A 16-bit 4x2 image: a row of a fill and literals; a row of a skip
    // and a fill.
    std::vector<uint8_t> codes;
    for (const uint16_t v : {0x4002, 0x7c00, 0x0002, 0x03e0, 0x001f, 0xc003, 0x4001, 0x7fff})
        put16(codes, v);
    putImage(d, 4, 2, 16, {}, {0, 5}, codes);
    // An 8-bit 3x1 image: palette entry 1 is white; a fill of 2 and a skip.
    std::vector<uint8_t> palette(512, 0);
    palette[2] = 0xff;
    palette[3] = 0x7f;
    putImage(d, 3, 1, 8, palette, {0}, {0x42, 0x01, 0xc1});
    const uint32_t objects = uint32_t(d.size());
    writeLE32(d.data() + 10, objects);
    // An object naming the second image: its name, four zero bytes, its
    // size and offset.
    const std::string name = "Test_bmp";
    put32(d, 0x12345678);
    put32(d, uint32_t(name.size()));
    d.insert(d.end(), name.begin(), name.end());
    put32(d, 0);
    put32(d, 20 + 512 + 4 + 3);
    put32(d, 14 + 20 + 8 + 16);
    return d;
}

void testSprites() {
    SpriteArchive archive;
    CHECK(!archive.open(makeSprites()));
    CHECK(archive.images().size() == 2);
    if (archive.images().size() != 2)
        return;
    const auto &a = archive.images()[0];
    CHECK(a.offset == 14 && a.width == 4 && a.height == 2 && a.bits == 16 && a.hotX == 2 && a.name.empty());
    Surface s;
    CHECK(!archive.decode(a, s));
    const auto rgba = [&](int x, int y) {
        const uint8_t *p = s.row(y) + 4 * x;
        return std::vector<int>{p[0], p[1], p[2], p[3]};
    };
    CHECK(rgba(0, 0) == (std::vector<int>{255, 0, 0, 255}) && rgba(1, 0) == (std::vector<int>{255, 0, 0, 255}));
    CHECK(rgba(2, 0) == (std::vector<int>{0, 255, 0, 255}) && rgba(3, 0) == (std::vector<int>{0, 0, 255, 255}));
    CHECK(rgba(0, 1) == (std::vector<int>{0, 0, 0, 0}) && rgba(3, 1) == (std::vector<int>{255, 255, 255, 255}));
    const auto &b = archive.images()[1];
    CHECK(b.bits == 8 && b.width == 3 && b.name == "Test_bmp");
    CHECK(!archive.decode(b, s));
    CHECK(rgba(1, 0) == (std::vector<int>{255, 255, 255, 255}) && rgba(2, 0)[3] == 0);
}

// A run past the row's width is an error, not a write past the image.
void testSpriteOverrun() {
    std::vector<uint8_t> d = makeSprites();
    writeLE16(d.data() + 14 + 20 + 8, 0x4005);
    SpriteArchive archive;
    CHECK(!archive.open(d));
    Surface s;
    CHECK(!archive.images().empty() && archive.decode(archive.images()[0], s));
}

void testFont() {
    std::vector<uint8_t> d;
    put32(d, 10);
    put32(d, 5);
    put32(d, 34);
    for (int i = 0; i < 33; ++i)
        for (int k = 0; k < 5; ++k)
            put32(d, 0);
    // '!': a 2x3 bitmap two pixels down, advancing 3.
    for (const int v : {1, 2, 3, 2, 3})
        put32(d, uint32_t(v));
    for (const uint8_t c : {255, 0, 128, 0, 255, 255})
        d.push_back(c);
    BftFont font;
    CHECK(!font.read(d));
    CHECK(font.lineHeight() == 10 && font.half() == 5 && font.glyphs().size() == 34);
    const auto &g = font.glyphs()[33];
    CHECK(g.x == 1 && g.y == 2 && g.advance == 3 && g.width == 2 && g.height == 3 && g.coverage[2] == 128);
    CHECK(font.textWidth("!!") == 6 && font.textWidth("\x01") == 0);
    Surface s;
    s.create(8, 8, PixelFormat::kRGBA32);
    CHECK(font.draw(s, 0, 0, "!", {200, 100, 50}) == 3);
    CHECK(s.row(2)[4 * 1] == 200 && s.row(3)[4 * 1] == 100 && s.row(2)[4 * 2] == 0);
    d.push_back(0);
    CHECK(font.read(d));
}

// A model of one polygon of `corners` corners (3 or 4).
std::vector<uint8_t> makeModel(uint32_t corners = 3) {
    std::vector<uint8_t> body;
    put32(body, 512);
    putFloat(body, 1.5f);
    put32(body, 1);
    for (const float f : {5.0f, 0.0f, 180.0f, 350.0f, 0.0f, 0.0f})
        putFloat(body, f);
    const std::string strs = std::string("gun\0hull\0hulli", 15);
    putTag(body, "STRS");
    put32(body, uint32_t(strs.size()));
    body.insert(body.end(), strs.begin(), strs.end());
    body.push_back(0); // padded to four bytes
    putTag(body, "PNTS");
    put32(body, 16);
    for (const float f : {1.0f, 2.0f, 3.0f})
        putFloat(body, f);
    put32(body, 0);
    putTag(body, "MATS");
    put32(body, 4 + 24 + 4 + 32);
    put32(body, 4); // untextured
    put32(body, 0xffffff);
    for (int i = 0; i < 5; ++i)
        put32(body, 0);
    put32(body, 0x23); // textured
    put32(body, 0xffffff);
    for (const uint32_t v : {0u, 0u, 0u, 0u, 4u, 0u, 9u})
        put32(body, v);
    std::vector<uint8_t> mesh;
    put32(mesh, corners);
    put32(mesh, 1);
    putTag(mesh, "VTXS");
    put32(mesh, 12 * corners);
    for (const float f : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f})
        if (mesh.size() < 16 + 12 * size_t(corners))
            putFloat(mesh, f);
    putTag(mesh, "POLY");
    put32(mesh, 20 + corners * 28);
    put32(mesh, corners);
    put32(mesh, 1);
    for (const float f : {0.0f, 0.0f, 1.0f})
        putFloat(mesh, f);
    for (uint32_t k = 0; k < corners; ++k) {
        put32(mesh, k | (k == 2 ? 0x20000u : 0u));
        for (const float f : {0.0f, 0.0f, 0.0f, 0.25f * float(k), 0.5f})
            putFloat(mesh, f);
        put32(mesh, 0);
    }
    putTag(body, "MOD0");
    put32(body, uint32_t(mesh.size()));
    body.insert(body.end(), mesh.begin(), mesh.end());
    std::vector<uint8_t> d;
    putTag(d, "MDLS");
    put32(d, uint32_t(body.size()));
    d.insert(d.end(), body.begin(), body.end());
    return d;
}

void testModel() {
    Model model;
    CHECK(!model.read(makeModel()));
    CHECK(model.version() == 512 && model.radius() == 1.5f && model.settings()[2] == 180.0f);
    CHECK(model.points().size() == 1 && model.points()[0].name == "gun" && model.points()[0].position[2] == 3.0f);
    CHECK(model.materials().size() == 2 && model.materials()[0].texture.empty());
    CHECK(model.materials()[1].texture == "hull" && model.materials()[1].illumination == "hulli");
    CHECK(model.meshes().size() == 1);
    if (model.meshes().size() != 1)
        return;
    const auto &mesh = model.meshes()[0];
    CHECK(mesh.vertices.size() == 3 && mesh.triangles.size() == 1);
    const auto &t = mesh.triangles[0];
    CHECK(t.material == 1 && t.normal[2] == 1.0f);
    CHECK(t.corners[1].vertex == 1 && t.corners[1].u == 0.25f && t.corners[2].vertex == 2 && t.corners[2].piece == 2);
}

// A polygon of four corners (a few models have them): two triangles, cut
// from its first corner, for the one polygon the level counts.
void testModelQuad() {
    Model model;
    CHECK(!model.read(makeModel(4)));
    CHECK(model.meshes().size() == 1);
    if (model.meshes().size() != 1)
        return;
    const auto &mesh = model.meshes()[0];
    CHECK(mesh.vertices.size() == 4 && mesh.triangles.size() == 2);
    if (mesh.triangles.size() != 2)
        return;
    CHECK(mesh.triangles[0].corners[0].vertex == 0 && mesh.triangles[0].corners[1].vertex == 1 &&
          mesh.triangles[0].corners[2].vertex == 2);
    CHECK(mesh.triangles[1].corners[0].vertex == 0 && mesh.triangles[1].corners[1].vertex == 2 &&
          mesh.triangles[1].corners[2].vertex == 3 && mesh.triangles[1].material == 1);
}

// A triangle naming a material that isn't there is an error.
void testModelBadMaterial() {
    std::vector<uint8_t> d = makeModel();
    const std::string poly = "POLY";
    const auto at = std::search(d.begin(), d.end(), poly.begin(), poly.end()) - d.begin();
    writeLE32(d.data() + at + 12, 2);
    Model model;
    CHECK(model.read(d));
}

void testTabTable() {
    const std::string text = "\tA comment\r\nRace\tBPV\r\nFederation\t\"1\"\"2\"\r\n\t\r\nKlingon\t3";
    TabTable table;
    CHECK(!table.read(std::vector<uint8_t>(text.begin(), text.end())));
    CHECK(table.rows.size() == 5);
    CHECK(!TabTable::isData(table.rows[0]) && TabTable::isData(table.rows[1]) && !TabTable::isData(table.rows[3]));
    CHECK(table.rows[2][1] == "1\"2" && table.rows[4][1] == "3");
}

void testStringTable() {
    const std::u16string text = u"HEADING\r\n|tKey|First\r\n||Second\r\n|tOther|été\r\n";
    std::vector<uint8_t> d;
    for (const char16_t c : text)
        put16(d, uint16_t(c));
    StringTable table;
    CHECK(!table.read(d));
    CHECK(table.entries.size() == 3);
    if (table.entries.size() != 3)
        return;
    CHECK(table.entries[0].key.empty() && table.entries[0].text[0] == "HEADING");
    CHECK(table.entries[1].key == "tKey" && table.entries[1].text.size() == 2 && table.entries[1].text[1] == "Second");
    CHECK(table.entries[2].text[0] == "\xc3\xa9t\xc3\xa9");
}

} // namespace

static const reluva::test::Suite
    kSuite("sfc.unit.formats",
           {
               {"sprites", [](const std::filesystem::path &) { testSprites(); }},
               {"sprite overrun", [](const std::filesystem::path &) { testSpriteOverrun(); }},
               {"font", [](const std::filesystem::path &) { testFont(); }},
               {"model", [](const std::filesystem::path &) { testModel(); }},
               {"model bad material", [](const std::filesystem::path &) { testModelBadMaterial(); }},
               {"model quad", [](const std::filesystem::path &) { testModelQuad(); }},
               {"tab table", [](const std::filesystem::path &) { testTabTable(); }},
               {"string table", [](const std::filesystem::path &) { testStringTable(); }},
           });
