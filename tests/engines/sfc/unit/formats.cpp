// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command's unit tests: its file formats, on files built here.

#include "common/bytes.h"
#include "engines/sfc/formats/bft_font.h"
#include "engines/sfc/formats/intro_movie.h"
#include "engines/sfc/formats/model.h"
#include "engines/sfc/formats/settings.h"
#include "engines/sfc/formats/sprite_archive.h"
#include "engines/sfc/formats/text_tables.h"

#include "tests/support/test.h"

#include <algorithm>
#include <cstring>
#include <optional>
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
    // The objects: their section's size and count, then the records, each
    // its kind, number, 0x99 and name. A screen (kind 3) with fields that
    // aren't read, then the second image's own (kind 1): four zero bytes,
    // its size and offset.
    put32(d, 0); // the section's size, set below
    put32(d, 2);
    const auto record = [&d](uint16_t kind, uint32_t id, const std::string &name) {
        put16(d, kind);
        put32(d, id);
        put16(d, 0x99);
        put32(d, uint32_t(name.size()));
        d.insert(d.end(), name.begin(), name.end());
    };
    record(3, 1, "TestScreen");
    put32(d, 640);
    put32(d, 0x99); // a field like a record's start, but not the next number
    record(1, 2, "Test_bmp");
    put32(d, 0);
    put32(d, 20 + 512 + 4 + 3);
    put32(d, 14 + 20 + 8 + 16);
    put16(d, 0xff);
    writeLE32(d.data() + objects, uint32_t(d.size() - objects));
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
    const auto &objects = archive.objects();
    CHECK(objects.size() == 2 && objects[0].kind == 3 && objects[0].name == "TestScreen" && objects[0].size == 30);
    CHECK(objects.size() == 2 && objects[1].kind == 1 && objects[1].id == 2 && objects[1].name == "Test_bmp");
    CHECK(!archive.decode(b, s));
    CHECK(rgba(1, 0) == (std::vector<int>{255, 255, 255, 255}) && rgba(2, 0)[3] == 0);
}

// The objects' fields: a group's children and where they go, a button's
// images by list, and a screen drawn from them.
void testSpriteObjects() {
    std::vector<uint8_t> d;
    putTag(d, "MDM!");
    put16(d, 0x99);
    put16(d, 0x100);
    put16(d, 0x10);
    put32(d, 0);
    // A 4x2 image, all red, its hot spot at 2,1.
    std::vector<uint8_t> codes;
    for (const uint16_t v : {0x4004, 0x7c00, 0x4004, 0x7c00})
        put16(codes, v);
    putImage(d, 4, 2, 16, {}, {0, 2}, codes);
    const uint32_t objects = uint32_t(d.size());
    writeLE32(d.data() + 10, objects);
    put32(d, 0);
    put32(d, 3);
    const auto record = [&d](uint16_t kind, uint32_t id, const std::string &name, const std::string &cls) {
        put16(d, kind);
        put32(d, id);
        put16(d, 0x99);
        put32(d, uint32_t(name.size()));
        d.insert(d.end(), name.begin(), name.end());
        put32(d, uint32_t(cls.size()));
        d.insert(d.end(), cls.begin(), cls.end());
    };
    // The screen: no class, a word, two children (the button at 10,5 in
    // front, the image itself at 20,5 behind), then fields not read.
    record(3, 1, "TestScreen", "");
    put32(d, 0);
    put32(d, 2);
    for (const uint32_t v : {3u, 10u, 5u, 1u, 2u, 20u, 5u, 2u})
        put32(d, v);
    put32(d, 32);
    // The image's own record (its first field read as an empty class).
    record(1, 2, "Test_bmp", "");
    put32(d, 20 + 8 + 8); // its size and offset
    put32(d, 14);
    put16(d, 0xff);
    // The button: its class, fields not read (one looking like a list),
    // then its lists: [2], [], [2].
    record(8, 3, "TestButton", "SFCPushBtn");
    for (const uint32_t v : {0u, 32u, 1u, 0x48u})
        put32(d, v);
    for (const uint32_t v : {1u, 2u, 0u, 1u, 2u})
        put32(d, v);
    writeLE32(d.data() + objects, uint32_t(d.size() - objects));

    SpriteArchive archive;
    CHECK(!archive.open(d));
    const auto *screen = archive.object(1);
    const auto *image = archive.object(2);
    const auto *button = archive.object(3);
    CHECK(screen && screen->children.size() == 2 && screen->className.empty());
    if (!screen || screen->children.size() != 2 || !image || !button)
        return;
    CHECK(screen->children[0].id == 3 && screen->children[0].x == 10 && screen->children[0].y == 5 &&
          screen->children[0].depth == 1 && screen->children[1].id == 2);
    CHECK(image->image == 0 && image->children.empty() && archive.images()[0].name == "Test_bmp");
    CHECK(button->className == "SFCPushBtn" && button->frames[0] == std::vector<uint32_t>{2} &&
          button->frames[1].empty() && button->frames[2] == std::vector<uint32_t>{2});
    CHECK(!archive.object(4));
    // Drawn: each image's hot spot where it's placed (the button by its
    // first state's image), nothing elsewhere.
    Surface out;
    out.create(32, 16, PixelFormat::kRGBA32);
    CHECK(!archive.compose(1, out, 0, 0));
    const auto red = [&](int x, int y) { return out.row(y)[4 * x] == 255 && out.row(y)[4 * x + 3] == 255; };
    CHECK(red(8, 4) && red(11, 5) && !red(12, 4) && !red(7, 4));
    CHECK(red(18, 4) && red(21, 5) && !red(22, 4) && out.row(0)[3] == 0);
    // Placed off the surface's edge, it's cut there.
    CHECK(!archive.compose(1, out, -10, -4));
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

// A program as SFC2Intro.exe is: one section, its data to 0x400, then a
// Bink movie of `size` bytes after the 8 of its signature and size.
std::vector<uint8_t> makeIntro(uint32_t size, uint32_t claimed) {
    std::vector<uint8_t> d(0x400, 0);
    d[0] = 'M', d[1] = 'Z';
    writeLE32(d.data() + 0x3c, 0x40);
    std::memcpy(d.data() + 0x40, "PE\0\0", 4);
    writeLE16(d.data() + 0x46, 1);    // one section
    writeLE16(d.data() + 0x54, 0xe0); // the optional header's size
    writeLE16(d.data() + 0x58, 0x10b);
    const size_t section = 0x58 + 0xe0;
    std::memcpy(d.data() + section, ".text", 5);
    writeLE32(d.data() + section + 16, 0x200); // its data's size, and where it is
    writeLE32(d.data() + section + 20, 0x200);
    const std::string bink = "BIKi";
    d.insert(d.end(), bink.begin(), bink.end());
    put32(d, claimed);
    for (uint32_t i = 0; i < size; ++i)
        d.push_back(uint8_t(i));
    return d;
}

void testIntroMovie() {
    std::vector<uint8_t> movie;
    CHECK(!introMovie(makeIntro(20, 20), movie));
    CHECK(movie.size() == 28 && movie[0] == 'B' && movie[3] == 'i' && movie[8] == 0 && movie[27] == 19);
    // A movie longer than the file, or none after the program, isn't one.
    CHECK(introMovie(makeIntro(20, 21), movie) && movie.empty());
    std::vector<uint8_t> none = makeIntro(20, 20);
    none[0x400] = 'X';
    CHECK(introMovie(none, movie));
    CHECK(introMovie(std::vector<uint8_t>(64, 0), movie)); // not a program
}

void testTabTable() {
    const std::string text = "\tA comment\r\nRace\tBPV\r\nFederation\t\"1\"\"2\"\r\n\t\r\nKlingon\t3";
    TabTable table;
    CHECK(!table.read(std::vector<uint8_t>(text.begin(), text.end())));
    CHECK(table.rows.size() == 5);
    CHECK(!TabTable::isData(table.rows[0]) && TabTable::isData(table.rows[1]) && !TabTable::isData(table.rows[3]));
    CHECK(table.rows[2][1] == "1\"2" && table.rows[4][1] == "3");
}

// A .gf: lines before any section, a comment line, a quoted value keeping
// its // and spaces, a comment after a plain value, sections by path.
void testSettings() {
    const std::string text = "Name = \"Time\"\r\n// a comment\r\n\r\n[Clock]\r\nTurnsPerYear\t\t= 5\r\n"
                             "MilliSecondsPerTurn = 60000\t // (60000)\r\nUrl=\"http://a b\"\r\n"
                             "[Clock/StartingDate]\nBaseYear = 2263\n";
    SettingsFile f;
    CHECK(!f.read(std::vector<uint8_t>(text.begin(), text.end())));
    CHECK(f.sections.size() == 3 && f.sections[0].name.empty() && f.sections[2].name == "Clock/StartingDate");
    CHECK(f.value("", "name") == std::optional<std::string>("Time"));
    CHECK(f.value("clock", "MilliSecondsPerTurn") == std::optional<std::string>("60000"));
    CHECK(f.value("Clock", "Url") == std::optional<std::string>("http://a b"));
    CHECK(f.section("Clock") && f.section("Clock")->entries[0].key == "TurnsPerYear" &&
          !f.section("Clock")->entries[0].quoted && f.section("Clock")->entries[2].quoted);
    CHECK(!f.value("Clock", "BaseYear") &&
          f.value("Clock/StartingDate", "BaseYear") == std::optional<std::string>("2263"));
    const std::string open = "[A]\r\n1=\"RIS Sui Iuris\r\n2=\"Next\"\r\n";
    CHECK(!f.read(std::vector<uint8_t>(open.begin(), open.end())) &&
          f.value("A", "1") == std::optional<std::string>("RIS Sui Iuris") &&
          f.value("A", "2") == std::optional<std::string>("Next"));
    const std::string bad = "[Open\r\n";
    CHECK(f.read(std::vector<uint8_t>(bad.begin(), bad.end())));
    const std::string bare = "[A]\r\nno equals here\r\n";
    CHECK(f.read(std::vector<uint8_t>(bare.begin(), bare.end())));
}

// A .mvm: its header names the classes and where the data starts; the
// data is a 2x1 map of seven-field cells.
void testMap() {
    using namespace std::string_literals;
    std::string header = "[Version]\r\nFileType=\"StarfleetCommand.Map.Metaverse\"\r\n[Version/Data/Offsets]\r\nData=";
    const std::string rest = "\r\n[Classes/Bases]\r\n0=\"(none)\"\r\n1=\"Starbase\"\r\n[Objects]\r\n\0\0"s;
    // The offset counts its own digits: 3 of them.
    header += std::to_string(header.size() + 3 + rest.size()) + rest;
    std::vector<uint8_t> d(header.begin(), header.end());
    put32(d, 2);
    put32(d, 1);
    put32(d, 2);
    for (int cell = 0; cell < 2; ++cell) {
        put32(d, 5);
        put32(d, 0x3f800000); // 1.0
        put32(d, 50);
        put32(d, uint32_t(cell + 1));
        put32(d, 0);
        put32(d, 0);
        put32(d, uint32_t(cell));
    }
    MetaverseMap map;
    CHECK(!map.read(d));
    CHECK(map.width == 2 && map.height == 1 && map.cells.size() == 2 && map.cells[1].region == 2 &&
          map.cells[1].base == 1 && map.cells[0].impedence == 1.0f);
    CHECK(map.bases == (std::vector<std::string>{"(none)", "Starbase"}));
    d.resize(d.size() - 4);
    CHECK(map.read(d));
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
               {"sprite objects", [](const std::filesystem::path &) { testSpriteObjects(); }},
               {"font", [](const std::filesystem::path &) { testFont(); }},
               {"model", [](const std::filesystem::path &) { testModel(); }},
               {"model bad material", [](const std::filesystem::path &) { testModelBadMaterial(); }},
               {"model quad", [](const std::filesystem::path &) { testModelQuad(); }},
               {"intro movie", [](const std::filesystem::path &) { testIntroMovie(); }},
               {"tab table", [](const std::filesystem::path &) { testTabTable(); }},
               {"string table", [](const std::filesystem::path &) { testStringTable(); }},
               {"settings", [](const std::filesystem::path &) { testSettings(); }},
               {"map", [](const std::filesystem::path &) { testMap(); }},
           });
