// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Need for Speed engine's detection and folder finding,
// against folders built here, so no game data is needed.

#include "engines/nfs/bnk.h"
#include "engines/nfs/col.h"
#include "engines/nfs/fce.h"
#include "engines/nfs/ffn.h"
#include "engines/nfs/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/hrz.h"
#include "engines/nfs/mad.h"
#include "engines/nfs/mus.h"
#include "engines/nfs/games.h"
#include "engines/nfs/refpack.h"
#include "engines/nfs/text_table.h"
#include "engines/nfs/viv.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::nfs;
namespace stdfs = std::filesystem;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

void touch(const stdfs::path& file, const std::string& contents = "x") {
    stdfs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << contents;
}

// An install as the game's installer lays it out (mixed case), without the
// CD's music and movies.
void makeInstall(const stdfs::path& dir) {
    touch(dir / "nfs3.exe");
    touch(dir / "GameData/Tracks/trk000/tr00.frd");
    touch(dir / "GameData/Tracks/trk001/tr01.frd");
    touch(dir / "GameData/Tracks/Tutor/readme");
    touch(dir / "GameData/CarModel/Diab/car.viv");
    touch(dir / "GameData/CarModel/f355/car.viv");
    touch(dir / "GameData/CarModel/Empty/notes.txt");
    touch(dir / "GameData/CarModel/Traffic/0000/car.viv");
}

// The CD, in its lower-case spelling, with the music and movies.
void makeCd(const stdfs::path& dir) {
    touch(dir / "nfs3.exe");
    touch(dir / "gamedata/tracks/trk000/tr00.frd");
    touch(dir / "gamedata/audio/pc/alpirock.mus");
    touch(dir / "gamedata/audio/pc/alpirock.map");
    touch(dir / "gamedata/audio/pc/show1.MUS");
    touch(dir / "fedata/movies/titleav.mad");
}

void testDetection(const stdfs::path& tmp) {
    const stdfs::path install = tmp / "detect/Need For Speed III";
    makeInstall(install);

    // The files exist but aren't the known release's, so only the
    // fallback table knows them.
    CHECK(detectFromTable("nfs", detectionTable(), install).empty());
    auto found = detectFromTable("nfs", fallbackTable(), install);
    CHECK(found.size() == 1);
    if (!found.empty()) {
        CHECK(found[0].gameId == "nfs3");
        CHECK(found[0].engineId == "nfs");
    }

    // The CD is found as the game too (its names are lower case).
    const stdfs::path cd = tmp / "detect/NFS3-CD";
    makeCd(cd);
    CHECK(detectFromTable("nfs", fallbackTable(), cd).size() == 1);

    // Without the first track it isn't the game.
    CHECK(detectFromTable("nfs", fallbackTable(), tmp / "detect").empty());

    CHECK(findGame("nfs3") != nullptr);
    CHECK(findGame("nfs4") == nullptr);
}

void testFolders(const stdfs::path& tmp) {
    const stdfs::path root = tmp / "folders";
    const stdfs::path install = root / "Need For Speed III";
    makeInstall(install);

    // Nothing beside the install: no CD.
    GameFolders f = findFolders(install, {}, "nfs3.exe");
    CHECK(f.install == install);
    CHECK(f.cd.empty());

    // A folder beside it without the executable isn't taken for the CD.
    touch(root / "Music/GameData/Audio/pc/alpirock.mus");
    CHECK(findFolders(install, {}, "nfs3.exe").cd.empty());

    // A copy of the CD beside the install is.
    makeCd(root / "NFS3-CD");
    f = findFolders(install, {}, "nfs3.exe");
    CHECK(f.cd == root / "NFS3-CD");
    CHECK(f.cdSource == "folder beside the install");

    // An empty cdpath is as none.
    CHECK(findFolders(install, {{"cdpath", ""}}, "nfs3.exe").cd == root / "NFS3-CD");

    // cdpath wins, absolute or relative to the install.
    makeCd(root / "Elsewhere");
    f = findFolders(install, {{"cdpath", (root / "Elsewhere").string()}}, "nfs3.exe");
    CHECK(f.cd == root / "Elsewhere");
    CHECK(f.cdSource == "cdpath setting");
    f = findFolders(install, {{"cdpath", "../Elsewhere"}}, "nfs3.exe");
    CHECK(f.cd == install / "../Elsewhere");

    // A cdpath without the CD's files gives no CD, rather than another.
    CHECK(findFolders(install, {{"cdpath", (root / "Nowhere").string()}}, "nfs3.exe").cd.empty());

    // Music copied into the install (or the game added from the CD): the
    // install is the CD.
    touch(install / "GameData/Audio/pc/hometech.mus");
    f = findFolders(install, {}, "nfs3.exe");
    CHECK(f.cd == install);
    CHECK(f.cdSource == "install folder");
}

void testListings(const stdfs::path& tmp) {
    const stdfs::path install = tmp / "listings";
    makeInstall(install);

    auto tracks = trackFolders(install);
    CHECK(tracks.size() == 2);
    if (tracks.size() == 2)
        CHECK(tracks[0] == "trk000" && tracks[1] == "trk001");

    // Folders with a car.viv, not Traffic's.
    auto cars = carFolders(install);
    CHECK(cars.size() == 2);
    if (cars.size() == 2)
        CHECK(cars[0] == "Diab" && cars[1] == "f355");

    const stdfs::path cd = tmp / "listings-cd";
    makeCd(cd);
    auto music = filesWithExtension(cd, "GameData/Audio/pc", ".mus");
    CHECK(music.size() == 2); // alpirock.mus and show1.MUS, not the .map
    CHECK(filesWithExtension(cd, "FeData/movies", ".mad").size() == 1);
    CHECK(filesWithExtension(install, "FeData/movies", ".mad").empty());
    CHECK(filesWithExtension(stdfs::path(), "FeData/movies", ".mad").empty());
}

using Bytes = std::vector<uint8_t>;

void put16(Bytes& b, unsigned v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void put32(Bytes& b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}
void put32be(Bytes& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8)
        b.push_back(uint8_t(v >> s));
}
void putText(Bytes& b, const char* s, size_t n) { b.insert(b.end(), s, s + n); }

void testRefPack() {
    // The three copy forms and both literal forms, by hand (refpack.md).
    const Bytes small = {0x10, 0xfb, 0x00, 0x00, 0x0c,
                         0xe0, 'a', 'b', 'c', 'd', // 4 literals
                         0x01, 0x04, 'e',          // 1 literal, then 3 from 5 back
                         0x80, 0x00, 0x00,         // 4 from 1 back (overlapping)
                         0xfc};                    // end, no literals
    Bytes out;
    CHECK(isRefPack(small));
    CHECK(!decompressRefPack(small, out));
    CHECK(std::string(out.begin(), out.end()) == "abcdeabccccc");

    // The 4-byte form, from further back than the 3-byte form reaches.
    Bytes big = {0x10, 0xfb, 0x00, 0x43, 0x94}; // 17300 bytes
    Bytes expect;
    for (int i = 0; i < 17000; ++i)
        expect.push_back(uint8_t(i * 7));
    size_t done = 0;
    while (17000 - done >= 112) { // 0xfb: 112 literals
        big.push_back(0xfb);
        big.insert(big.end(), expect.begin() + done, expect.begin() + done + 112);
        done += 112;
    }
    while (17000 - done >= 4) { // 0xe0: 4 literals
        big.push_back(0xe0);
        big.insert(big.end(), expect.begin() + done, expect.begin() + done + 4);
        done += 4;
    }
    CHECK(done == 17000);
    const uint32_t distance = 17000 - 1, length = 300 - 5;
    big.push_back(uint8_t(0xc0 | (distance >> 16) << 4 | (length >> 8) << 2));
    big.push_back(uint8_t(distance >> 8));
    big.push_back(uint8_t(distance));
    big.push_back(uint8_t(length));
    big.push_back(0xfc);
    expect.insert(expect.end(), expect.begin(), expect.begin() + 300);
    CHECK(!decompressRefPack(big, out));
    CHECK(out == expect);

    // The flags: 0x01 adds a compressed size before the size.
    Bytes sized = {0x11, 0xfb, 0x00, 0x00, 0x09, 0x00, 0x00, 0x02, 0xfe, 'h', 'i'};
    CHECK(!decompressRefPack(sized, out));
    CHECK(std::string(out.begin(), out.end()) == "hi");

    // Damage is caught.
    Bytes cut(small.begin(), small.end() - 1);
    CHECK(decompressRefPack(cut, out)); // no end code
    Bytes wrongSize = small;
    wrongSize[4] = 0x0d;
    CHECK(decompressRefPack(wrongSize, out));
    Bytes tooFar = {0x10, 0xfb, 0x00, 0x00, 0x05, 0xe0, 'a', 'b', 'c', 'd', 0x00, 0x09, 0xfc};
    CHECK(decompressRefPack(tooFar, out)); // copy from 10 back with 4 written
    CHECK(!isRefPack(Bytes{'S', 'H', 'P', 'I', 0}));
    const Bytes huffman = {0x32, 0xfb, 0x07, 0xc4, 0x48, 0x71};
    CHECK(!isRefPack(huffman) && isOtherEaCompression(huffman));
    CHECK(!isOtherEaCompression(small));
    Bytes copy = huffman;
    CHECK(unpackIfRefPack(copy)); // fails rather than passing it on

    Bytes plain = {'S', 'H', 'P', 'I', 0};
    CHECK(!unpackIfRefPack(plain));
    CHECK(plain.size() == 5);
}

Bytes makeViv(const std::vector<std::pair<std::string, std::string>>& files) {
    uint32_t dirSize = 16;
    for (const auto& f : files)
        dirSize += 8 + uint32_t(f.first.size()) + 1;
    const uint32_t first = (dirSize + 3) & ~3u;
    Bytes b = {'B', 'I', 'G', 'F'};
    put32be(b, 0); // total size, filled in below
    put32be(b, uint32_t(files.size()));
    put32be(b, first);
    uint32_t offset = first;
    for (const auto& f : files) {
        put32be(b, offset);
        put32be(b, uint32_t(f.second.size()));
        putText(b, f.first.c_str(), f.first.size() + 1);
        offset += (uint32_t(f.second.size()) + 3) & ~3u;
    }
    for (const auto& f : files) {
        b.resize((b.size() + 3) & ~size_t(3));
        putText(b, f.second.data(), f.second.size());
    }
    const uint32_t total = uint32_t(b.size());
    for (int i = 0; i < 4; ++i)
        b[4 + i] = uint8_t(total >> (24 - 8 * i));
    return b;
}

void testViv() {
    VivArchive viv;
    CHECK(!viv.open(makeViv({{"carp.txt", "Serial Number"}, {"car.fce", "abc"}}), "test.viv"));
    CHECK(viv.entries().size() == 2);
    CHECK(viv.list() == (std::vector<std::string>{"carp.txt", "car.fce"}));
    CHECK(viv.has("CAR.FCE"));
    CHECK(!viv.has("car.tga"));
    Bytes data;
    CHECK(!viv.read("Carp.TXT", data));
    CHECK(std::string(data.begin(), data.end()) == "Serial Number");
    CHECK(!viv.read("car.fce", data) && data.size() == 3);
    CHECK(viv.read("car.tga", data));
    CHECK(viv.describe() == "test.viv");

    // An empty archive is fine; damage isn't.
    CHECK(!viv.open(makeViv({}), "empty.viv"));
    CHECK(viv.entries().empty());
    Bytes bad = makeViv({{"a", "1234"}});
    bad[0] = 'X';
    CHECK(viv.open(bad, "bad.viv"));
    Bytes past = makeViv({{"a", "1234"}});
    past[19] = 0x40; // the file's offset puts it past the end
    CHECK(viv.open(past, "past.viv"));
    Bytes unterminated = makeViv({{"abc", "1234"}});
    unterminated.resize(27); // cut inside the name
    unterminated[15] = 27;
    CHECK(viv.open(unterminated, "cut.viv"));

    // Read from disk too.
    const stdfs::path file = stdfs::temp_directory_path() / "reluva_nfs_test.viv";
    const Bytes disk = makeViv({{"x.bin", "xyz"}});
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(disk.data()), std::streamsize(disk.size()));
    CHECK(!viv.open(file));
    CHECK(viv.has("x.bin"));
    stdfs::remove(file);
    CHECK(viv.open(file));
}

// A shape block: the type, the offset to the next block (0: last), then
// width, height, centre and position.
void putShapeHeader(Bytes& b, uint8_t type, uint32_t next, int w, int h, int cx, int cy, int x, int y) {
    b.push_back(type);
    b.push_back(uint8_t(next));
    b.push_back(uint8_t(next >> 8));
    b.push_back(uint8_t(next >> 16));
    put16(b, unsigned(w));
    put16(b, unsigned(h));
    put16(b, unsigned(cx) & 0xffff);
    put16(b, unsigned(cy) & 0xffff);
    put16(b, unsigned(x) & 0xffff);
    put16(b, unsigned(y) & 0xffff);
}

Bytes makeShapeBank(const std::vector<std::pair<std::string, Bytes>>& shapes) {
    Bytes b = {'S', 'H', 'P', 'I'};
    put32(b, 0);
    put32(b, uint32_t(shapes.size()));
    putText(b, "GIMX", 4);
    uint32_t offset = 16 + 8 * uint32_t(shapes.size());
    for (const auto& s : shapes) {
        putText(b, s.first.c_str(), 4);
        put32(b, offset);
        offset += uint32_t(s.second.size());
    }
    for (const auto& s : shapes)
        b.insert(b.end(), s.second.begin(), s.second.end());
    const uint32_t total = uint32_t(b.size());
    for (int i = 0; i < 4; ++i)
        b[4 + i] = uint8_t(total >> (8 * i));
    return b;
}

void testShapes() {
    // 8-bit, 2x1, with a 16-bit palette of 2 colours and a text block.
    Bytes indexed;
    putShapeHeader(indexed, 0x7b, 16 + 4, 2, 1, -256, 3, 10, -20); // pixels padded to 4
    indexed.insert(indexed.end(), {1, 0, 0, 0});
    putShapeHeader(indexed, 0x2d, 16 + 4, 2, 1, 0, 0, 0, 0);
    put16(indexed, 0x7c00);         // red, alpha bit clear
    put16(indexed, 0x8000 | 0x03e0); // green, opaque
    indexed.push_back(0x6f);
    indexed.insert(indexed.end(), {0, 0, 0});
    put32(indexed, 12);
    putText(indexed, "<additive>\r\n", 12);

    // 16-bit 5:6:5, 1x1, with an unknown block.
    Bytes rgb565;
    putShapeHeader(rgb565, 0x78, 18, 1, 1, 0, 0, 0, 0);
    put16(rgb565, 0x001f); // blue
    rgb565.insert(rgb565.end(), {0x7c, 0, 0, 0, 2, 0, 0, 0});

    // 32-bit (B, G, R, A) and 24-bit (B, G, R), 1x1.
    Bytes argb;
    putShapeHeader(argb, 0x7d, 0, 1, 1, 0, 0, 0, 0);
    argb.insert(argb.end(), {0x10, 0x20, 0x30, 0x40});
    Bytes rgb;
    putShapeHeader(rgb, 0x7f, 0, 1, 1, 0, 0, 0, 0);
    rgb.insert(rgb.end(), {0x10, 0x20, 0x30});

    const Bytes file = makeShapeBank({{"pal8", indexed}, {"r565", rgb565}, {"argb", argb}, {"rgb", rgb}});
    ShapeBank bank;
    CHECK(!parseShapeBank(file, bank));
    CHECK(bank.id == "GIMX");
    CHECK(bank.shapes.size() == 4);
    if (bank.shapes.size() != 4)
        return;

    const Shape& s = bank.shapes[0];
    CHECK(s.name == "pal8" && s.type == kShape8Bit && s.width == 2 && s.height == 1);
    CHECK(s.centerX == -256 && s.centerY == 3 && s.x == 10 && s.y == -20);
    CHECK(s.pixels.size() == 2);
    CHECK(s.palette.size() == 8);
    CHECK(s.texts.size() == 1 && s.texts[0] == "<additive>");
    image::Image img;
    CHECK(!decodeShape(s, img));
    CHECK(img.w == 2 && img.h == 1);
    const uint8_t pal8[] = {0, 255, 0, 255, 255, 0, 0, 0};
    CHECK(img.rgba.size() == 8 && std::memcmp(img.rgba.data(), pal8, 8) == 0);

    CHECK(bank.shapes[1].unknown.size() == 1 && bank.shapes[1].unknown[0].type == 0x7c);
    CHECK(!decodeShape(bank.shapes[1], img));
    const uint8_t blue[] = {0, 0, 255, 255};
    CHECK(std::memcmp(img.rgba.data(), blue, 4) == 0);
    CHECK(!decodeShape(bank.shapes[2], img));
    const uint8_t fromArgb[] = {0x30, 0x20, 0x10, 0x40};
    CHECK(std::memcmp(img.rgba.data(), fromArgb, 4) == 0);
    CHECK(!decodeShape(bank.shapes[3], img));
    const uint8_t fromRgb[] = {0x30, 0x20, 0x10, 0xff};
    CHECK(std::memcmp(img.rgba.data(), fromRgb, 4) == 0);

    // Compressed: a RefPack stream of only literals holding the file.
    Bytes packed = {0x10, 0xfb, uint8_t(file.size() >> 16), uint8_t(file.size() >> 8), uint8_t(file.size())};
    size_t done = 0;
    while (file.size() - done >= 4) {
        const size_t n = std::min<size_t>((file.size() - done) & ~size_t(3), 112);
        packed.push_back(uint8_t(0xe0 | ((n - 4) >> 2)));
        packed.insert(packed.end(), file.begin() + done, file.begin() + done + n);
        done += n;
    }
    packed.push_back(uint8_t(0xfc | (file.size() - done)));
    packed.insert(packed.end(), file.begin() + done, file.end());
    ShapeBank fromQfs;
    CHECK(!parseShapeBank(packed, fromQfs));
    CHECK(fromQfs.shapes.size() == 4);

    // A picture larger than its block fails; an 8-bit one without a
    // palette reads but doesn't decode.
    Bytes tooBig;
    putShapeHeader(tooBig, 0x7d, 0, 2, 2, 0, 0, 0, 0);
    tooBig.insert(tooBig.end(), 4, 0);
    CHECK(parseShapeBank(makeShapeBank({{"big", tooBig}}), bank));
    Bytes noPalette;
    putShapeHeader(noPalette, 0x7b, 0, 1, 1, 0, 0, 0, 0);
    noPalette.push_back(0);
    CHECK(!parseShapeBank(makeShapeBank({{"nopl", noPalette}}), bank));
    CHECK(decodeShape(bank.shapes[0], img));
    CHECK(parseShapeBank(Bytes{'S', 'H', 'P', 'X'}, bank));
}

void testTextTable() {
    // Three offsets, then the strings; the last without its 0.
    Bytes b;
    put32(b, 12);
    put32(b, 13);
    put32(b, 25);
    b.push_back(0);
    putText(b, "Skill Level", 12);
    putText(b, "Abilit\xe0 \x85", 9);
    TextTable table;
    CHECK(!parseTextTable(b, table));
    CHECK(table.strings.size() == 3);
    if (table.strings.size() == 3) {
        CHECK(table.strings[0].empty());
        CHECK(table.strings[1] == "Skill Level");
        CHECK(table.strings[2] == "Abilit\xe0 \x85");
        CHECK(cp1252ToUtf8(table.strings[2]) == "Abilit\xc3\xa0 \xe2\x80\xa6"); // à, …
    }
    CHECK(cp1252ToUtf8("\x80\x81\x99") == "\xe2\x82\xac\xef\xbf\xbd\xe2\x84\xa2"); // €, unassigned, ™

    Bytes outside = b;
    outside[4] = 200;
    CHECK(parseTextTable(outside, table));
    Bytes odd = b;
    odd[0] = 13;
    CHECK(parseTextTable(odd, table));
    CHECK(parseTextTable(Bytes{1, 0}, table));
}

void testFont() {
    // Two glyphs over a 4x2 atlas of 4-bit pixels, high nibble first.
    Bytes b = {'F', 'N', 'T', 'F'};
    put32(b, 0);
    put16(b, 100);
    put16(b, 2); // glyphs
    put16(b, 1); // flags
    put16(b, 0);
    put16(b, 0);
    b.push_back(9); // line height
    b.push_back(0);
    put16(b, 32);
    b.resize(28, 0);
    const uint32_t atlas = 32 + 3 * 11; // a table with room for 3
    put32(b, atlas);
    // 'A': 2x2 at (0, 0), advance 3, offsets 1, -1. 'B': 1x1 at (3, 1).
    b.insert(b.end(), {'A', 0, 2, 2, 0, 0, 0, 0, 3, 1, 0xff});
    b.insert(b.end(), {'B', 0, 1, 1, 3, 0, 1, 0, 2, 0, 0});
    b.insert(b.end(), {0xad, 0xad, 9, 9, 9, 9, 9, 9, 9, 9, 9}); // filler past the count
    putShapeHeader(b, 0x7a, 0, 4, 2, 0, 0, 0, 0);
    b.insert(b.end(), {0xf8, 0x00, 0x0f, 0x0f});

    Font font;
    CHECK(!parseFont(b, font));
    CHECK(font.lineHeight == 9 && font.flags == 1);
    CHECK(font.atlasWidth == 4 && font.atlasHeight == 2);
    CHECK(font.coverage == (Bytes{255, 136, 0, 0, 0, 255, 0, 255}));
    CHECK(font.has('A') && font.has('B') && !font.has('C') && !font.has(0xad));
    CHECK(font.glyphs['A'].advance == 3 && font.glyphs['A'].offsetX == 1 && font.glyphs['A'].offsetY == -1);
    CHECK(font.glyphs['B'].x == 3 && font.glyphs['B'].y == 1);
    CHECK(font.measure("AB") == 5);

    // Drawn: 'A' at (0 + 1, 2 - 1), 'B' at (3, 2).
    Surface s;
    s.create(6, 4, PixelFormat::RGBA32);
    CHECK(font.draw(s, 0, 2, "AB", {200, 100, 50}) == 5);
    CHECK(s.row(1)[4] == 200 && s.row(1)[5] == 100 && s.row(1)[6] == 50); // full coverage
    CHECK(s.row(1)[8] == 200 * 136 / 255);                                 // partial
    CHECK(s.row(2)[4 + 1] == 0 && s.row(2)[8] == 200);                     // (1, 2) clear, (2, 2) solid
    CHECK(s.row(2)[3 * 4] == 200);                                         // 'B'

    Bytes outside = b;
    outside[32 + 4] = 3; // 'A' at x 3 runs off the atlas
    CHECK(parseFont(outside, font));
    Bytes wrongType = b;
    wrongType[atlas] = 0x7b;
    CHECK(parseFont(wrongType, font));
    CHECK(parseFont(Bytes(b.begin(), b.end() - 1), font)); // atlas cut short
}

void putFloat(Bytes& b, size_t at, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    for (int i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (8 * i));
}
void putU32(Bytes& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (8 * i));
}

void testModel() {
    // Two parts: a triangle and a quad (two triangles), 7 vertices.
    constexpr size_t kData = 0x1f04;
    const uint32_t nV = 7, nT = 3;
    Bytes b(kData + 24 * nV + 56 * nT + 32 * nV + 12 * nV + 12 * nV, 0);
    putU32(b, 0x04, nT);
    putU32(b, 0x08, nV);
    putU32(b, 0x0c, 1);
    putU32(b, 0x10, 0);
    putU32(b, 0x14, 12 * nV);
    putU32(b, 0x18, 24 * nV);
    putFloat(b, 0x28, 1.0f);
    putU32(b, 0x34, 1); // a dummy
    putFloat(b, 0x38 + 8, 2.0f);
    std::memcpy(&b[0xa04], "HFLO", 4);
    putU32(b, 0xf8, 2);
    putFloat(b, 0xfc + 12 + 0, -0.8f); // part 1's position
    putU32(b, 0x3fc + 4, 3);           // first vertices 0, 3
    putU32(b, 0x4fc, 3);
    putU32(b, 0x4fc + 4, 4);
    putU32(b, 0x5fc + 4, 1); // first triangles 0, 1
    putU32(b, 0x6fc, 1);
    putU32(b, 0x6fc + 4, 2);
    putU32(b, 0x7fc, 1);
    for (int k = 0; k < 4; ++k)
        putU32(b, 0x800 + 4 * k, 10u + k);
    std::memcpy(&b[0xe04], "high body", 9);
    std::memcpy(&b[0xe44], "left front wheel", 16);
    for (uint32_t i = 0; i < nV; ++i) {
        putFloat(b, kData + 12 * i, float(i));
        putFloat(b, kData + 12 * nV + 12 * i + 4, 1.0f); // normals up
    }
    // Triangles: page, three part-local indices, 12 bytes, flags, u[3], v[3].
    const uint32_t tris[3][4] = {{0, 0, 1, 2}, {1, 0, 1, 2}, {1, 2, 3, 0}};
    for (uint32_t t = 0; t < nT; ++t) {
        const size_t at = kData + 24 * nV + 56 * t;
        for (int k = 0; k < 4; ++k)
            putU32(b, at + 4 * k, tris[t][k]);
        putU32(b, at + 28, t == 2 ? 0x80 : 1);
        putFloat(b, at + 32, 0.25f);
        putFloat(b, at + 44 + 8, 0.75f);
    }

    Model m;
    CHECK(!parseModel(b, m));
    CHECK(m.vertices.size() == nV && m.triangles.size() == nT && m.parts.size() == 2);
    if (m.parts.size() == 2 && m.triangles.size() == nT) {
        CHECK(m.parts[0].name == "high body" && m.parts[1].name == "left front wheel");
        CHECK(m.parts[1].position[0] == -0.8f);
        CHECK(m.parts[1].firstVertex == 3 && m.parts[1].vertexCount == 4 && m.parts[1].triangleCount == 2);
        // Part 1's triangles index the model's vertices: 3 + local.
        CHECK(m.triangles[0].vertices == (std::array<uint32_t, 3>{0, 1, 2}));
        CHECK(m.triangles[2].vertices == (std::array<uint32_t, 3>{5, 6, 3}));
        CHECK(m.triangles[1].texturePage == 1 && m.triangles[2].flags == 0x80);
        CHECK(m.triangles[1].u[0] == 0.25f && m.triangles[1].v[2] == 0.75f);
        CHECK(m.vertices[4][0] == 4.0f && m.normals[4][1] == 1.0f);
    }
    CHECK(m.halfSize[0] == 1.0f);
    CHECK(m.dummies.size() == 1 && m.dummies[0].name == "HFLO" && m.dummies[0].position[2] == 2.0f);
    CHECK(m.primaryColours.size() == 1 && m.primaryColours[0].hsbx[3] == 13);
    CHECK(m.secondaryColours.empty());

    Bytes outside = b;
    putU32(outside, kData + 24 * nV + 56 * 2 + 4, 4); // part 1 has 4 vertices: 0-3
    CHECK(parseModel(outside, m));
    Bytes tooMany = b;
    putU32(tooMany, 0x4fc + 4, 5);
    CHECK(parseModel(tooMany, m));
    CHECK(parseModel(Bytes(100, 0), m));
}

void putF(Bytes& b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(b, v);
}

void putPolygon(Bytes& b, std::array<uint16_t, 4> v, uint16_t texture) {
    for (uint16_t i : v)
        put16(b, i);
    put16(b, texture);
    put16(b, 0);
    b.push_back(0x10);
    b.push_back(0xf9);
}

// A one-block track (frd.md): the header, the block, its polygons and
// object layers, the extra object groups and the texture table.
Bytes makeTrack() {
    Bytes b(28, 0);
    put32(b, 0); // one block, stored as one less
    for (float f : {10.f, 1.f, 20.f})
        putF(b, f); // centre
    for (int i = 0; i < 12; ++i)
        putF(b, float(i)); // corners
    const uint32_t nV = 5;
    for (uint32_t v : {nV, 4u, 4u, 4u, nV, 1u})
        put32(b, v);
    for (uint32_t i = 0; i < nV; ++i)
        for (float f : {float(i), 0.f, -float(i)})
            putF(b, f);
    for (uint32_t i = 0; i < nV; ++i)
        put32(b, 0xff102030u + i);
    for (int i = 0; i < 300; ++i) {
        put16(b, i == 0 ? 0 : 0xffff);
        put16(b, 0xffff);
    }
    // Records: start, positions, polygons, road vectors, refs, polygon
    // objects, sounds, lights.
    for (uint32_t n : {7u, 1u, 1u, 1u, 0u, 0u, 1u, 1u})
        put32(b, n);
    b.insert(b.end(), 8 + 8 + 12 + 16 + 16, 0xab);
    // Polygons: list 4 (the road) has two, list 6 (lanes) one, the rest 0.
    for (int list = 0; list < 7; ++list) {
        const uint32_t n = list == 4 ? 2 : list == 6 ? 1 : 0;
        put32(b, n);
        if (!n)
            continue;
        put32(b, n);
        if (list == 4) {
            putPolygon(b, {1, 2, 3, 4}, 0);
            putPolygon(b, {1, 3, 4, 4}, 1);
        } else {
            putPolygon(b, {2, 3, 4, 1}, 1);
        }
    }
    // Object layers: the first holds a type-1 object of one polygon and a
    // type-4 entry; the rest are empty.
    put32(b, 1);
    put32(b, 2);
    put32(b, 1);
    put32(b, 1);
    putPolygon(b, {0, 0, 1, 1}, 0);
    put32(b, 4);
    for (int i = 0; i < 3; ++i)
        put32(b, 0);
    // Extra objects: 4 * 1 + 1 groups; the first holds a still object and
    // a moving one.
    put32(b, 2);
    put32(b, 4);
    put32(b, 0);
    put32(b, 0xffffffe0);
    for (float f : {100.f, 2.f, 300.f})
        putF(b, f);
    put32(b, 0);
    put32(b, 3);
    for (int i = 0; i < 3; ++i)
        for (float f : {float(i), 1.f, 0.f})
            putF(b, f);
    for (int i = 0; i < 3; ++i)
        put32(b, 0xffffffff);
    put32(b, 1);
    putPolygon(b, {0, 1, 2, 2}, 0);
    put32(b, 3); // moving: type, 8 + 12 + 8 bytes, key count, 2 bytes, keys
    b.insert(b.end(), 28, 0);
    put16(b, 2);
    put16(b, 6);
    for (int k = 0; k < 2; ++k) {
        put32(b, uint32_t(k) << 16);
        put32(b, 0);
        put32(b, 0);
        for (int q : {0, 0, k * 100, 16384})
            put16(b, unsigned(q));
    }
    put32(b, 1);
    for (float f : {0.f, 0.f, 0.f})
        putF(b, f);
    put32(b, 0xffffffff);
    put32(b, 0);
    for (int g = 1; g < 5; ++g)
        put32(b, 0);
    // Textures: width, height, u32, 8 corners, u32, lane, picture.
    put32(b, 2);
    for (int t = 0; t < 2; ++t) {
        put16(b, 64);
        put16(b, 32);
        put32(b, 0);
        for (int k = 0; k < 8; ++k)
            putF(b, k * 0.125f);
        put32(b, 0);
        b.push_back(t == 1);
        put16(b, uint16_t(40 + t));
    }
    return b;
}

void testTrack() {
    const Bytes file = makeTrack();
    Track t;
    CHECK(!parseTrack(file, t));
    CHECK(t.blocks.size() == 1 && t.textures.size() == 2 && t.extraObjects.size() == 5);
    if (t.blocks.size() == 1) {
        const Track::Block& b = t.blocks[0];
        CHECK(b.centre[2] == 20.f && b.corners[3][2] == 11.f);
        CHECK(b.vertices.size() == 5 && b.vertices[3][2] == -3.f && b.colours[1] == 0xff102031u);
        CHECK(b.objectVertices == 1 && b.highResVertices == 4);
        CHECK(b.neighbours == (std::vector<int16_t>{0}));
        CHECK(b.startPosition == 7 && b.positions.size() == 8 && b.sounds.size() == 16 && b.lights.size() == 16);
        CHECK(b.polygons[4].size() == 2 && b.polygons[6].size() == 1 && b.polygons[0].empty());
        if (b.polygons[4].size() == 2) {
            CHECK(b.polygons[4][1].vertices == (std::array<uint16_t, 4>{1, 3, 4, 4}));
            CHECK(b.polygons[4][1].texture == 1 && b.polygons[4][1].flags == 0x10 && b.polygons[4][1].extra == 0xf9);
        }
        CHECK(b.objectLayers[0].size() == 2 && b.objectLayers[1].empty());
        if (b.objectLayers[0].size() == 2)
            CHECK(b.objectLayers[0][0].polygons.size() == 1 && b.objectLayers[0][1].type == 4 &&
                  b.objectLayers[0][1].polygons.empty());
    }
    if (t.extraObjects.size() == 5 && t.extraObjects[0].size() == 2) {
        const auto& still = t.extraObjects[0][0];
        CHECK(still.type == 4 && still.reference[2] == 300.f && still.vertices.size() == 3 && still.polygons.size() == 1);
        const auto& moving = t.extraObjects[0][1];
        CHECK(moving.type == 3 && moving.keys.size() == 2 && moving.keys[1].position[0] == 65536 &&
              moving.keys[1].rotation[2] == 100 && moving.vertices.size() == 1);
    }
    if (t.textures.size() == 2) {
        CHECK(t.textures[0].width == 64 && t.textures[0].height == 32 && t.textures[0].corners[7] == 0.875f);
        CHECK(!t.textures[0].lane && t.textures[1].lane && t.textures[1].picture == 41);
    }

    // Damage is caught: a byte too many, a polygon past the vertices, a
    // texture past the table, counts that disagree, an unknown object type.
    Bytes extra = file;
    extra.push_back(0);
    CHECK(parseTrack(extra, t));
    CHECK(parseTrack(Bytes(file.begin(), file.end() - 1), t));
    const size_t polys = 28 + 4 + 84 + 16 * 5 + 1200 + 32 + 8 + 8 + 12 + 16 + 16 + 4 * 4; // lists 0-3: one count each
    Bytes outside = file;
    outside[polys + 8] = 9; // the first road polygon's first vertex
    CHECK(parseTrack(outside, t));
    Bytes badTexture = file;
    badTexture[polys + 8 + 8] = 2;
    CHECK(parseTrack(badTexture, t));
    Bytes disagree = file;
    disagree[polys + 4] = 3;
    CHECK(parseTrack(disagree, t));
}

void testHorizon() {
    // Comments (one inside a line, as the retail files have), one fog
    // region, then the 48 values after the regions.
    std::string text = "/*Fog Color (r,g,b) and Density (%)*/\n134,134,140,\t5,\t70,\n/*regions*/\n1,\n"
                       "10,11,12, 5,\n20,21,22, 55,\n30,31,32, 70,\n150,\n175,\n200,\n";
    const int after[48] = {1, 1, 0, 0, 500, -100, 250, 235, 208, 80, 10, 100, 0, 1, 1500, 0, 1,
                           -1200, 1800, 1200, 1350, 1150, 117, 106, 15, 207, 153, 98, 120, 182, 239,
                           223, 210, 151, 120, 171, 204, 0, 0, 0, 0, 0, 0, 0, 0, 100, 90, 80};
    for (int i = 0; i < 48; ++i) {
        text += std::to_string(after[i]) + ",";
        if (i == 33)
            text += "\t\t/* 255, 176, 61, at position of sun */\n";
    }
    Horizon h;
    CHECK(!parseHorizon(text, h));
    CHECK(h.fogColour == (Horizon::Rgb{134, 134, 140}) && h.fogDensity == 5 && h.fogOnPixmap == 70);
    CHECK(h.fogRegions.size() == 1);
    if (h.fogRegions.size() == 1)
        CHECK(h.fogRegions[0].apex.colour[2] == 22 && h.fogRegions[0].end.density == 70 &&
              h.fogRegions[0].afterSlice == 200);
    CHECK(h.cloudType == 1 && h.visualHeight == 500 && h.cloudDomeOffset == (std::array<int, 2>{-100, 250}));
    CHECK(h.mirror && !h.blackHorizon && h.radius == 1500 && h.hasPixmap);
    CHECK(h.shadedBase == -1200 && h.shadedHeight == 1800 && h.pixmapBottom == 1150);
    CHECK(h.skyBaseSun == (Horizon::Rgb{223, 210, 151}) && h.skyBaseOpposite == (Horizon::Rgb{120, 171, 204}));
    CHECK(h.ambient == (Horizon::Rgb{100, 90, 80}));
    CHECK(parseHorizon(text + "7,", h));                      // a number too many
    CHECK(parseHorizon("1,2,3,4,5,99,", h));                  // regions that aren't there
    CHECK(parseHorizon("/* only a comment */", h));
}

void testCollision() {
    Bytes b = {'C', 'O', 'L', 'L'};
    put32(b, 11);
    put32(b, 0); // size, unchecked
    put32(b, 4);
    const size_t table = b.size();
    b.resize(table + 16, 0);
    auto block = [&](int i, uint16_t id, uint16_t count, const Bytes& body) {
        putU32(b, table + 4 * i, uint32_t(b.size() - table));
        put32(b, uint32_t(8 + body.size()));
        put16(b, id);
        put16(b, count);
        b.insert(b.end(), body.begin(), body.end());
    };
    block(0, 2, 1, Bytes(8, 7));
    Bytes structure;
    put32(structure, 8 + 16 * 3 + 6);
    put16(structure, 3);
    put16(structure, 1);
    for (int v = 0; v < 3; ++v) {
        putF(structure, float(v));
        putF(structure, 0);
        putF(structure, 0);
        put32(structure, 0);
    }
    put16(structure, 5);
    structure.insert(structure.end(), {0, 1, 2, 2});
    block(1, 8, 1, structure);
    Bytes objects;
    put16(objects, 8 + 20);
    objects.push_back(3);
    objects.push_back(0);
    put16(objects, 1);
    put16(objects, 25);
    put32(objects, 874u << 16);
    put32(objects, 52u << 16);
    put32(objects, uint32_t(-(515 << 16)));
    for (int q : {0, 0, 0, 16384})
        put16(objects, unsigned(q));
    block(2, 7, 1, objects);
    Bytes road;
    put32(road, uint32_t(829.5 * 65536));
    put32(road, 13u << 16);
    put32(road, uint32_t(136.25 * 65536));
    put32(road, 0x550ff8);
    road.insert(road.end(), {0, 127, 0, 34, 0x55, 0xff, 0xa3, 16, 0xa2, 0, 0xab, 16});
    put32(road, uint32_t(23.5 * 65536));
    put32(road, 28u << 16);
    block(3, 15, 1, road);

    TrackCollision c;
    CHECK(!parseCollision(b, c));
    CHECK(c.textures.size() == 1 && c.textures[0][7] == 7);
    CHECK(c.structures.size() == 1 && c.structures[0].vertices.size() == 3 && c.structures[0].polygons.size() == 1);
    if (!c.structures.empty() && !c.structures[0].polygons.empty())
        CHECK(c.structures[0].polygons[0].texture == 5 && c.structures[0].polygons[0].vertices[3] == 2);
    CHECK(c.objects.size() == 1);
    if (!c.objects.empty())
        CHECK(c.objects[0].type == 3 && c.objects[0].delay == 25 && c.objects[0].keys.size() == 1 &&
              c.objects[0].keys[0].position[2] == -(515 << 16) && c.objects[0].keys[0].rotation[3] == 16384);
    CHECK(c.road.size() == 1);
    if (!c.road.empty()) {
        const auto& r = c.road[0];
        CHECK(r.position[0] == 829.5f && r.position[2] == 136.25f && r.normal[1] == 1.0f);
        CHECK(r.forward[0] == 85 / 127.0f && r.forward[2] == -93 / 127.0f && r.right[0] == -94 / 127.0f);
        CHECK(r.leftWall == 23.5f && r.rightWall == 28.0f && r.extra[0] == 34 && r.unknown == 0x550ff8);
    }

    Bytes badVertex = b;
    const size_t poly = table + 16 + 16 + 8 + 8 + 16 * 3 + 2; // block 1's polygon's vertices
    badVertex[poly] = 3;
    CHECK(parseCollision(badVertex, c));
    Bytes badStructure = b;
    badStructure[table + 16 + 16 + 8 + 62 + 8 + 3] = 1; // the object's structure: 1 of 1
    CHECK(parseCollision(badStructure, c));
    CHECK(parseCollision(Bytes{'C', 'O', 'L', 'X'}, c));
}

// A PT header: tags (tag, length, big-endian value), then 0xFF.
Bytes ptHeader(std::vector<std::pair<uint8_t, uint32_t>> tags) {
    Bytes b = {'P', 'T', 0, 0, 0x06, 1, 25, 0xfd};
    for (auto [tag, value] : tags) {
        const int n = value > 0xffffff ? 4 : value > 0xffff ? 3 : value > 0xff ? 2 : 1;
        b.push_back(tag);
        b.push_back(uint8_t(n));
        for (int i = n - 1; i >= 0; --i)
            b.push_back(uint8_t(value >> (8 * i)));
    }
    b.push_back(0xff);
    return b;
}

void testSoundBank() {
    // Four slots: PCM, empty (0), EA-XA, and a stale one pointing into the
    // audio; the headers' area ends at 128, the audio follows.
    Bytes b = {'B', 'N', 'K', 'l', 2, 0, 4, 0};
    put32(b, 128);
    b.resize(12 + 16, 0);
    const Bytes pcm = ptHeader({{0x85, 3}, {0x88, 128}, {0x84, 16000}});
    const Bytes xa = ptHeader({{0x83, 7}, {0x85, 30}, {0x88, 134}, {0x86, 2}, {0x87, 20}, {0x92, 1}});
    putU32(b, 12, uint32_t(b.size() - 12));
    b.insert(b.end(), pcm.begin(), pcm.end());
    putU32(b, 20, uint32_t(b.size() - 20));
    b.insert(b.end(), xa.begin(), xa.end());
    putU32(b, 24, uint32_t(140 - 24)); // into the audio: no header there
    b.resize(128, 0);
    for (int16_t v : {100, -200, 300})
        put16(b, uint16_t(v));
    Bytes frames(30, 0);
    frames[0] = 0x0c;
    frames[1] = 0x70; // 7, then zeros
    frames[15] = 0x0c;
    b.insert(b.end(), frames.begin(), frames.end());

    SoundBank bank;
    CHECK(!parseSoundBank(b, bank));
    CHECK(bank.version == 2 && bank.slots.size() == 4);
    if (bank.slots.size() != 4)
        return;
    CHECK(bank.slots[0] && !bank.slots[1] && bank.slots[2] && !bank.slots[3]);
    if (!bank.slots[0] || !bank.slots[2])
        return;
    const SoundHeader& p = *bank.slots[0];
    CHECK(p.compression == SoundHeader::kPcm16 && p.channels == 1 && p.rate == 16000 && p.samples == 3);
    const SoundHeader& x = *bank.slots[2];
    CHECK(x.compression == SoundHeader::kEaXa && x.rate == 22050 && x.samples == 30 && x.dataOffset == 134);
    CHECK(x.loopStart == 2 && x.loopLength == 20 && x.tags.at(0x92) == 1 && x.tags.at(0x06) == 25);

    audio::Sound sound;
    CHECK(!decodeBankSound(b, p, sound));
    CHECK(sound.rate == 16000 && sound.samples == (std::vector<int16_t>{100, -200, 300}));
    CHECK(!decodeBankSound(b, x, sound));
    CHECK(sound.format == audio::kFormatEaXa && sound.samples.size() == 30 && sound.samples[0] == 7);

    // Damage: an unknown compression, data past the end, a cut header.
    SoundHeader odd = x;
    odd.compression = 3;
    CHECK(decodeBankSound(b, odd, sound));
    odd = x;
    odd.dataOffset = uint32_t(b.size() + 1);
    CHECK(decodeBankSound(b, odd, sound));
    Bytes cut(b.begin(), b.begin() + 40);
    putU32(cut, 8, 40);
    CHECK(parseSoundBank(cut, bank));
    CHECK(parseSoundBank(Bytes{'B', 'N', 'K', 'x', 2, 0, 0, 0, 0, 0, 0, 0}, bank));
}

void testMusic() {
    // A stream of two segments: EA-XA stereo (one block of 30 samples:
    // two groups) and PCM stereo (2 samples).
    auto chunk = [](Bytes& out, const char* tag, const Bytes& body) {
        out.insert(out.end(), tag, tag + 4);
        put32(out, uint32_t(8 + body.size()));
        out.insert(out.end(), body.begin(), body.end());
    };
    Bytes mus;
    chunk(mus, "SCHl", ptHeader({{0x82, 2}, {0x83, 7}, {0x85, 30}}));
    Bytes count;
    put32(count, 1);
    chunk(mus, "SCCl", count);
    Bytes xa;
    put32(xa, 30);
    for (int v : {100, 0, -100, 0}) // left previous, before; right previous, before
        put16(xa, unsigned(v));
    xa.push_back(0x10); // filters: left 1, right 0
    xa.push_back(0xcc); // shifts 12, 12: residuals as they are
    xa.push_back(0x1f); // left +1, right -1
    xa.insert(xa.end(), 27, 0);
    Bytes group2(30, 0);
    group2[1] = 0xcc;
    xa.insert(xa.end(), group2.begin(), group2.end());
    chunk(mus, "SCDl", xa);
    chunk(mus, "SCEl", {});
    const uint32_t second = uint32_t(mus.size());
    chunk(mus, "SCHl", ptHeader({{0x82, 2}, {0x85, 2}}));
    chunk(mus, "SCCl", count);
    Bytes pcm;
    put32(pcm, 2);
    for (int v : {1, -1, 2, -2})
        put16(pcm, unsigned(v));
    chunk(mus, "SCDl", pcm);
    chunk(mus, "SCEl", {});

    MusicStream stream;
    CHECK(!stream.open(mus));
    CHECK(stream.segments().size() == 2 && stream.segmentAt(second) == 1 && stream.segmentAt(4) == -1);
    audio::Sound s;
    CHECK(!stream.decode(0, s));
    CHECK(s.channels == 2 && s.samples.size() == 60);
    if (s.samples.size() == 60) {
        // Left: 1 + 240/256 x 100 = 1 + 94; right: -1 + 0.
        CHECK(s.samples[0] == 1 + ((240 * 100 + 128) >> 8) && s.samples[1] == -1);
        CHECK(s.samples[56] == 0 && s.samples[57] == 0); // the second group, filter 0
    }
    CHECK(!stream.decode(1, s));
    CHECK(s.samples == (std::vector<int16_t>{1, -1, 2, -2}));
    Bytes cut(mus.begin(), mus.end() - 3);
    CHECK(stream.open(cut));

    // A map of three sections: 0 goes to 1 (values 0-49) or 2 (50-100),
    // 1 to 0, 2 nowhere; one event; then the offsets, big-endian.
    Bytes map = {'P', 'F', 'D', 'x', 0, 0, 3, 16, 0, 0, 0, 1};
    auto section = [&](uint8_t kind, std::vector<std::array<uint8_t, 3>> routes) {
        Bytes r(28, 0);
        r[0] = kind;
        r[1] = uint8_t(routes.size());
        r[2] = r[3] = 0xff;
        for (size_t k = 0; k < routes.size(); ++k)
            for (int i = 0; i < 3; ++i)
                r[4 + 3 * k + i] = routes[k][i];
        map.insert(map.end(), r.begin(), r.end());
    };
    section(1, {{0, 49, 1}, {50, 100, 2}});
    section(2, {{0, 100, 0}});
    section(3, {});
    map.insert(map.end(), 16, 0);
    for (uint32_t o : {0u, second, 0x01020304u})
        put32be(map, o);
    MusicMap m;
    CHECK(!parseMusicMap(map, m));
    CHECK(m.sections.size() == 3 && m.events.size() == 1 && m.offsets.size() == 3);
    CHECK(m.offsets[1] == second && m.offsets[2] == 0x01020304u);
    CHECK(m.next(0, 10) == 1 && m.next(0, 50) == 2 && m.next(0, 100) == 2 && m.next(1, 70) == 0);
    CHECK(m.next(2, 0) == -1 && m.next(5, 0) == -1 && m.sections[2].kind == 3);
    Bytes badRoute = map;
    badRoute[12 + 6] = 3; // section 0's first route to section 3
    CHECK(parseMusicMap(badRoute, m));
    CHECK(parseMusicMap(Bytes(map.begin(), map.end() - 1), m));
}

void testGameFiles(const stdfs::path& tmp) {
    const stdfs::path install = tmp / "files/install", cd = tmp / "files/cd";
    touch(install / "FeData/text/text.eng", "eng");
    touch(install / "FeData/art/title.qfs", "install's");
    const Bytes viv = makeViv({{"carp.txt", "Serial"}, {"dash.qfs", "dash"}});
    touch(install / "GameData/CarModel/Diab/car.viv", std::string(viv.begin(), viv.end()));
    // The CD has the same title in its own spelling, and the music.
    touch(cd / "fedata/art/TITLE.QFS", "cd's");
    touch(cd / "gamedata/audio/pc/alpirock.mus", "music");
    touch(cd / "broken.viv", "not an archive");

    GameFolders folders;
    folders.install = install;
    folders.cd = cd;
    GameFiles files;
    files.scan(folders);

    auto names = [](const std::vector<FileRef>& refs) {
        std::vector<std::string> out;
        for (const auto& r : refs)
            out.push_back(r.name);
        return out;
    };
    // The install's title, not the CD's; the .viv's contents after it.
    CHECK(names(files.withExtensions({".qfs"})) ==
          (std::vector<std::string>{"FeData/art/title.qfs", "GameData/CarModel/Diab/car.viv/dash.qfs"}));
    CHECK(names(files.withExtensions({".mus", ".txt"})) ==
          (std::vector<std::string>{"GameData/CarModel/Diab/car.viv/carp.txt", "gamedata/audio/pc/alpirock.mus"}));
    CHECK(names(files.textTables()) == (std::vector<std::string>{"FeData/text/text.eng"}));
    CHECK(names(files.withExtensions({".viv"})).size() == 2); // the broken one is listed, with nothing in it

    Bytes data;
    for (const auto& ref : files.withExtensions({".qfs", ".txt"})) {
        CHECK(!files.read(ref, data));
        const std::string s(data.begin(), data.end());
        CHECK(s == "install's" || s == "dash" || s == "Serial");
    }
    FileRef missing{"x", install / "GameData/CarModel/Diab/car.viv", "nothing.bin"};
    CHECK(files.read(missing, data));
}

// Bits as a MAD frame holds them: 16-bit little-endian words, each from its
// top bit.
struct MadBits {
    std::vector<bool> bits;
    void put(uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i)
            bits.push_back((v >> i) & 1);
    }
    void code(const char* s) {
        for (; *s; ++s)
            bits.push_back(*s == '1');
    }
    Bytes bytes() const {
        Bytes out;
        for (size_t w = 0; w * 16 < bits.size(); ++w) {
            unsigned v = 0;
            for (size_t i = w * 16; i < w * 16 + 16; ++i)
                v = v << 1 | (i < bits.size() && bits[i]);
            put16(out, v);
        }
        return out;
    }
};

Bytes madFrame(char kind, const MadBits& bits, int qscale = 1) {
    Bytes f = {'M', 'A', 'D', uint8_t(kind)};
    const Bytes body = bits.bytes();
    put32(f, uint32_t(24 + body.size()));
    put32(f, 0);
    put32(f, 4369067); // 66.67 ms, in 1/65536 ms
    put16(f, 16);
    put16(f, 16);
    f.push_back(0);
    f.push_back(uint8_t(qscale));
    put16(f, 0);
    f.insert(f.end(), body.begin(), body.end());
    return f;
}

void testMovie() {
    // A 16x16 intra frame: luma blocks of 138, 100 and 128, then one whose
    // first horizontal coefficient is escaped (level 100); chroma 128.
    MadBits k;
    for (int dc : {10, -28, 0}) {
        k.put(uint32_t(dc) & 0xff, 8);
        k.code("10"); // end of block
    }
    k.put(0, 8);
    k.code("000001"); // escape
    k.put(100, 10);
    k.put(0, 6); // run 0: the coefficient after the DC term
    k.code("10");
    for (int i = 0; i < 2; ++i) {
        k.put(0, 8);
        k.code("10");
    }
    const Bytes intra = madFrame('k', k);
    MadDecoder d;
    CHECK(!d.decode(intra.data(), intra.size()));
    CHECK(d.width() == 16 && d.height() == 16);
    auto y = [&](int x, int row) { return d.plane(0)[row * d.stride(0) + x]; };
    CHECK(y(0, 0) == 138 && y(7, 7) == 138 && y(8, 0) == 100 && y(0, 8) == 128);
    // Quantiser 2953 x 16 / 1024 = 46: (100 x 46 >> 4) - 1, made odd, is
    // 287; the transform spreads it from +552 to -552 (16ths), across.
    CHECK(y(8, 8) == (2052 + 552) >> 4 && y(15, 15) == (2052 - 552) >> 4);
    CHECK(d.plane(1)[0] == 128 && d.plane(2)[d.stride(2) * 7 + 7] == 128);
    Surface rgb;
    d.toSurface(rgb);
    CHECK(rgb.w == 16 && rgb.row(0)[0] == 138 && rgb.row(0)[1] == 138 && rgb.row(0)[3] == 255);

    // An inter frame copying block 0 only ("01", map 000001), vector 0,0,
    // 6 brighter (motion code +3, doubled); the rest intra: 128, and block 3
    // as before.
    MadBits m;
    m.code("01");
    m.put(1, 6);
    m.code("00");     // vector x 0, y 0
    m.code("100010"); // +3
    for (int i = 1; i < 6; ++i) {
        m.put(0, 8);
        if (i == 3) {
            m.code("000001");
            m.put(100, 10);
            m.put(0, 6);
        }
        m.code("10");
    }
    const Bytes inter = madFrame('m', m);
    CHECK(!d.decode(inter.data(), inter.size()));
    CHECK(y(0, 0) == 144 && y(8, 0) == 128 && y(8, 8) == 162 && y(15, 15) == 93);

    // All blocks copied ("1"), one pixel left (x +1); the last luma block's
    // source would read past the picture, so it stays.
    MadBits all;
    all.code("1");
    all.code("1" "0" "0000"); // x +1
    all.code("0");
    for (int i = 0; i < 6; ++i)
        all.code("0");
    const Bytes shifted = madFrame('e', all);
    CHECK(!d.decode(shifted.data(), shifted.size()));
    CHECK(y(0, 0) == 144 && y(6, 0) == 144 && y(7, 0) == 128 && y(8, 8) == 162);
    CHECK(y(0, 8) == 128 && y(7, 8) == 162); // block 2 takes block 3's first column
    // An 'e' frame isn't kept: the next refers to the one before it.
    CHECK(!d.decode(shifted.data(), shifted.size()));
    CHECK(y(7, 0) == 128 && y(6, 0) == 144);

    Bytes bad = intra;
    bad.resize(30);
    CHECK(d.decode(bad.data(), bad.size()));

    // A movie: frames interleaved with stereo EA-XA audio.
    auto chunk = [](Bytes& out, const char* tag, const Bytes& body) {
        out.insert(out.end(), tag, tag + 4);
        put32(out, uint32_t(8 + body.size()));
        out.insert(out.end(), body.begin(), body.end());
    };
    Bytes mad = intra;
    chunk(mad, "SCHl", ptHeader({{0x82, 2}, {0x83, 7}, {0x85, 28}}));
    Bytes count;
    put32(count, 1);
    chunk(mad, "SCCl", count);
    mad.insert(mad.end(), inter.begin(), inter.end());
    Bytes xa;
    put32(xa, 28);
    xa.insert(xa.end(), 8, 0);
    Bytes group(30, 0);
    group[1] = 0xcc;
    xa.insert(xa.end(), group.begin(), group.end());
    chunk(mad, "SCDl", xa);
    chunk(mad, "SCEl", {});
    MadMovie movie;
    CHECK(!movie.open(mad));
    CHECK(movie.frames().size() == 2 && movie.frames()[0].kind == 'k' && movie.frames()[1].kind == 'm');
    CHECK(movie.width() == 16 && movie.height() == 16 && movie.frameDuration() == 4369067);
    CHECK(movie.hasAudio() && movie.audioHeader().samples == 28);
    audio::Sound sound;
    CHECK(!movie.decodeAudio(sound));
    CHECK(sound.channels == 2 && sound.rate == 22050 && sound.samples.size() == 56);
    MadDecoder fresh;
    CHECK(!movie.decodeFrame(fresh, 0) && !movie.decodeFrame(fresh, 1));
    CHECK(fresh.plane(0)[0] == 144);
    CHECK(movie.decodeFrame(fresh, 2));
    Bytes cut(mad.begin(), mad.end() - 3);
    CHECK(movie.open(cut));
}

} // namespace

int main() {
    const stdfs::path tmp = stdfs::temp_directory_path() / "reluva_nfs_tests";
    stdfs::remove_all(tmp);
    testDetection(tmp);
    testFolders(tmp);
    testListings(tmp);
    testRefPack();
    testViv();
    testShapes();
    testTextTable();
    testFont();
    testGameFiles(tmp);
    testModel();
    testTrack();
    testHorizon();
    testCollision();
    testSoundBank();
    testMusic();
    testMovie();
    stdfs::remove_all(tmp);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
