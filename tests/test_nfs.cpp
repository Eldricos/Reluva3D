// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Need for Speed engine's detection and folder finding,
// against folders built here, so no game data is needed.

#include "engines/nfs/ffn.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/fsh.h"
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
    stdfs::remove_all(tmp);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
