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
#include "engines/nfs/host/force_effects.h"
#include "engines/nfs/host/sound3d.h"
#include "engines/nfs/host/thrash_native.h"
#include "engines/nfs/mad.h"
#include "engines/nfs/mus.h"
#include "engines/nfs/particles.h"
#include "engines/nfs/games.h"
#include "engines/nfs/refpack.h"
#include "engines/nfs/text_table.h"
#include "engines/nfs/viv.h"

#include <algorithm>
#include <cmath>
#include <map>
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
    // EA Huffman (refpack.md), built by hand: A coded 0, B 10, the escape
    // (C) 11; A, B, then the escape repeating B twice, then the end.
    const Bytes huffman = {0x30, 0xfb, 0x00, 0x00, 0x04, 0x43, 0xb8, 0x22, 0xc8, 0xbd, 0xc8};
    CHECK(isEaHuffman(huffman) && !isRefPack(huffman) && !isOtherEaCompression(huffman));
    CHECK(!decompressEaHuffman(huffman, out));
    CHECK(out == (Bytes{'A', 'B', 'B', 'B'}));
    // As differences (32 FB): each byte the sum of those so far.
    Bytes summed = huffman;
    summed[0] = 0x32;
    CHECK(!decompressEaHuffman(summed, out));
    CHECK(out == (Bytes{'A', 0x83, 0xc5, 0x07}));
    Bytes wrong = huffman;
    wrong[4] = 0x05;
    CHECK(decompressEaHuffman(wrong, out)); // not the size it says
    Bytes cutShort(huffman.begin(), huffman.end() - 2);
    CHECK(decompressEaHuffman(cutShort, out));
    const Bytes other = {0x46, 0xfb, 0x00, 0x00, 0x04, 0x00};
    CHECK(isOtherEaCompression(other));
    CHECK(!isOtherEaCompression(small));
    Bytes copy = other;
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

// A cockpit's gauges block (fsh.md, Gauges) and its needles.
void testCockpit() {
    Shape dash;
    Cockpit c;
    CHECK(!readCockpit(dash, c));
    // Colour texel (5, 6), width 4; a speedometer about (100, 100), hub 0,
    // km/h, 0-200, from the left (90, 100) to the right (110, 100); no
    // tachometer.
    const int32_t v[24] = {5, 6, 4, 0, 100, 100, 0, 1, 0, 200, 90, 100, 110, 100};
    Shape::Block b{kGauges, {}};
    put32(b.data, 12);
    for (int32_t x : v)
        put32(b.data, uint32_t(x));
    dash.unknown.push_back(b);
    CHECK(readCockpit(dash, c));
    CHECK(c.colorX == 5 && c.colorY == 6 && c.width == 4);
    CHECK(c.speedometer.shown() && c.speedometer.kmh && c.speedometer.high == 200 && !c.tachometer.shown());
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
    // Half way it points up: the sweep turns clockwise, over the top.
    Needle n = needle(c.speedometer, c.width, 100);
    CHECK(near((n.x[2] + n.x[3]) / 2, 100) && near((n.y[2] + n.y[3]) / 2, 90));
    CHECK(near(n.x[0] - n.x[1], 4) && near(n.x[3] - n.x[2], 2)); // base 4 wide, tip 2
    // At the ends, toward p0 and p1; past them it turns on, as the game's.
    n = needle(c.speedometer, c.width, 0);
    CHECK(near((n.x[2] + n.x[3]) / 2, 90) && near((n.y[2] + n.y[3]) / 2, 100));
    n = needle(c.speedometer, c.width, 200);
    CHECK(near((n.x[2] + n.x[3]) / 2, 110) && near((n.y[2] + n.y[3]) / 2, 100));
    n = needle(c.speedometer, c.width, 300);
    CHECK(near((n.x[2] + n.x[3]) / 2, 100) && near((n.y[2] + n.y[3]) / 2, 110));
    // A range from high to low sweeps the other way, under the bottom.
    Gauge reversed = c.speedometer;
    std::swap(reversed.low, reversed.high);
    n = needle(reversed, c.width, 100);
    CHECK(near((n.x[2] + n.x[3]) / 2, 100) && near((n.y[2] + n.y[3]) / 2, 110));
    // The wheel: a quarter turn at full lock standing still, none at 800/2.2369 m/s.
    CHECK(near(wheelTurns(128, 0), 0.25f) && near(wheelTurns(-64, 0), -0.125f) && near(wheelTurns(128, 800 / 2.2369f / 2), 0));
    CHECK(near(metresPerSecond(c.speedometer, 36), 10));
    // Fewer than 12 pairs: not read.
    dash.unknown[0].data.resize(40);
    CHECK(!readCockpit(dash, c));
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

    CHECK(font.penX == 0 && font.penY == 0 && font.lineStep() == 9);

    // A pen offset of (1, -1) moves everything; the gap adds to a line's step.
    Bytes moved = b;
    moved[16] = 1;
    moved[17] = 0xff;
    moved[19] = 2;
    CHECK(!parseFont(moved, font));
    CHECK(font.penX == 1 && font.penY == -1 && font.lineGap == 2 && font.lineStep() == 11);
    Surface m;
    m.create(6, 4, PixelFormat::RGBA32);
    CHECK(font.draw(m, 0, 3, "AB", {200, 100, 50}) == 5);
    CHECK(m.row(1)[2 * 4] == 200 && m.row(2)[4 * 4] == 200); // 'A' at (2, 1), 'B' at (4, 2)

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
    // The parts each detail draws (0x4bb4c0): a car of 13 parts.
    {
        const CarDetailParts near = carDetailParts(0, 13), mid = carDetailParts(1, 13), small = carDetailParts(2, 13),
                             tiny = carDetailParts(3, 13);
        CHECK(near.body == (std::vector<int>{0, 1, 2, 3, 4}) && near.headlights);
        CHECK(mid.body == (std::vector<int>{5, 6, 7, 8, 9}) && mid.headlights);
        CHECK(small.body == (std::vector<int>{10}) && !small.headlights && tiny.body == (std::vector<int>{11}) && !tiny.headlights);
        // 12 parts: no headlights; traffic and police (6 parts): 0-4 then 5.
        CHECK(!carDetailParts(0, 12).headlights && carDetailParts(0, 12).body.size() == 5);
        CHECK(carDetailParts(2, 6).body == (std::vector<int>{0, 1, 2, 3, 4}) && carDetailParts(3, 6).body == (std::vector<int>{5}));
        CHECK(carDetailParts(-1, 13).body.empty() && carDetailParts(4, 13).body.empty());
        // A two-sided triangle (flag 0x4) both ways round, the rest as stored.
        CHECK(carTriangleCorners(0) == (std::vector<int>{0, 1, 2}) && carTriangleCorners(1) == (std::vector<int>{0, 1, 2}));
        CHECK(carTriangleCorners(5) == (std::vector<int>{0, 1, 2, 0, 2, 1}));
    }
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

// Lights as nfs3.exe's table has them (frd.md, Lights).
// The cars' reflection on the sky's sphere map (fce.md, flags).
void testReflectionUv() {
    auto near = [](std::array<float, 2> a, float u, float v) {
        return std::fabs(a[0] - u) < 1e-4f && std::fabs(a[1] - v) < 1e-4f;
    };
    CHECK(near(reflectionUv(0, 0, 1), 0.5f, 0.5f) && near(reflectionUv(0, 0, -1), 0.5f, 0.5f));
    CHECK(near(reflectionUv(1, 0, 0), 1.0f, 0.5f) && near(reflectionUv(0, -1, 0), 0.5f, 0.0f));
    const float d = std::sqrt(0.5f); // 45 degrees: 0.5 + 0.7071 * 0.7071 / sqrt(2 * 1.7071)
    CHECK(near(reflectionUv(d, 0, d), 0.5f + 0.5f / std::sqrt(2 * (1 + d)), 0.5f));
}

void testPolygonAnimation() {
    Track::Polygon p;
    p.texture = 7;
    CHECK(p.textureAt(1000) == 7); // not animated
    // Animated from entry 7: 10 ticks a frame, 4 frames (84, as on the
    // tracks).
    p.flags = Track::Polygon::kAnimated | Track::Polygon::kTwoSided;
    p.animationBase = 7;
    p.animation = 84;
    CHECK(p.frameDelay() == 10 && p.frameCount() == 4);
    CHECK(p.textureAt(0) == 7 && p.textureAt(9) == 7 && p.textureAt(10) == 8 && p.textureAt(39) == 10 &&
          p.textureAt(40) == 7);
    p.animation = 0xf9; // 31 ticks, 1 frame: still
    CHECK(p.textureAt(0) == 7 && p.textureAt(500) == 7);
}

// Placed sounds as the game plays them (frd.md, Sounds).
void testPlacedSounds() {
    // 2^18 / distance^2, at most 127: full within 45 units.
    CHECK(placedSoundVolume(0) == 127 && placedSoundVolume(40) == 127);
    CHECK(placedSoundVolume(64) == 64 && placedSoundVolume(128) == 16 && placedSoundVolume(256) == 4);
    // A one-shot fires in the eighth of 64 frames picked, then picks again.
    struct Fixed {
        int value;
        int next() { return value; }
    } random{5};
    OneShotTimer t; // picks 0 first
    CHECK(t.fires(64, random) && t.pick == 5);
    CHECK(!t.fires(65, random) && !t.fires(39, random) && t.fires(40, random) && t.pick == 5);
    CHECK(t.fires(47, random) && !t.fires(48, random));
}

void testLightLook() {
    // Dimmed: k = 0.5 * 65536 >> 8 = 128, each byte * k >> 8; 1 and above unchanged.
    CHECK(dimLight(0xffdf1616, 0.5f) == 0x7f6f0b0b && dimLight(0xffdf1616, 1) == 0xffdf1616);
    CHECK(dimLight(0xffffffff, 0.999999f) == 0xfefefefe); // k at most 0xffff >> 8 = 255
    CHECK(lightLook(0, 0).color == 0xffdeeaeb && lightLook(0, 0).size == 5 && lightLook(0, 12345).shown);
    CHECK(lightLook(16 + 7, 0).color == 0xffff0000); // the type is taken & 15
    // Type 5: off while bit 6 of the tick is set, a second on and off.
    CHECK(lightLook(5, 63).shown && !lightLook(5, 64).shown && lightLook(5, 128).shown);
    // Type 6: bit 5, 27 ticks ahead.
    CHECK(lightLook(6, 4).shown && !lightLook(6, 5).shown && lightLook(6, 37).shown);
    CHECK(lightLook(10, 0).color >> 24 == 0xba && lightLook(10, 0).size == 2.5f);
}

void testParticles() {
    // The side codes' kinds (0x553740) and the burst's count (0x4cac20).
    CHECK(wallParticleKind(1) == 3 && wallParticleKind(2) == 3 && wallParticleKind(4) == 3);
    CHECK(wallParticleKind(3) == 2 && wallParticleKind(5) == 2 && wallParticleKind(7) == 2);
    CHECK(wallParticleKind(0) == 0 && wallParticleKind(6) == 0 && wallParticleKind(9) == 0);
    CHECK(burstCount(20) == 30 && burstCount(30) == 30 && burstCount(11) == 16 && burstCount(15) == 22);
    // Sparks: white, to orange and clear (bytes rounded as fistp, halves to even).
    const ParticleTemplate& sparks = *particleTemplate(3);
    CHECK(particleColour(sparks, 0) == 0xffffffff && particleColour(sparks, 1) == 0x00ff3900);
    CHECK(particleColour(sparks, 0.5f) == 0x7fff9c7f);
    CHECK(particleColour(*particleTemplate(2), 1) == 0x00dfdfdf);
    CHECK(!particleTemplate(1) && particleTemplate(6)->life == 256);

    // One dust puff (0x4cae00): random 0.5 throughout, so the template's own
    // speeds; it rises, spins (the sign from the first number's bit 0).
    ParticleSystem sys;
    ParticleRandom random;
    random.script = {1, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    sys.spawnOne(2, {10, 20, 30}, 0, random);
    CHECK(sys.live() == 1);
    const Particle* p = &sys.particles()[0];
    CHECK(p->kind == 2 && p->life == 128 && p->lifeMax == 128 && p->colour == 0x7fdfdfdf);
    CHECK(p->velocity[0] == 0 && std::fabs(p->velocity[1] - 3.1f) < 1e-6f && p->velocity[2] == 0);
    CHECK(p->size == 0.9375f && p->spin == 0.5f && p->growth == 3.0f && p->angle == 0.5f);
    // The throttle: dust every other pair of ticks, smoke one in four; none
    // with Horizon off, half at Low.
    sys.clear();
    CHECK(!sys.spawn(2, {}, 1, {}, {}, {}, 2, random) && sys.spawn(2, {}, 1, {}, {}, {}, 4, random));
    CHECK(sys.spawn(6, {}, 1, {}, {}, {}, 1, random) && !sys.spawn(6, {}, 1, {}, {}, {}, 6, random) &&
          sys.spawn(6, {}, 1, {}, {}, {}, 8, random));
    CHECK(!sys.spawn(3, {}, 1, {}, {}, {}, 0, random, 2) && !sys.spawn(3, {}, 1, {}, {}, {}, 0, random, 1) &&
          sys.spawn(3, {}, 1, {}, {}, {}, 2, random, 1));
    // Nothing moving: not made.
    sys.clear();
    CHECK(sys.spawn(6, {}, 0, {}, {}, {}, 0, random) && sys.live() == 0);

    // Tyre smoke (0x42a480 -> 0x4cb010, kind 6). The skid levels: front x
    // 7.5 and rear as they are, held to 0..10; none in the air or tipped.
    CHECK(wheelSkidLevel(0, 0.5f, 0, false, 1) == 3.75f && wheelSkidLevel(1, 2, 0, false, 1) == 10 &&
          wheelSkidLevel(2, 0, 12, false, 1) == 10 && wheelSkidLevel(3, 0, -1, false, 1) == 0);
    CHECK(wheelSkidLevel(2, 0, 8, true, 1) == 0 && wheelSkidLevel(2, 0, 8, false, 0.85f) == 0);
    // Only rear wheels over 5, on the dry surfaces marked for skid marks
    // (0x553090: 1, 4, 7, 10, 11, 12), none in rain or snow.
    CHECK(wheelSmokes(2, 10, 1, 0) && wheelSmokes(3, 5.5f, 12, 0) && !wheelSmokes(3, 5, 1, 0));
    CHECK(!wheelSmokes(0, 10, 1, 0) && !wheelSmokes(2, 10, 2, 0) && !wheelSmokes(2, 10, 1, 1) && !wheelSmokes(2, 10, 14, 0));
    CHECK(skidMarkSurface(4, 0) && skidMarkSurface(0x41, 0) && !skidMarkSurface(5, 0) && !skidMarkSurface(1, 2));
    // A left rear wheel's puff: along the velocity made a unit vector, out
    // to the left (against the right row), half the velocity inherited
    // (x 0.5), spinning the other way.
    sys.clear();
    random.script = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    tyreSmoke(sys, 3, {1303, 0, 950}, {4, 0, 0}, {0, 0, 1}, 0, random);
    CHECK(sys.live() == 1);
    {
        const Particle& q = sys.particles()[0];
        CHECK(q.kind == 6 && q.life == 256 && q.position[0] == 1303 && q.colour == 0x7fffffff);
        CHECK(std::fabs(q.velocity[0] - 1.5f) < 1e-6f && std::fabs(q.velocity[1] - 1.5f) < 1e-6f &&
              std::fabs(q.velocity[2] + 0.5f) < 1e-6f && std::fabs(q.spin + 0.1f) < 1e-6f);
    }
    // A right one goes out to the right; a held car (no velocity) still
    // gets the up and across speeds.
    sys.clear();
    random.script = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    tyreSmoke(sys, 2, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}, 8, random);
    CHECK(sys.live() == 1 && sys.particles()[0].velocity[2] == 0.5f && sys.particles()[0].velocity[0] == 0 &&
          std::fabs(sys.particles()[0].spin - 0.1f) < 1e-6f);
    // Its throttle (one in four pairs of ticks): nothing at tick 2.
    sys.clear();
    tyreSmoke(sys, 2, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 2, random);
    CHECK(sys.live() == 0);

    // A spark: along × a + across × b + up, then falling and moving 1/64 of
    // its velocity a game tick, the rest every other tick.
    sys.clear();
    random.script = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    Particle* s = sys.spawn(3, {0, 0, 0}, 1, {0, 0, 1}, {1, 0, 0}, {0, 0, 0}, 0, random);
    CHECK(s && s->velocity[0] == 2 && s->velocity[1] == 2 && s->velocity[2] == 5);
    sys.step(0, random); // even: moves only
    CHECK(s->life == 16 && s->position[2] == 5 / 64.0f && s->velocity[1] == 2);
    sys.step(1, random);
    CHECK(s->life == 15 && s->velocity[1] == 1.75f && s->colour == 0xffffffff);
    sys.step(2, random);
    sys.step(3, random); // odd with bit 1: the colour, at 1/16 of its life
    CHECK(s->life == 14 && s->colour == particleColour(sparks, 1 / 16.0f));
    // Its end: two more sparks of half the life from where it was.
    s->children = 2;
    random.script.clear();
    for (uint32_t tick = 4; sys.particles()[0].children != 0 && tick < 64; ++tick)
        sys.step(tick, random);
    // The second, in a later slot, is stepped in the same pass (as the game).
    CHECK(sys.particles()[0].life == 8 && sys.particles()[1].life == 7 && sys.live() == 2);
    CHECK(sys.particles()[0].children == 0 && sys.particles()[0].lifeMax == 16);

    // When all 600 are taken, the one with the least life goes.
    sys.clear();
    for (int i = 0; i < ParticleSystem::kSlots; ++i)
        sys.particles()[size_t(i)].life = 100 + (i == 321 ? -50 : i);
    CHECK(&sys.slot() == &sys.particles()[321]);

    // Wall hits (0x42ac40): fast on a sparks code, one spark and a burst of
    // 30; fast on dust, two puffs; slow, the code's particle and smoke.
    auto hitCount = [&](int code, float speed, float rough = 0) {
        ParticleSystem hits;
        WallHit hit;
        hit.code = code;
        hit.speed = speed;
        hit.impact = 10;
        hit.rough = rough;
        hit.contact = {1, 0, 0};
        hit.carVelocity = {0, 0, speed};
        wallHitParticles(hits, hit, 0, random);
        return hits.live();
    };
    CHECK(hitCount(4, 40) == 31 && hitCount(1, 22) == 1 + 16 && hitCount(5, 40) == 2);
    CHECK(hitCount(5, 18) == 2 && hitCount(0, 18) == 1 && hitCount(0, 40) == 0 && hitCount(4, 10) == 1);
    CHECK(hitCount(4, 40, 3) == 2);
    // The burst's sparks live 9-16 ticks and may each leave 0-2 more.
    {
        ParticleSystem hits;
        hits.burst({0, 0, 0}, {-1, 0, 0}, {0, 0, 40}, 20, 0, random);
        bool ok = hits.live() == 30;
        for (const Particle& q : hits.particles())
            if (q.life)
                ok = ok && q.life >= 9 && q.life <= 16 && q.children >= 0 && q.children <= 2 && q.velocity[2] > 10;
        CHECK(ok);
    }

    // Drawing: a square turned by the angle, its corners `half` away; a
    // streak from where the spark was drawn, the tail then halfway.
    Particle d;
    d.position = {5, 5, 5};
    const ParticleVec right{1, 0, 0}, up{0, 1, 0};
    auto corners = pictureCorners(d, right, up, 2);
    CHECK(corners[0] == (ParticleVec{5, 3, 5}) && corners[1] == (ParticleVec{7, 5, 5}) &&
          corners[2] == (ParticleVec{5, 7, 5}) && corners[3] == (ParticleVec{3, 5, 5}));
    corners = streakCorners(d, right, up, 0.5f, 0.1f); // held to half a pixel
    CHECK(d.tailSet && d.tail == d.position && corners[0] == d.position && corners[2] == (ParticleVec{5.25f, 4.75f, 5}));
    d.position = {9, 5, 5};
    corners = streakCorners(d, right, up, 0.5f, 3); // held to a pixel
    CHECK(corners[0] == (ParticleVec{5, 5, 5}) && corners[1] == corners[0] && corners[2] == (ParticleVec{9.5f, 4.5f, 5}) &&
          corners[3] == (ParticleVec{8.5f, 5.5f, 5}) && d.tail == (ParticleVec{7, 5, 5}));
}

void testViewAndRoadFrame() {
    // The screen (0x4dbce0, 0x4bf260): 90 degrees across, 36 up for each
    // half, at 640 x 480: 320 and -330.33; 75 degrees: 37 and 30.
    const ViewScreen s = viewScreen(0, 0, 640, 480);
    CHECK(std::fabs(s.sx - 320) < 1e-3f && std::fabs(s.sy + 330.332f) < 1e-2f && s.cx == 320 && s.cy == 240);
    CHECK(s.right == 640 && s.bottom == 480);
    const ViewScreen narrow = viewScreen(0, 0, 640, 480, 75, true);
    CHECK(std::fabs(narrow.sx + 320 / std::tan(37 * 0.0174532925f)) < 1e-2f);
    CHECK(std::fabs(narrow.sy + 240 / std::tan(30 * 0.0174532925f)) < 1e-2f);

    // The road's frame on a polygon: the virtual road vector its record's
    // byte 0 picks (up, forward as s16 / 32768), right up x forward, the
    // point its vertex 1.
    Track::Block b;
    b.vertices = {{0, 5, 0}, {10, 5, 0}, {10, 5, 10}, {0, 5, 10}};
    Track::Polygon p;
    p.vertices = {0, 1, 2, 3};
    b.polygons[4] = {p};
    b.polygonRoad = {1, 0, 0, 0, 0, 0, 0, 0};
    auto put = [&](int16_t v) {
        b.virtualRoad.push_back(static_cast<uint8_t>(v));
        b.virtualRoad.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    };
    for (int16_t v : {0, 0, 0, 0, 0, 0}) // entry 0, unused
        put(v);
    // Entry 1: tilted up across (a camber of 0.1 / 1), forward along z.
    const float tilt = 0.1f / std::sqrt(1.01f);
    for (int16_t v : {static_cast<int16_t>(-tilt * 32768), static_cast<int16_t>(32768 / std::sqrt(1.01f) - 1),
                      int16_t(0), int16_t(0), int16_t(0), int16_t(32767)})
        put(v);
    RoadFrame f;
    CHECK(polygonRoadFrame(b, 0, f));
    CHECK(f.point == (std::array<float, 3>{10, 5, 0}));
    CHECK(std::fabs(f.right[0] - 0.995f) < 1e-3f && std::fabs(f.right[1] - 0.0995f) < 1e-3f);
    // On the plane: 5 m to the right of the point it's 0.5 up; a point
    // above it is taken along the up (so a little to the left), not down.
    CHECK(std::fabs(roadFrameHeight(f, {15, 5.5f, 0}) - 5.5f) < 1e-3f);
    const float above = roadFrameHeight(f, {15, 7.5f, 0});
    CHECK(above > 5.5f && above < 5.7f);
    CHECK(std::fabs(roadFrameHeight(f, {10, 5, 30}) - 5) < 1e-3f); // along the forward
    b.virtualRoad.resize(12);
    CHECK(!polygonRoadFrame(b, 0, f));
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
            CHECK(b.polygons[4][1].texture == 1 && b.polygons[4][1].flags == 0x10 && b.polygons[4][1].animation == 0xf9);
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

    // The order the game draws a polygon's vertices in (0x41b300, 0x41b640):
    // the file's for the block's lists, 1, 0, 3, 2 for placed objects.
    for (int k = 0; k < 4; ++k)
        CHECK(polygonSlot(k, false) == k);
    CHECK(polygonSlot(0, true) == 1 && polygonSlot(1, true) == 0 && polygonSlot(2, true) == 3 &&
          polygonSlot(3, true) == 2);
    // Hometown's first 50 MPH sign (a placed object, one-sided, texture
    // corners (0, 0), (1, 0), (1, 1), (0, 1)), on the left of the road
    // where the race heads to -x, -z: drawn in the game's order its front,
    // (b - a) x (c - a) as for the Taco Den sign's block polygon that reads
    // the right way round from the start, faces the oncoming traffic
    // (-x, -z), and corner 0 (u = 0) is vertex 1.
    const Track::Vec3 sign[4] = {{884.5f, 25.6f, -123.3f}, {883.1f, 25.6f, -122.2f}, {883.1f, 23.2f, -122.2f},
                                 {884.5f, 23.2f, -123.3f}};
    const Track::Vec3& a = sign[polygonSlot(0, true)];
    const Track::Vec3& b = sign[polygonSlot(1, true)];
    const Track::Vec3& c = sign[polygonSlot(2, true)];
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float nx = e1[1] * e2[2] - e1[2] * e2[1], nz = e1[0] * e2[1] - e1[1] * e2[0];
    CHECK(nx < 0 && nz < 0 && &a == &sign[1]);

    // The blocks drawn (0x41cdc0, 0x41d620): within 500 by day, high detail
    // within 110 and medium within 150; by night all × 0.75; the lower view
    // levels × 0.55 and 0.4 (night 0.41, 0.3); view level 0, all high.
    CHECK(blockDetailAt(0, false) == 2 && blockDetailAt(110, false) == 2 && blockDetailAt(111, false) == 1);
    CHECK(blockDetailAt(150, false) == 1 && blockDetailAt(151, false) == 0 && blockDetailAt(500, false) == 0);
    CHECK(blockDetailAt(501, false) == -1 && blockDetailAt(376, true) == -1 && blockDetailAt(374, true) == 0);
    CHECK(blockDetailAt(83, true) == 1 && blockDetailAt(82, true) == 2);
    CHECK(blockDetailAt(276, false, 2) == -1 && blockDetailAt(274, false, 2) == 0 && blockDetailAt(201, false, 3) == -1);
    CHECK(blockDetailAt(499, false, 0) == 2 && blockDetailAt(501, false, 0) == -1);
}

void testHeadlights() {
    // The cut-off of far polygons (0x41b3bf): 500 ahead, day or night.
    CHECK(polygonInReach(500) && !polygonInReach(500.5f) && polygonInReach(-20));
    CHECK(scaleArgb(0xFF804020u, 128) == 0x7F402010u && scaleArgb(0xB0B0B0B0u, 255) == 0xAFAFAFAFu);

    // Low beams, projected (0x42d253): 10 to 40 in 0xB0B0B0B0, tilted down
    // 1/16 of a turn; heading 0 looks along +z, its right +x.
    const float kTilt = 0.39269908f; // 22.5 degrees
    const Headlight low = carHeadlight(1, true, {0, 1, 0}, 0);
    CHECK(low.fadeFrom == 10 && low.reach == 40 && low.colour == 0xB0B0B0B0u && low.nearPlane == 1);
    CHECK(std::fabs(low.forward[1] + std::sin(kTilt)) < 1e-5f && std::fabs(low.forward[2] - std::cos(kTilt)) < 1e-5f);
    CHECK(std::fabs(low.up[1] - std::cos(kTilt)) < 1e-5f && std::fabs(low.up[2] - std::sin(kTilt)) < 1e-5f);
    CHECK(low.right[0] == 1 && low.right[2] == 0);
    auto along = [&](const Headlight& h, float z, float x = 0) {
        return std::array<float, 3>{h.at[0] + h.forward[0] * z + h.right[0] * x, h.at[1] + h.forward[1] * z,
                                    h.at[2] + h.forward[2] * z + h.right[2] * x};
    };
    // On the axis: the picture's middle; full colour before the fade, two
    // thirds at 20 (0x41a3e0), nothing past the reach or behind.
    HeadlightPoint p = headlightAt(low, along(low, 5));
    CHECK(std::fabs(p.u - 0.5f) < 1e-4f && std::fabs(p.v - 0.5f) < 1e-4f && p.outside == 0 && p.colour == 0xAFAFAFAFu);
    p = headlightAt(low, along(low, 20));
    CHECK(p.colour == 0x74747474u && p.outside == 0);
    p = headlightAt(low, along(low, 45));
    CHECK(p.colour == 0 && (p.outside & 0x20));
    p = headlightAt(low, along(low, -3));
    CHECK(p.colour == 0 && p.outside == 0x10);
    p = headlightAt(low, along(low, 0.5f));
    CHECK((p.outside & 0x10) && p.colour != 0);
    // On the frustum's right edge (x = z): u 1, half the colour; past it,
    // outcode 2, the colour as at the edge.
    p = headlightAt(low, along(low, 5, 5));
    CHECK(std::fabs(p.u - 1) < 1e-4f && p.colour == 0x58585858u && p.outside == 0);
    const HeadlightPoint right = headlightAt(low, along(low, 5, 7));
    CHECK(right.outside == 2 && right.colour == 0x58585858u);
    // Triangles: drawn unless all outside one side, one past the reach, or
    // all nearly transparent.
    const HeadlightPoint a = headlightAt(low, along(low, 5)), b = headlightAt(low, along(low, 10, 2)),
                         c = headlightAt(low, along(low, 12, -2)), far = headlightAt(low, along(low, 41));
    CHECK(headlightTriangle(a, b, c) && !headlightTriangle(a, b, far) && !headlightTriangle(right, right, right));
    CHECK(headlightTriangle(a, b, right));
    // High beams, and without projection; a later car's dimmer reach.
    const Headlight high = carHeadlight(2, true, {0, 0, 0}, 0);
    CHECK(high.fadeFrom == 20 && high.reach == 60 && high.colour == 0xE0E0E0E0u);
    CHECK(std::fabs(high.forward[1] + std::sin(kTilt / 2)) < 1e-5f);
    const Headlight plain = carHeadlight(1, false, {0, 0, 0}, 0);
    CHECK(plain.fadeFrom == 20 && plain.reach == 40 && plain.colour == 0xFFFFFFFFu);
    const Headlight dim = carHeadlight(1, true, {0, 0, 0}, 0, 0.25f);
    CHECK(dim.fadeFrom == 2.5f && dim.reach == 10);
    // Each further car the pass lights for a block gets less (0x41c670):
    // 1 - lit / cars x 0.75.
    CHECK(headlightIntensity(0, 8) == 1 && headlightIntensity(4, 8) == 0.625f && headlightIntensity(8, 8) == 0.25f);
    CHECK(headlightIntensity(0, 0) == 1);
    const Headlight east = carHeadlight(1, true, {0, 0, 0}, 1.5707963f);
    CHECK(std::fabs(east.right[2] + 1) < 1e-5f && std::fabs(east.forward[0] - std::cos(kTilt)) < 1e-5f);
    // The light's place (0x42d160): up by +0x10C, a quarter of the half
    // length along the light's forward (tilted down).
    const std::array<float, 3> at = headlightPlace(low, {10, 2, 30}, 2.4f);
    CHECK(std::fabs(at[0] - 10) < 1e-5f && std::fabs(at[2] - (30 + 0.6f * std::cos(kTilt))) < 1e-5f);
    CHECK(std::fabs(at[1] - (3 - 0.6f * std::sin(kTilt))) < 1e-5f);
    // With the car's rows: the same light for an upright car; on a banked
    // car the place goes up its own up row.
    const Headlight rows = carHeadlight(1, true, {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    CHECK(std::fabs(rows.forward[1] - low.forward[1]) < 1e-6f && rows.reach == low.reach && rows.backColour == 0);
    const std::array<float, 3> banked = headlightPlace(low, {0, 0, 0}, {0.6f, 0.8f, 0}, 2.4f);
    CHECK(std::fabs(banked[0] - 0.6f) < 1e-5f && std::fabs(banked[1] - (0.8f - 0.6f * std::sin(kTilt))) < 1e-5f);

    // The lights a race starts with (0x42bcb0): a human's high beams by
    // night, the computer's low; low for all in the weather by day.
    CHECK(startingBeams(true, false, true) == 2 && startingBeams(true, false, false) == 1);
    CHECK(startingBeams(false, true, true) == 1 && startingBeams(false, false, false) == 0);
    CHECK(startingBeams(true, true, false) == 1);

    // A roof light (0x42d020): a unit up, turned a step's 1/32 of a turn
    // about the up; blue ahead, red behind (0x41a550 negates the point).
    const Headlight roof = sirenLight({5, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 8); // a quarter turn
    CHECK(roof.at[1] == 1 && roof.nearPlane == 3.5f && roof.fadeFrom == 15 && roof.reach == 30);
    CHECK(std::fabs(roof.forward[0] - 1) < 1e-5f && std::fabs(roof.right[2] + 1) < 1e-5f);
    CHECK(roof.colour == 0xFF8080FFu && roof.backColour == 0xFFFF8080u);
    p = headlightAt(roof, {15, 1, 0}); // 10 ahead
    CHECK(p.outside == 0 && p.colour == 0xFE7F7FFEu);
    p = headlightAt(roof, {-5, 1, 0}); // 10 behind: red
    CHECK(p.outside == 0 && p.colour == 0xFEFE7F7Fu);
    p = headlightAt(roof, {7, 1, 0}); // 2 ahead: within the near plane
    CHECK((p.outside & 0x10) != 0);
    CHECK(sirenLight({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 40).forward[0] ==
          sirenLight({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 8).forward[0]);

    // The block test (0x41bf30): a block ahead is kept; one whose corners
    // are all wider than deep and off one side is left out; one all
    // behind too, unless the light shines both ways (0x41bc90).
    const Headlight flat = carHeadlight(1, true, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    auto square = [](float x, float z, float half) {
        return std::array<std::array<float, 3>, 4>{std::array<float, 3>{x - half, 0, z - half}, {x + half, 0, z - half},
                                                   {x + half, 0, z + half}, {x - half, 0, z + half}};
    };
    CHECK(!lightMissesBlock(flat, square(0, 30, 10)));
    CHECK(lightMissesBlock(flat, square(60, 20, 10)) && lightMissesBlock(flat, square(-60, 20, 10)));
    CHECK(!lightMissesBlock(flat, square(30, 20, 15))); // one corner within
    CHECK(lightMissesBlock(flat, square(0, -40, 10)));
    CHECK(!lightMissesBlock(roof, square(-20, 0, 5), 1, true) && lightMissesBlock(roof, square(0, 60, 5), 1, true));
    // Straddling both sides: no outcode shared, so kept.
    CHECK(!lightMissesBlock(flat, {std::array<float, 3>{-40, 0, 5}, {40, 0, 5}, {40, 0, 6}, {-40, 0, 6}}));

    // Other cars' lights on a car (0x4b97b0): (1 - |x| / z)(1 - z / reach)
    // of a headlight's colour, the lights' mean added.
    CarLights behind;
    behind.headlight = carHeadlight(2, false, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    behind.headlightOn = true;
    const std::array<float, 3> ahead{0, -0.8f, 30}; // in the beam's frame roughly 30 along
    const uint32_t lit = carLitBy(0xff101010u, ahead, {behind});
    CHECK(lit == 0xff919191u); // 0x10 + 255 x (1 - 29.6 / 60)
    CHECK(carLitBy(0xff101010u, {0, 0, -5}, {behind}) == 0xff101010u); // behind it
    CarLights cop;
    cop.siren = roof;
    cop.sirenOn = true;
    cop.headlight = behind.headlight;
    cop.headlightOn = true;
    // In the roof light's beam (10 ahead of it): 1 - 10/30 of its blue,
    // and then the headlights' share too; out of it, not even those.
    const uint32_t blue = carLitBy(0xff000000u, {15, 1, 0}, {cop});
    CHECK((blue & 0xff) > ((blue >> 16) & 0xff));
    CHECK(carLitBy(0xff000000u, {5, 1, 30}, {cop}) == 0xff000000u);

    // The track's lights on a near car (0x4b9550): 0x496550's distance on
    // 16.16 integers (the largest of |dx|, |dy| plus a quarter of the
    // other, then the same with |dz|); within 16, the colour x (1 - d / 16),
    // the mean of the lights in the seven slices' blocks added.
    CHECK(lightDistance({0, 0, 0}, {0x40000, 0x10000, 0x20000}) == 0x40000 + 0x4000 + 0x8000);
    CHECK(lightDistance({0, 0, 0}, {0x10000, 0, 0x80000}) == 0x80000 + 0x4000);
    Track lights;
    lights.blocks.resize(3);
    Track::Block::Light red;
    red.position = {0, 0, 8}; // 8 from the car: half its colour
    red.type = 9;             // 0xffdf1616, steady
    lights.blocks[1].lightList.push_back(red);
    // Slice 12 (block 1): slices 9-15 are all in block 1, so the light
    // counts seven times, its mean the same.
    CHECK(carLitByTrackLights(0xff101010u, {0, 0, 0}, 12, 24, lights, 0, false) == 0xff7f1b1bu);
    CHECK(carLitByTrackLights(0xff101010u, {0, 0, 0}, 12, 24, lights, 0, true) == 0xff101010u); // effects off
    CHECK(carLitByTrackLights(0xff101010u, {0, 0, 24}, 12, 24, lights, 0, false) == 0xff101010u); // 16 away
    Track::Block::Light white = red;
    white.type = 2; // 0xffffffd2
    white.position = {0, 0, -8};
    lights.blocks[0].lightList.push_back(white);
    // Slice 8: slices 5-7 in block 0 (the white light, 3 times) and 8-11
    // in block 1 (the red, 4 times): (3 x 127 + 4 x 111) / 7 red.
    const uint32_t both = carLitByTrackLights(0xff000000u, {0, 0, 0}, 8, 24, lights, 0, false);
    CHECK(((both >> 16) & 0xff) == (3 * 127 + 4 * 111) / 7 && ((both >> 8) & 0xff) == (3 * 127 + 4 * 11) / 7);
    Track::Block::Light blink = red;
    blink.type = 7; // blinks on bit 6 of the step
    lights.blocks[2].lightList.push_back(blink);
    CHECK(carLitByTrackLights(0xff000000u, {0, 0, 0}, 20, 24, lights, 0, false) != 0xff000000u &&
          carLitByTrackLights(0xff000000u, {0, 0, 0}, 20, 24, lights, 64, false) == 0xff000000u);
    // The detail a car is drawn at (0x4bb5d0): the nearest only with the
    // car detail option 0, its distances x 0.75 by night.
    CHECK(carDetailLevel(25, 0, false) == 0 && carDetailLevel(25, 0, true) == 1 && carDetailLevel(25, 1, false) == 1 &&
          carDetailLevel(10, 2, false) == 1 && carDetailLevel(250, 0, true) == 3 && carDetailLevel(301, 0, false) == -1);

    // The speed tables: a byte per two slices after 16, 64 without one.
    std::vector<uint8_t> speeds(16, 0);
    speeds.push_back(20);
    speeds.push_back(35);
    CHECK(aiSlicesSpeed(speeds, 1) == 20 && aiSlicesSpeed(speeds, 2) == 35 && aiSlicesSpeed(speeds, 9) == 64);
    CHECK(aiSlicesSpeed({}, 0) == 64);
}

void testCarLighting() {
    // 0x49d8f0: base a third of red and green, half of blue; top twice,
    // at most 255. 0x49da30: base + (top - base) x n . L when lit.
    const CarShade s = carShade(0xff906030u);
    CHECK(s.base[0] == 48 && s.base[1] == 32 && s.base[2] == 24);
    CHECK(s.top[0] == 255 && s.top[1] == 192 && s.top[2] == 96);
    CHECK(carVertexColour(s, -0.5f) == 0xff302018u && carVertexColour(s, 0) == 0xff302018u);
    CHECK(carVertexColour(s, 1) == 0xffffc060u);
    CHECK(carVertexColour(s, 0.5f) == ((0xffu << 24) | (151u << 16) | (112u << 8) | 60u));
    // The light's direction from the horizon's rotation (0x478340).
    const auto north = carLightDirection(0), east = carLightDirection(90);
    CHECK(std::fabs(north[2] - 2 / std::sqrt(5.0f)) < 1e-5f && std::fabs(north[1] - 1 / std::sqrt(5.0f)) < 1e-5f);
    CHECK(std::fabs(east[0] - 2 / std::sqrt(5.0f)) < 1e-4f && std::fabs(east[2]) < 1e-4f);
    // The shading under the car (0x4206f0) and the ambient light (0x419660).
    CHECK(carShadeColour({0xff102030u, 0xff102030u, 0xff203040u, 0xff203041u}) == 0xff182838u);
    CHECK(ambientScaled(0x80ff8040u, {50, 100, 33}) == 0x807f8015u);
    // The glows' brightness (0x4b87a0): 1 - 0.3 x sum / 765, 0.7 at least.
    CHECK(carGlowBrightness(0xff000000u) == 1 && carGlowBrightness(0xffffffffu) == 0.7f);
    CHECK(std::fabs(carGlowBrightness(0xff7f7f7fu) - (1 - 0.3f * 381 / 765)) < 1e-5f);

    // The glows (0x4b8740). A tail light seen from behind: red, 128 or
    // 198 braking; only with the lights on or braking.
    CarGlow g = carLightGlow("TRLN", -1, 1, false, false, 0, 1);
    CHECK(g.shown && g.colour == 0x80ff0000u && std::fabs(g.size - 0.8f) < 1e-6f && g.nearer == 0);
    CHECK(carLightGlow("TRLN", -1, 0, true, false, 0, 1).colour == 0xc6ff0000u);
    CHECK(!carLightGlow("TRLN", -1, 0, false, false, 0, 1).shown && !carLightGlow("TRLN", 1, 1, false, false, 0, 1).shown);
    // Seen from the side: dimmer under a quarter, the size kept at 0.25 of it.
    g = carLightGlow("TRRN", -0.1f, 1, false, false, 0, 0.7f);
    CHECK(g.colour == ((uint32_t(std::lrint(128 * 0.4f * 0.7f)) << 24) | 0xff0000u));
    CHECK(std::fabs(g.size - 0.25f * 0.8f * 0.7f) < 1e-6f);
    // A headlight from ahead: white, half as large again on high beams.
    g = carLightGlow("HFLO", 1, 2, false, false, 0, 1);
    CHECK(g.shown && g.colour == 0xffffffffu && std::fabs(g.size - 1.2f) < 1e-6f);
    // A police car's: flashing in turn with the siren on, lights off.
    CHECK(carLightGlow("HFLO", 1, 0, false, true, 4, 1).shown && !carLightGlow("HFRE", 1, 0, false, true, 4, 1).shown);
    CHECK(!carLightGlow("HFLO", 1, 0, false, true, 20, 1).shown && carLightGlow("HFRE", 1, 0, false, true, 20, 1).shown);
    // The roof lights: blue left, red right, rising and falling in turn.
    g = carLightGlow("SMLN", 0, 0, false, true, 8, 1); // phase 1/4: share 1/2
    CHECK(g.shown && g.colour == 0x808080ffu && std::fabs(g.size - 0.75f) < 1e-6f && g.nearer == 0.75f);
    g = carLightGlow("SMRN", 0, 0, false, true, 16, 1); // share 1: red gone
    CHECK(g.colour == 0x00ff8080u && std::fabs(g.size - 0.375f) < 1e-6f);
    CHECK(!carLightGlow("SMLN", 0, 1, false, false, 8, 1).shown);
}

void testCarReflection() {
    // 0x49c880 and 0x49ca50: integer HSV, the hue a turn in 256.
    CHECK((rgbToHsv({255, 0, 0}) == std::array<int, 3>{0, 255, 255}));
    CHECK((rgbToHsv({0, 255, 0}) == std::array<int, 3>{85, 255, 255}));
    CHECK((rgbToHsv({128, 128, 128}) == std::array<int, 3>{0, 0, 128}));
    CHECK((rgbToHsv({100, 150, 200}) == std::array<int, 3>{149, 128, 200}));
    CHECK((hsvToRgb({0, 255, 255}) == std::array<int, 3>{255, 0, 0}));
    CHECK((hsvToRgb({85, 255, 255}) == std::array<int, 3>{2, 255, 0}));
    CHECK((hsvToRgb({40, 0, 90}) == std::array<int, 3>{90, 90, 90}) && (hsvToRgb({40, 200, 0}) == std::array<int, 3>{0, 0, 0}));
    // 0x4ba350: the sky's hue; value 128 + 200 x 127 / 512 = 177, less a
    // quarter of the paint's value (112) for the normal one; all x 255 / 256.
    CarReflection r = carReflection(0xff7f7f7fu, {100, 150, 200}, 112, 0);
    CHECK((r.normal >> 24) == 149 * 255 / 256 && (r.strong >> 24) == 177 * 255 / 256);
    CHECK((r.normal & 0xff) > ((r.normal >> 16) & 0xff) && (r.strong & 0xff) > ((r.strong >> 16) & 0xff));
    // The strong one keeps half the sky's saturation, the normal 96 / 256.
    CHECK((r.strong & 0xff) - ((r.strong >> 16) & 0xff) > (r.normal & 0xff) - ((r.normal >> 16) & 0xff));
    // Deep under cover the sky is the car's own grey, both dimmed to half.
    r = carReflection(0xff7f7f7fu, {100, 150, 200}, 0, 8);
    CHECK((r.strong >> 24) == (128 + 127 * 127 / 512) * 128 / 256 && ((r.strong >> 16) & 0xff) == (r.strong & 0xff));
    // The frame (0x49d780): from a camera 5 ahead along z, x is the
    // track's -x and z points from the camera to the car.
    auto f = reflectionFrame({0, 0, 5});
    CHECK(std::fabs(f[0][0] + 1) < 1e-5f && std::fabs(f[1][1] - 1) < 1e-5f && std::fabs(f[2][2] + 1) < 1e-5f);
    f = reflectionFrame({0, 5, 0}); // from straight above
    CHECK(std::fabs(f[2][1] + 1) < 1e-5f);
}

void testLightEffects() {
    // 0x491190: glw0 fading with the depth, glw3 at full colour; none
    // nearer than 1 or past 256, nor blinking off.
    const LightLook white{0xffffffffu, 4, true};
    CHECK(!lightQuads(white, 0.5f).shown && !lightQuads(white, 300).shown && !lightQuads({0xffffffffu, 4, false}, 10).shown);
    LightQuads q = lightQuads(white, 1);
    CHECK(q.shown && q.glowColour == 0xfefefefeu && q.flareColour == 0xffffffffu);
    CHECK(std::fabs(q.screenHalf - 0.1f) < 1e-6f && std::fabs(q.worldHalf - 1.05f) < 1e-6f);
    CHECK(lightQuads(white, 128).glowColour == 0x7f7f7f7fu && lightQuads(white, 256).glowColour == 0);
    // 0x491bc0: a headlight 10 ahead facing the camera: 1 either side,
    // 7.5 long towards the camera, 166 / 256 of the glow's colour.
    RainFlare f = rainFlare({0, 1, 10}, {0, 2, 0}, {0, 0, -1}, 2, 0xffffffffu);
    CHECK(f.shown && f.colour == 0xa5a5a5a5u && std::fabs(f.depth - 10) < 1e-5f);
    CHECK(std::fabs(f.corners[0][0] - 1) < 1e-5f && std::fabs(f.corners[0][1] - 10) < 1e-5f);
    CHECK(std::fabs(f.corners[1][0] + 1) < 1e-5f && std::fabs(f.corners[2][1] - 2.5f) < 1e-5f);
    // Far away it is 30 long at most; seen side on, as long as half wide.
    CHECK(std::fabs(rainFlare({0, 1, 100}, {0, 2, 0}, {0, 0, -1}, 2, 0).corners[3][1] - 70) < 1e-4f);
    CHECK(std::fabs(rainFlare({0, 1, 10}, {0, 2, 0}, {1, 0, 0}, 2, 0).corners[3][1] - 9) < 1e-5f);
    CHECK(!rainFlare({0, 1, 0}, {0, 5, 0}, {0, 0, 1}, 2, 0).shown);
    CHECK(wetSurface(1) && wetSurface(10) && !wetSurface(14) && !wetSurface(0) && !wetSurface(2));
    // 0x4b81a0: the shadow's corners pushed along their edges away from
    // the light by the half height, all 0.125 up; flat without the cast.
    {
        const std::array<std::array<float, 3>, 4> box{{{-1, 0, 2}, {1, 0, 2}, {1, 0, -2}, {-1, 0, -2}}};
        const auto lit = carShadowQuad(box, {1, 0, 0}, 0.5f, true);
        CHECK(std::fabs(lit[0][0] + 1.5f) < 1e-6f && std::fabs(lit[3][0] + 1.5f) < 1e-6f);
        CHECK(lit[1][0] == 1 && lit[2][0] == 1 && lit[0][2] == 2 && lit[3][2] == -2);
        CHECK(lit[0][1] == 0.125f && lit[2][1] == 0.125f);
        // From high in front and to the right: the left and back corners
        // pushed by their share; the front right one stays.
        const float k = 1 / std::sqrt(3.0f);
        const auto high = carShadowQuad(box, {k, k, k}, 0.6f, true);
        CHECK(high[1][0] == 1 && high[1][2] == 2);
        CHECK(std::fabs(high[3][0] + 1 + k * 0.6f) < 1e-5f && std::fabs(high[3][2] + 2 + k * 0.6f) < 1e-5f);
        CHECK(std::fabs(high[0][0] + 1 + k * 0.6f) < 1e-5f && high[0][2] == 2);
        const auto flat = carShadowQuad(box, {1, 0, 0}, 0.5f, false);
        CHECK(flat[0][0] == -1 && flat[0][1] == 0.125f && kCarShadowColour == 0x6fffffffu);
    }
    // 0x4b8740 and 0x492980: the beam's colour over 30 % fog, and its cone.
    CHECK(fogBeamColour(30, 2) == 0 && fogBeamColour(60, 2) == 0xe4ffffffu && fogBeamColour(45, 1) == 0x39ffffffu);
    const std::array<float, 3> right{1, 0, 0}, up{0, 1, 0}, ahead{0, 0, 1};
    FogBeam b = fogBeam({0, 0, 0}, right, up, ahead, {0, 0, -50}, 0x80ffffffu);
    CHECK(b.shown && b.nearRing.size() == 7 && b.farRing.size() == 7 && b.colour == 0x80ffffffu);
    CHECK(std::fabs(b.farRing[0][2] - 6) < 1e-5f && std::fabs(std::hypot(b.farRing[2][0], b.farRing[2][1]) - 0.75f) < 1e-5f);
    CHECK(std::fabs(std::hypot(b.nearRing[3][0], b.nearRing[3][1]) - 0.1f) < 1e-5f && b.nearRing[3][2] == 0);
    CHECK(fogBeam({0, 0, 0}, right, up, ahead, {0, 0, 98}, 0x80ffffffu).nearRing.size() == 4); // 3 quads at least
    CHECK(fogBeam({0, 0, 0}, right, up, ahead, {0, 0, 30}, 0x80ffffffu).colour == 0); // looking into it
    CHECK(!fogBeam({0, 0, 0}, right, up, ahead, {0, 0, -150}, 0x80ffffffu).shown);
}

namespace {
// A straight road along +z, a point every 4: two lanes each side, 4
// wide (the bytes 8), walls 10 away, road strips in lanes 5-8.
TrackCollision straightRoad(int points, uint8_t codes = 0x44) {
    TrackCollision col;
    for (int i = 0; i < points; ++i) {
        TrackCollision::RoadPoint p;
        p.position = {0, 0, 4.0f * static_cast<float>(i)};
        p.normal = {0, 1, 0};
        p.forward = {0, 0, 1};
        p.right = {1, 0, 0};
        p.leftWall = p.rightWall = 10;
        p.extra = {0x22, 8, 8};
        p.unknown = 0x3c0u | static_cast<uint32_t>(codes) << 16;
        col.road.push_back(p);
    }
    return col;
}
} // namespace

void testComputerDriving() {
    TrackCollision col = straightRoad(200);
    const auto& p = col.road[0];
    // 0x40e2b0: 7 + offset / width, rounded to the nearest (even on a tie).
    CHECK(aiLaneAt(p, 1) == 7 && aiLaneAt(p, 2) == 7 && aiLaneAt(p, 2.1f) == 8 && aiLaneAt(p, 6) == 9);
    CHECK(aiLaneAt(p, -1) == 6 && aiLaneAt(p, -3) == 5 && aiLaneAt(p, 100) == 13);
    // 0x40daf0: lane middles, the lanes past the road a half width out,
    // kept within the walls.
    CHECK(aiLaneOffset(p, 7, 1) == 2 && aiLaneOffset(p, 8, 1) == 6 && aiLaneOffset(p, 6, 1) == -2);
    CHECK(aiLaneOffset(p, 9, 1) == 9 && aiLaneOffset(p, 9, 1.5f) == 8.5f && aiLaneOffset(p, 4, 1) == -9);
    CHECK(aiLaneOffset(p, 7, 1, 1) == 3);
    CHECK(aiLaneOpen(p, 5) && aiLaneOpen(p, 8) && !aiLaneOpen(p, 9) && !aiLaneOpen(p, 4));
    // The speed table: speeds for slice pairs, then a byte a slice.
    std::vector<uint8_t> speeds(16, 0);
    speeds.insert(speeds.end(), 100, 40);
    speeds.insert(speeds.end(), 200, 0x80); // the best line in lane 8
    CHECK(aiLineLane(speeds, 3, 200) == 8 && aiLineLane({}, 3, 200) == 7);
    // The drivers' factors (0x408dc0) and the tracks' (0x40e410).
    CHECK(aiDriverFactor({2, false, 2}, 0) == 1 && std::fabs(aiDriverFactor({0, true, 4}, 128) - 0.94f * 1.05f) < 1e-6f);
    CHECK(std::fabs(aiDriverFactor({1, true, 0}, 384) - 0.97f * 0.95f * 0.95f) < 1e-6f);
    CHECK(aiTrackFactor(0) == 0.82f && aiTrackFactor(1) == 0.84f);

    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.topSpeedCap = 80;
    // One car: the best line's vote (10 for skill 2) takes it a lane
    // right, its aim 0.2 a call of the 32 Hz model (the even steps)
    // towards the lane's middle; there it stays. Its driver acts on the
    // steps the gate lets through (0x401240): (step / 2 + phase) mod 4 = 0
    // on the even ones, every 8 steps.
    CHECK(aiDriverActs(AiCar{}, 0) && aiDriverActs(AiCar{}, 8) && !aiDriverActs(AiCar{}, 1) && !aiDriverActs(AiCar{}, 2) &&
          !aiDriverActs(AiCar{}, 9));
    AiCar car;
    car.slice = 10;
    car.halfWidth = 1;
    car.driver = {2, false, 2};
    car.offset = 2;
    car.speed = 40;
    std::vector<AiCar> cars{car};
    aiStep(cars, 0, col, speeds, data, 0, 1, false);
    CHECK(cars[0].pick == 8 && cars[0].target == 6 && std::fabs(cars[0].offset - 2.2f) < 1e-5f);
    aiStep(cars, 0, col, speeds, data, 1, 1, false); // an odd step: the aim stays
    CHECK(std::fabs(cars[0].offset - 2.2f) < 1e-5f);
    for (int step = 2; step < 80; ++step)
        aiStep(cars, 0, col, speeds, data, step, 1, false);
    // At the race's start (0x40f050): 400 slices with 5 or more racers,
    // 200 with 2-4, 50 with one, 26 less backwards, the place x 14 added.
    CHECK(aiAtRaceStart(400, 0, 8) && !aiAtRaceStart(401, 0, 8) && aiAtRaceStart(302, 7, 8) && !aiAtRaceStart(303, 7, 8));
    CHECK(aiAtRaceStart(200, 0, 2) && !aiAtRaceStart(201, 0, 4) && !aiAtRaceStart(51, 0, 1));
    CHECK(!aiAtRaceStart(375, 0, 8, true) && aiAtRaceStart(374, 0, 8, true) && aiAtRaceStart(474, 0, 8, false, 100));
    {
        // There the best line's vote is left out: the car keeps its lane
        // (keeping right gives lane 8 only +1 against its own lane's +1).
        std::vector<AiCar> early{car};
        early[0].distance = 100;
        aiStep(early, 0, col, speeds, data, 0, 1, false);
        CHECK(early[0].pick == 7 && early[0].target == 2);
        early[0].distance = 401;
        aiStep(early, 0, col, speeds, data, 8, 1, false);
        CHECK(early[0].pick == 8);
    }
    // In lane 8, with no road strip in lane 9 beside it (0x40d1f0), it is
    // nudged a half width left of the lane's middle.
    CHECK(cars[0].pick == 8 && cars[0].lane == 8 && cars[0].offset == 5 && !cars[0].braking);
    // Too fast for the table: the brakes (x 1.25 for an AI racer), held
    // only while more than 5 m/s over.
    cars[0].speed = 60;
    aiStep(cars, 0, col, speeds, data, 80, 1, false);
    CHECK(cars[0].braking && std::fabs(cars[0].speed - (60 - 12.5f / 32)) < 1e-4f && cars[0].wanted == 40);
    cars[0].speed = 44;
    aiStep(cars, 0, col, speeds, data, 82, 1, false);
    CHECK(!cars[0].braking && cars[0].speed < 44);
    // Slower: the acceleration table (x 0.9 with ai Normal), over 1/32 s.
    cars[0].speed = 30;
    aiStep(cars, 0, col, speeds, data, 84, 1, false);
    CHECK(std::fabs(cars[0].speed - (30 + 3.6f / 32)) < 1e-4f);
    {
        // Not past race distance 100 (0x406591): the table as it is.
        std::vector<AiCar> start{cars[0]};
        start[0].speed = 30;
        start[0].distance = 100;
        aiStep(start, 0, col, speeds, data, 84, 1, false);
        CHECK(std::fabs(start[0].speed - (30 + 4.0f / 32)) < 1e-4f);
        // The speed wanted (0x409330): the table's 40 held under the cap x
        // the track's factor (25 x 0.84 = 21) before the scale (0.5): 10.5,
        // not the scaled 20 under 21.
        AiCarData capped = data;
        capped.topSpeedCap = 25;
        start[0].scale = 0.5f;
        start[0].distance = 1 << 20;
        aiStep(start, 0, col, speeds, capped, 88, 1, false);
        CHECK(std::fabs(start[0].wanted - 21.0f * 0.5f) < 1e-4f);
    }
    {
        // The force model (ai_ctl_rails 0x4076b0, ai_integrate 0x4071a0):
        // nothing holds the speed at the speed wanted, so a car just under
        // it goes past it by the acceleration and is braked back.
        std::vector<AiCar> one{cars[0]};
        one[0].speed = 39.99f;
        aiStep(one, 0, col, speeds, data, 84, 1, false);
        CHECK(one[0].wanted == 40 && std::fabs(one[0].speed - (39.99f + 3.6f / 32)) < 1e-4f);
        aiStep(one, 0, col, speeds, data, 86, 1, false);
        CHECK(!one[0].braking && std::fabs(one[0].speed - (39.99f + 3.6f / 32 - 12.5f / 32)) < 1e-4f);
        // Accelerating over 44.703 m/s a racer's drive loses 3.45 / its
        // lateral grip multiplier (0x536364).
        std::vector<uint8_t> fast(16, 0);
        fast.insert(fast.end(), 100, 60);
        fast.insert(fast.end(), 200, 0x80);
        AiCarData grip = data;
        grip.latGrip = 3.45f / 2;
        one[0].speed = 45;
        aiStep(one, 0, col, fast, grip, 88, 1, false);
        CHECK(one[0].wanted == 60 && std::fabs(one[0].speed - (45 + (3.6f - 2) / 32)) < 1e-4f);
        // Gravity along the road (0x4a2a0c): -0.30656 m/s a tick x the
        // forward's rise, the way the car runs: up a 10 % slope it loses
        // 0.0306 m/s more a tick, running the other way it gains it.
        TrackCollision hill = col;
        for (auto& point : hill.road) {
            const float k = 1 / std::sqrt(1.01f);
            point.forward = {0, 0.1f * k, k};
        }
        std::vector<AiCar> up{cars[0]};
        up[0].speed = 30;
        aiStep(up, 0, hill, speeds, data, 84, 1, false);
        CHECK(std::fabs(up[0].speed - (30 + 3.6f / 32 - 0.3065625f * 0.1f / std::sqrt(1.01f))) < 1e-4f);
        up[0].way = -1;
        up[0].speed = 30;
        aiStep(up, 0, hill, speeds, data, 86, 1, false);
        CHECK(std::fabs(up[0].speed - (30 + 3.6f / 32 + 0.3065625f * 0.1f / std::sqrt(1.01f))) < 1e-4f);
    }

    // Held up behind a car in its lane within 2 slices, wanting more than
    // 8.94 m/s over its speed: its lights a step up for 64 steps (0x40de50),
    // its speed wanted the other's (0x4094d0).
    AiCar ahead = cars[0];
    ahead.slice = 11;
    ahead.speed = 20;
    cars[0].speed = 20;
    cars[0].beams = 1;
    cars.push_back(ahead);
    aiStep(cars, 0, col, speeds, data, 104, 1, false);
    CHECK(cars[0].flashing && cars[0].beams == 2 && cars[0].wanted == 20);
    cars[1].slice = 100; // gone
    for (int step = 105; step <= 175; ++step)
        aiStep(cars, 0, col, speeds, data, step, 1, false);
    CHECK(cars[0].flashing && cars[0].beams == 2); // its 64 steps were up at 168, its next call 176
    aiStep(cars, 0, col, speeds, data, 176, 1, false);
    CHECK(!cars[0].flashing && cars[0].beams == 1);
    // By day the lights come on for the flash.
    cars[0].beams = 0;
    cars[0].speed = 20;
    cars[1].slice = 11;
    aiStep(cars, 0, col, speeds, data, 200, 1, false);
    CHECK(cars[0].beams == 1);
    // Not at the race's start (0x404840 asks 0x40f050).
    {
        std::vector<AiCar> early = cars;
        early[0].beams = 0;
        early[0].flashing = false;
        early[0].speed = 20;
        early[0].distance = 0;
        aiStep(early, 0, col, speeds, data, 208, 1, false);
        CHECK(!early[0].flashing && early[0].beams == 0 && early[0].wanted == 20);
    }
    // A car going the other way keeps to its own right (lanes under 7).
    std::vector<AiCar> oncoming{car};
    oncoming[0].way = -1;
    oncoming[0].slice = 150;
    for (int step = 1; step < 80; ++step)
        aiStep(oncoming, 0, col, speeds, data, step, 1, false);
    CHECK(oncoming[0].lane < 7);

    // Cover (0x420bf0) and how far it reaches (0x4ba350).
    TrackCollision tunnel = straightRoad(40);
    for (int i = 10; i <= 20; ++i)
        tunnel.road[size_t(i)].unknown = (tunnel.road[size_t(i)].unknown & 0xffff) | 0x22u << 16;
    CHECK(pointCovered(tunnel.road[15], {1, 0, 60}) && !pointCovered(tunnel.road[5], {1, 0, 20}));
    CHECK(coverDepth(tunnel, 15, {1, 0, 60}) == 6 && coverDepth(tunnel, 11, {1, 0, 44}) == 2 && coverDepth(tunnel, 5, {1, 0, 20}) == 0);
    TrackCollision::RoadPoint half = col.road[0];
    half.unknown = 0x20u << 16; // covered on the left only; the right's 0 takes the left's
    CHECK(pointCovered(half, {-1, 0, 0}) && pointCovered(half, {1, 0, 0}));
    half.unknown = 0x24u << 16;
    CHECK(pointCovered(half, {-1, 0, 0}) && !pointCovered(half, {1, 0, 0}));
    // The road polygons' surfaces (byte 1 & 0x3F of their records).
    Track::Block block;
    block.polygonRoad = {0, 0x41, 0, 0, 0, 0, 0, 0, 0, 14};
    CHECK(roadSurface(block, 0) == 1 && roadSurface(block, 1) == 14 && roadSurface(block, 2) == 0);
    // A slice's run of road polygons (the positions record: first, count)
    // and where a point is across it (the track position's +0x6C): three
    // polygons side by side, 4 wide, from x -6 to 6; polygon i's edge 0-3
    // is the one it shares with i - 1, 1-2 with i + 1.
    Track runTrack;
    runTrack.blocks.resize(1);
    Track::Block& run = runTrack.blocks[0];
    for (int i = 0; i < 4; ++i) {
        const float x = -6.0f + 4.0f * static_cast<float>(i);
        run.vertices.push_back({x, 0, 4}); // front
        run.vertices.push_back({x, 0, 0}); // back
    }
    for (uint16_t i = 0; i < 3; ++i) {
        Track::Polygon poly;
        // 0 front left, 1 front right, 2 back right, 3 back left
        poly.vertices = {uint16_t(2 * i), uint16_t(2 * i + 2), uint16_t(2 * i + 3), uint16_t(2 * i + 1)};
        run.polygons[4].push_back(poly);
    }
    run.positions = {0, 0, 3, 0, 0xff, 0xff, 0xff, 0xff};
    run.polygonRoad = {0, 1, 0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0};
    size_t found = 99;
    CHECK(sliceRunSide(run, 0, {0, 0, 2}, found) == 0 && found == 1);
    CHECK(sliceRunSide(run, 0, {-5, 0, 2}, found) == 0 && found == 0);
    CHECK(sliceRunSide(run, 0, {5, 0, 2}, found) == 0 && found == 2);
    CHECK(sliceRunSide(run, 0, {-7, 0, 2}, found) == 1 && found == 0);
    CHECK(sliceRunSide(run, 0, {9, 0, 2}, found) == 2 && found == 2);
    CHECK(sliceRunSide(run, 1, {0, 0, 2}, found) == -1); // no record for slice 1
    // The track position's search (0x41e990) over the same run: found in
    // polygons 0 and 2, not in 1 (surface 14); beyond the run's ends side 1
    // (0x41ee86) and side 2 (0x41ebd7), nothing found at the other slices
    // (they have no records).
    {
        RoadSearch rs;
        CHECK(roadSearch(runTrack, rs, 5, 2) && rs.foundSlice == 0 && rs.polygon == 2 && rs.index == 2);
        rs = {};
        CHECK(roadSearch(runTrack, rs, -5, 2) && rs.polygon == 0 && rs.side == 0);
        rs = {};
        CHECK(!roadSearch(runTrack, rs, 9, 2) && rs.side == 2);
        rs = {};
        CHECK(!roadSearch(runTrack, rs, -7, 2) && rs.side == 1);
        rs = {};
        CHECK(!roadSearch(runTrack, rs, 0, 2) && rs.side == 0 && !rs.found);
        // A walk goes on past a polygon farther than the one before only
        // when it is of surface 14 (0x41ecd2): the run's polygon 2 is far
        // off and of surface 14, so the walk up from polygon 1 goes on to
        // 3, which holds the point.
        Track far;
        far.blocks.resize(1);
        Track::Block& fb = far.blocks[0];
        for (float x : {-2.0f, 2.0f, 6.0f, 20.0f, 24.0f, -6.0f}) {
            fb.vertices.push_back({x, 0, 4});
            fb.vertices.push_back({x, 0, 0});
        }
        auto quad = [&](uint16_t left, uint16_t right) {
            Track::Polygon q;
            q.vertices = {uint16_t(2 * left), uint16_t(2 * right), uint16_t(2 * right + 1), uint16_t(2 * left + 1)};
            fb.polygons[4].push_back(q);
        };
        quad(0, 1); // x -2..2
        quad(1, 2); // 2..6, the start ((4 - 1) / 2)
        quad(3, 4); // 20..24, surface 14
        quad(5, 0); // -6..-2
        fb.positions = {0, 0, 4, 0, 0xff, 0xff, 0xff, 0xff};
        fb.polygonRoad = {0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
        rs = {};
        CHECK(roadSearch(far, rs, -4, 2) && rs.polygon == 3 && rs.foundSlice == 0);
        fb.polygonRoad[17] = 4; // the far polygon a road one: the walk stops there
        rs = {};
        CHECK(!roadSearch(far, rs, -4, 2));
        // The down walk starts from the first slice's start less one, held
        // to nothing (0x41ecf6): from slice 0 (5 polygons, start 2) to
        // slice 1 (1 polygon) it reads polygon 1 of slice 1's run, which is
        // slice 2's first, and finds the point there.
        Track rows;
        rows.blocks.resize(1);
        Track::Block& rb = rows.blocks[0];
        auto rowQuad = [&](float x0, float x1, float z0) {
            const uint16_t v = static_cast<uint16_t>(rb.vertices.size());
            rb.vertices.push_back({x0, 0, z0 + 4});
            rb.vertices.push_back({x1, 0, z0 + 4});
            rb.vertices.push_back({x1, 0, z0});
            rb.vertices.push_back({x0, 0, z0});
            Track::Polygon q;
            q.vertices = {v, uint16_t(v + 1), uint16_t(v + 2), uint16_t(v + 3)};
            rb.polygons[4].push_back(q);
            for (uint8_t byte : {0, 1, 0, 0, 0, 0, 0, 0})
                rb.polygonRoad.push_back(byte);
        };
        for (int i = 0; i < 5; ++i)
            rowQuad(-10.0f + 4.0f * static_cast<float>(i), -6.0f + 4.0f * static_cast<float>(i), 0);
        rowQuad(-2, 2, 4);
        rowQuad(-2, 2, 8);
        rb.positions = {0, 0, 5, 0, 0xff, 0xff, 0xff, 0xff, 5, 0, 1, 0, 0xff, 0xff, 0xff, 0xff, 6, 0, 1, 0, 0xff, 0xff, 0xff, 0xff};
        rs = {};
        CHECK(roadSearch(rows, rs, 0, 10) && rs.foundSlice == 1 && rs.polygon == 6 && rs.index == 1 && rs.pastRun == 1);
        // From slice 2 itself it is found where it is.
        rs = {};
        rs.slice = 2;
        CHECK(roadSearch(rows, rs, 0, 10) && rs.foundSlice == 2 && rs.polygon == 6 && rs.index == 0 && rs.pastRun == 0);
    }
    // The officer's spot (0x401d78): kept on polygons 0 (surface 1) and 2
    // (4), not on 1 (surface 14) or off the run.
    CHECK(officerSpotOnRoad(runTrack, 0, {-5, 0, 2}) && officerSpotOnRoad(runTrack, 0, {5, 0, 2}));
    CHECK(!officerSpotOnRoad(runTrack, 0, {0, 0, 2}) && !officerSpotOnRoad(runTrack, 0, {-8, 0, 2}));

    // carp.txt's fields for the computer.
    std::string carp = "maximum velocity of car [m/s](14)\n77.5\ntop speed cap [m/s](15)\n78.23\nmaximum braking deceleration(18)\n10.06\n";
    for (int row = 0; row < 8; ++row) {
        carp += "AI ACC" + std::to_string(row) + " acceleration table section(" + std::to_string(67 + row) + ")\n";
        for (int i = 0; i < 14; ++i)
            carp += std::to_string(row * 14 + i) + ".5,";
        carp += "\n";
    }
    AiCarData parsed;
    CHECK(parseCarpAi(carp, parsed) && std::fabs(parsed.topSpeedCap - 78.23f) < 1e-4f && std::fabs(parsed.maxBrake - 10.06f) < 1e-4f);
    CHECK(parsed.accel[0] == 0.5f && parsed.accel[111] == 111.5f);
    CHECK(parsed.latGrip == 0); // field 30 left out: no drive loss over 44.703 m/s by it
    CHECK(parseCarpAi(carp + "lateral acceleration grip multiplier(30)\n3.300000\n", parsed) &&
          std::fabs(parsed.latGrip - 3.3f) < 1e-6f);
    CHECK(!parseCarpAi("maximum braking deceleration(18)\n10\n", parsed));
}

void testComputerCatchUpAndCops() {
    // The track sections' speeds (0x40ed40), the switch falling through.
    CHECK(trackSectionSpeed(0, 0x200) == 13.4109f && trackSectionSpeed(0, 10) == 22.3515f);
    CHECK(trackSectionSpeed(1, 0x317) == 31.2921f && trackSectionSpeed(1, 0xab) == 29.05695f && trackSectionSpeed(1, 5) == 24.58665f);
    CHECK(trackSectionSpeed(4, 0x130) == 8.9406f && trackSectionSpeed(4, 5) == 22.3515f);
    CHECK(trackSectionSpeed(8, 500) == 15.64605f && trackSectionSpeed(8, 5) == 20.11635f);
    // The catch-up (0x4090a0), as nfs-host's dumps of Hometown have it
    // (+0x7BC): the human behind them all, places 2-5 x 0.9 and 6-8 x
    // 0.8; some ahead, one 50 or more slices ahead x 0.85; none ahead,
    // one over 15 behind x 1.1.
    CHECK(aiRacersAhead(1, {85, 87, 92}) == 2 && aiRacersAhead(230, {236, 84}) == 1 && aiRacersAhead(196, {40}) == 0);
    CHECK(aiRacersAhead(0, {}) == 0);
    AiRace race;
    race.humanDistance = 1;
    race.ahead = 2;
    CHECK(aiCatchUpFactor(race, 92, 5) == 0.9f && aiCatchUpFactor(race, 88, 6) == 0.8f);
    race.raceType = 3;
    CHECK(aiCatchUpFactor(race, 88, 6) == 0.85f);
    race = {};
    race.humanDistance = 230;
    race.ahead = 1;
    CHECK(aiCatchUpFactor(race, 236, 1) == 1 && aiCatchUpFactor(race, 280, 1) == 0.85f && aiCatchUpFactor(race, 279, 1) == 1);
    race.ahead = 0;
    race.humanDistance = 196;
    CHECK(aiCatchUpFactor(race, 40, 8) == 1.1f && aiCatchUpFactor(race, 181, 2) == 1);
    race.raceType = 2;
    race.expert = true;
    CHECK(aiCatchUpFactor(race, 190, 2) == 1.15f);
    race.link = 2;
    CHECK(aiCatchUpFactor(race, 40, 8) == 1);
    race = {};
    race.human = false;
    CHECK(aiCatchUpFactor(race, 0, 8) == 1);

    // A computer cop (0x4032e0): it acts every 8 steps on its phase.
    TrackCollision col = straightRoad(200);
    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.maxVelocity = 70;
    AiCar cop;
    cop.cop = true;
    cop.racer = false;
    cop.slice = 100;
    cop.halfWidth = 1;
    cop.offset = cop.target = 2;
    cop.speed = 10;
    cop.wanted = 20;
    std::vector<AiCar> cars{cop};
    aiCopStep(cars, 0, col, data, 2, 1, false); // (2 / 2 + 0) & 3 = 1: not its call
    CHECK(cars[0].copState == 0 && std::fabs(cars[0].speed - (10 + 3.6f / 32)) < 1e-4f);
    aiCopStep(cars, 0, col, data, 8, 1, false);
    // Patrolling on track 1's slice 100: 24.59 x 1.19.
    CHECK(cars[0].copState == 1 && std::fabs(cars[0].wanted - 24.58665f * 1.19f) < 1e-4f);
    // With the siren: x 2, at most its top speed.
    cars[0].copState = 7;
    aiCopStep(cars, 0, col, data, 16, 1, false);
    CHECK(std::fabs(cars[0].wanted - 24.58665f * 2) < 1e-4f);
    // A racer within 16 slices that has been over the section's speed by
    // more than 2 m/s (+0x6F0, brought up to date as the cop looks,
    // 0x4014b0) becomes its target (0x403f80), and the chase begins (5).
    AiCar racer;
    racer.slice = 110;
    racer.halfWidth = 1;
    racer.offset = racer.target = 2;
    racer.speed = 24.58665f + 1.9f;
    cars.push_back(racer);
    aiCopStep(cars, 0, col, data, 24, 1, false);
    CHECK(cars[0].copState == 7 && cars[0].chasing == -1 && std::fabs(cars[1].overspeed - 1.9f) < 1e-4f);
    cars[1].speed = 24.58665f + 2.1f;
    aiCopStep(cars, 0, col, data, 32, 1, false);
    CHECK(cars[0].copState == 5 && cars[0].chasing == 1 && cars[0].siren && cars[1].recordOpen &&
          cars[1].chaseFrom == 32 && cars[1].callAt == 32 + 640);
    // The most it was over stays when it slows (+0x6F0 is a maximum).
    cars[1].speed = 10;
    aiCopStep(cars, 0, col, data, 40, 1, false);
    CHECK(std::fabs(cars[1].overspeed - 2.1f) < 1e-4f);
    cars[1].speed = 24.58665f + 2.1f;
    // The chase's speed (0x402420): 10 slices behind, the top speed;
    // within 8, closing: slices x (top - its speed) / 8 + its speed + 5.81.
    aiCopStep(cars, 0, col, data, 40, 1, false);
    CHECK(cars[0].wanted == 70);
    cars[0].slice = 106;
    aiCopStep(cars, 0, col, data, 48, 1, false);
    CHECK(std::fabs(cars[0].wanted - (4 * (70 - cars[1].speed) * 0.125f + cars[1].speed + 5.81139f)) < 1e-3f);
    // Turned to a target coming the other way (0x402910), lost past 50.
    cars[1].way = -1;
    aiCopStep(cars, 0, col, data, 56, 1, false);
    CHECK(cars[0].way == -1);
    cars[1].slice = 170;
    aiCopStep(cars, 0, col, data, 64, 1, false);
    CHECK(cars[0].copState == 1 && cars[0].chasing == -1 && !cars[0].siren);

    // The side offset as driven (+0x7B8) follows the aim, lagging it,
    // and the lanes are taken there (0x40e350).
    AiCar weaver;
    weaver.slice = 50;
    weaver.halfWidth = 1;
    weaver.offset = weaver.target = 2;
    weaver.speed = 30;
    weaver.wanted = 30;
    std::vector<AiCar> lone{weaver};
    std::vector<uint8_t> flat(16, 0);
    flat.insert(flat.end(), 100, 30);
    aiStep(lone, 0, col, flat, data, 1, 1, false); // the first step takes the aim as it
    CHECK(lone[0].sideKept && lone[0].side == 2);
    lone[0].offset = 5.9f;
    lone[0].target = 5.9f;
    aiStep(lone, 0, col, flat, data, 2, 1, false);
    CHECK(lone[0].side > 2 && lone[0].side < 2.2f && lone[0].lane == 7);
    // Then it weaves about lane 7's middle as the game's cars do: the
    // controller overshoots past 2 m, the lane at the offset (rounded at
    // the lane's middle) flips to 8 and back, and the aim with it.
    float lo = 100, hi = -100;
    int flips = 0, last = lone[0].lane;
    for (int step = 3; step < 400; ++step) {
        aiStep(lone, 0, col, flat, data, step, 1, false);
        if (step > 100) {
            lo = std::min(lo, lone[0].side);
            hi = std::max(hi, lone[0].side);
            flips += lone[0].lane != last ? 1 : 0;
        }
        last = lone[0].lane;
    }
    CHECK(lo > 0 && hi < 4.5f && flips >= 2);

    // A dump of the game's car records (0x9AC bytes each).
    std::vector<uint8_t> dump(2 * 0x9ac, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(dump.data() + at, &v, 4); };
    auto putF = [&](size_t at, float v) { std::memcpy(dump.data() + at, &v, 4); };
    dump[0x89] = 1;
    put32(0x200, 5);
    put32(0x1c, 230);
    putF(0x98, 472.5f);
    putF(0xd8 + 8, 1);
    put32(0x660, 2);
    const size_t b = 0x9ac;
    dump[b + 0x89] = 1;
    put32(b + 0x200, 0x2b);
    put32(b + 0x734, 1);
    dump[b + 0x509] = 255;
    put32(b + 0x944, 0x1801);
    putF(b + 0x7c4, 31.5f);
    // The officer on foot beside the second car (+0x8E4, +0x8E8, +0x8EC rows, +0x910).
    put32(b + 0x8e4, 1);
    put32(b + 0x8e8, 1);
    putF(b + 0x904 + 8, 1);
    putF(b + 0x8ec, 1);
    putF(b + 0x910, 742.75f);
    putF(b + 0x918, -266.25f);
    // Its tyres (+0x5C0, +0x5C4, +0x164, +0x10C, +0x160, +0x18C, the wheels' 16.16 points).
    putF(b + 0x5c0, 0.5f);
    putF(b + 0x5c4, 12.0f);
    putF(b + 0x10c, 1.0f);
    put32(b + 0x18c, 0x41);
    putF(b + 0xa4, 3.0f);
    put32(b + 0x304 + 2 * 0x78, static_cast<uint32_t>(1302 * 65536 + 32768));
    put32(b + 0x304 + 2 * 0x78 + 8, static_cast<uint32_t>(-951 * 65536));
    put32(0x164, 3);
    const std::vector<DumpedCar> parsedCars = parseCarDump(dump);
    CHECK(parsedCars[1].frontSkid == 0.5f && parsedCars[1].rearSkid == 12.0f && parsedCars[1].upright == 1 &&
          !parsedCars[1].airborne && parsedCars[0].airborne && parsedCars[1].surface == 1 && parsedCars[1].velocity[0] == 3 &&
          parsedCars[1].wheelAt[2][0] == 1302.5f && parsedCars[1].wheelAt[2][2] == -951.0f);
    // The particles' slots (0x7ddfd0, 128 bytes each): the live ones.
    {
        std::vector<uint8_t> parts(3 * 128, 0);
        auto p32 = [&](size_t at, uint32_t v) { std::memcpy(parts.data() + at, &v, 4); };
        auto pF = [&](size_t at, float v) { std::memcpy(parts.data() + at, &v, 4); };
        p32(128, 199);
        p32(128 + 4, 6);
        p32(128 + 8, 256);
        pF(128 + 0xc, 1303.25f);
        pF(128 + 0x24, 1.5f);
        p32(128 + 0x54, 0x64f1f1f1);
        parts[128 + 0x7d] = 1;
        const std::vector<Particle> live = parseParticleDump(parts);
        CHECK(live.size() == 1 && live[0].life == 199 && live[0].kind == 6 && live[0].lifeMax == 256 &&
              live[0].position[0] == 1303.25f && live[0].size == 1.5f && live[0].colour == 0x64f1f1f1 && live[0].halved);
    }
    CHECK(!parsedCars[0].officer && parsedCars[1].officer && parsedCars[1].officerAt[0] == 742.75f &&
          parsedCars[1].officerAt[2] == -266.25f && parsedCars[1].officerForward[2] == 1 && parsedCars[1].officerRight[0] == 1);
    CHECK(parsedCars.size() == 2 && parsedCars[0].human && !parsedCars[0].cop && parsedCars[0].slice == 230);
    CHECK(parsedCars[0].at[0] == 472.5f && parsedCars[0].forward[2] == 1 && parsedCars[0].beams == 2 && !parsedCars[0].siren);
    CHECK(parsedCars[1].cop && parsedCars[1].siren && parsedCars[1].braking && parsedCars[1].beamScroll == 1 &&
          parsedCars[1].speed == 31.5f && !parsedCars[1].human);
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
    {
        // The fog by the camera's road slice (0x478e10): Lost Canyons' day
        // region, 850-910-1000 from 50 % (44, 28, 38) to 10 % black.
        Horizon lc;
        lc.fogColour = {44, 28, 38};
        lc.fogDensity = 50;
        Horizon::FogRegion r;
        r.beforeSlice = 850;
        r.centreSlice = 910;
        r.afterSlice = 1000;
        r.start = {{44, 28, 38}, 50};
        r.apex = {{0, 0, 0}, 10};
        r.end = {{0, 0, 0}, 20};
        lc.fogRegions = {r};
        CHECK(fogAtSlice(lc, 120).density == 50 && fogAtSlice(lc, 849).colour == (Horizon::Rgb{44, 28, 38}));
        CHECK(fogAtSlice(lc, 850).density == 50 && fogAtSlice(lc, 910).density == 10);
        const Horizon::FogPoint mid = fogAtSlice(lc, 880);
        CHECK(mid.density == 30 && mid.colour == (Horizon::Rgb{22, 14, 19}));
        CHECK(fogAtSlice(lc, 955).density == 15 && fogAtSlice(lc, 1000).density == 20 && fogAtSlice(lc, 1001).density == 50);
    }
    CHECK(h.cloudType == 1 && h.visualHeight == 500 && h.cloudDomeOffset == (std::array<int, 2>{-100, 250}));
    CHECK(h.mirror && !h.blackHorizon && h.radius == 1500 && h.hasPixmap);
    CHECK(h.shadedBase == -1200 && h.shadedHeight == 1800 && h.pixmapBottom == 1150);
    CHECK(h.skyBaseSun == (Horizon::Rgb{223, 210, 151}) && h.skyBaseOpposite == (Horizon::Rgb{120, 171, 204}));
    CHECK(h.ambient == (Horizon::Rgb{100, 90, 80}));
    CHECK(parseHorizon(text + "7,", h));                      // a number too many
    CHECK(parseHorizon("1,2,3,4,5,99,", h));                  // regions that aren't there
    CHECK(parseHorizon("/* only a comment */", h));

    // The fog values the game sent for Hometown's day (density 5, pixmap
    // effect 70, radius 1500), and with the radius made 2000.
    CHECK(sceneryFogValue(5) == 475 && sceneryFogValue(0) == 0 && sceneryFogValue(100) == 5);
    Horizon hometown;
    hometown.fogOnPixmap = 70;
    hometown.radius = 1500;
    CHECK(skyFogValue(hometown, 5) == 2375);
    hometown.radius = 2000;
    CHECK(skyFogValue(hometown, 5) == 2850);
    hometown.fogOnPixmap = 100; // none on the pixmap: no fog for the sky
    CHECK(skyFogValue(hometown, 5) == 0);

    // The sky's layout, as the game drew Hometown's: the sky's colour at
    // the first slot edges, the cloud dome, the drift.
    hometown.skyBaseSun = {223, 210, 151};
    hometown.skyBaseOpposite = {120, 171, 204};
    const auto base = skyBaseColours(hometown);
    CHECK(base[0] == (Horizon::Rgb{223, 210, 151}) && base[1] == (Horizon::Rgb{210, 205, 157}));
    CHECK(base[15] == base[0] && base[8] == base[7]);
    CHECK(horizonTurn(hometown) == 0);
    hometown.rotation = 90;
    CHECK(std::fabs(horizonTurn(hometown) - 0.25f) < 1e-4f);
    hometown.rotation = -90;
    CHECK(std::fabs(horizonTurn(hometown) - 0.75f) < 1e-4f);

    // The sky's flat rows (0x479160, 0x479500): eight quads in a plane at
    // the radius along the forward on the ground, a slot's arc wide, from
    // 4 slots and the heading's fraction of one to the left.
    {
        Horizon flat;
        flat.radius = 1500;
        const float step = 1500 * 6.2831853f / 16;
        // Along +z, looking up: heading 0, so 16 x frac(0 + 0.5) = 8, no fraction.
        const SkyRow row = skyRow(flat, {0, 0.3f, 1}, -10, 600);
        CHECK(row.firstSlot == 8);
        CHECK(std::fabs(row.bottom[4][0]) < 1e-3f && std::fabs(row.bottom[4][2] - 1500) < 1e-3f);
        CHECK(std::fabs(row.bottom[0][0] + 4 * step) < 1e-2f && std::fabs(row.bottom[8][0] - 4 * step) < 1e-2f);
        CHECK(row.bottom[3][1] == -10 && row.top[3][1] == 590 && std::fabs(row.top[8][2] - 1500) < 1e-3f);
        // A quarter slot to the right: the row a quarter step to the left
        // (the right of +z is +x), the view's centre a quarter into slot 12.
        const float a = 6.2831853f / 64;
        const SkyRow turned = skyRow(flat, {std::sin(a), 0, std::cos(a)}, 0, 1);
        const float across = turned.bottom[4][0] * std::cos(a) - turned.bottom[4][2] * std::sin(a);
        const float ahead = turned.bottom[4][0] * std::sin(a) + turned.bottom[4][2] * std::cos(a);
        CHECK(turned.firstSlot == 8 && std::fabs(across + step / 4) < 1e-2f && std::fabs(ahead - 1500) < 1e-2f);
        // Along -x: heading 0.75, slot 16 x 0.25 = 4; with the rotation
        // 90, 16 x frac(1.5) = 8; nearer the next slot, rounded up.
        CHECK(skyRow(flat, {-2, 0, 0}, 0, 1).firstSlot == 4);
        flat.rotation = 90;
        CHECK(skyRow(flat, {-2, 0, 0}, 0, 1).firstSlot == 8);
        flat.rotation = 0;
        const float b = 6.2831853f * 0.7f / 16;
        const SkyRow late = skyRow(flat, {std::sin(b), 0, std::cos(b)}, 0, 1);
        CHECK(late.firstSlot == 9);
        CHECK(std::fabs(late.bottom[4][0] * std::cos(b) - late.bottom[4][2] * std::sin(b) - 0.3f * step) < 1e-2f);
        CHECK(skyRow(flat, {0, 1, 0}, 0, 1).firstSlot == 8); // straight up: along +z
        // The horizon picture's tiles (0x478bb0): twice round, the second
        // time mirrored with the mirror flag.
        bool flipped = true;
        CHECK(horizonTile(flat, 3, &flipped) == 3 && !flipped);
        CHECK(horizonTile(flat, 11, &flipped) == 3 && !flipped);
        flat.mirror = true;
        CHECK(horizonTile(flat, 7, &flipped) == 7 && !flipped);
        CHECK(horizonTile(flat, 8, &flipped) == 7 && flipped);
        CHECK(horizonTile(flat, 15, &flipped) == 0 && flipped);
        // No sky, black (0x47a4c0): the flag, or Lost Canyons' slices
        // 906-1064.
        CHECK(!skyBlack(flat, 5, 905) && skyBlack(flat, 5, 906) && skyBlack(flat, 5, 1064));
        CHECK(!skyBlack(flat, 5, 1065) && !skyBlack(flat, 0, 1000));
        flat.blackHorizon = true;
        CHECK(skyBlack(flat, 0, 0));
    }
    hometown.cloudDomeOffset = {-100, 250};
    CHECK(std::fabs(cloudHeight(hometown, 0) - 650) < 0.01f);
    CHECK(std::fabs(cloudHeight(hometown, 3000) + 100) < 0.01f);
    hometown.nimbus = 80; // below 100: no effect
    CHECK(std::fabs(cloudHeight(hometown, 0, 255) - 650) < 0.01f);
    hometown.nimbus = 100; // the brightest hang half the arch (750) lower
    CHECK(std::fabs(cloudHeight(hometown, 0, 255) - 275) < 0.01f);
    hometown.windSpeed = 10;
    hometown.windDirection = 90;
    const auto drift = cloudDrift(hometown);
    CHECK(std::fabs(drift[0]) < 1e-7f && std::fabs(drift[1] - 3e-4f) < 1e-7f);
    hometown.cloudBright = 235;
    hometown.cloudVariance = 208;
    const auto clouds = cloudBrightness(hometown, 1);
    CHECK(clouds == cloudBrightness(hometown, 1));
    CHECK(*std::max_element(clouds.begin(), clouds.end()) <= 235);
    CHECK(*std::max_element(clouds.begin(), clouds.end()) > *std::min_element(clouds.begin(), clouds.end()));
    // Without variance every point is the brightness pushed up from the
    // middle, (1 + sqrt(107 / 128)) x 127.5, and stays so: the passes only
    // hold a point to the brightness when it moves.
    Horizon even = hometown;
    even.cloudVariance = 0;
    const auto flat = cloudBrightness(even, 7);
    CHECK(std::all_of(flat.begin(), flat.end(), [](uint8_t v) { return v == 244; }));
    // The camera's place moves the clouds a tile a visual height (0x47b88a).
    even.visualHeight = 1000;
    const auto offset = cloudOffset(even, 500, -250);
    CHECK(std::fabs(offset[0] - 0.5f) < 1e-6f && std::fabs(offset[1] + 0.25f) < 1e-6f);
    even.visualHeight = 0;
    CHECK(cloudOffset(even, 500, 20)[0] == 0);

    // Lightning: on ticks that are a multiple of the second value; a bolt
    // of points 40 apart on the cloud dome, starting within 750, forking.
    SkyRandom random;
    hometown.lightning = 100;
    hometown.lightningChance = 64;
    CHECK(!lightningStarts(hometown, 63, random) && lightningStarts(hometown, 128, random));
    hometown.lightning = 0;
    CHECK(!lightningStarts(hometown, 128, random));
    const LightningBolt bolt = makeBolt(hometown, random);
    CHECK(bolt.points.size() > 40 && bolt.points.size() <= 240 && bolt.life == 24);
    CHECK(std::fabs(bolt.points[0].at[0]) <= 750 && std::fabs(bolt.points[0].at[2]) <= 750);
    bool onDome = true, apart = true;
    for (size_t i = 1; i < bolt.points.size(); ++i) {
        const auto& p = bolt.points[i];
        const auto& q = bolt.points[size_t(p.parent)];
        onDome = onDome && std::fabs(p.at[1] - cloudHeight(hometown, std::hypot(p.at[0], p.at[2]))) < 0.5f;
        apart = apart && std::fabs(std::hypot(p.at[0] - q.at[0], p.at[2] - q.at[2]) - 40) < 0.01f;
    }
    CHECK(onDome && apart);
    LightningBolt ticking = bolt;
    CHECK(boltFlash(ticking) == 255);
    for (int i = 0; i < 18; ++i)
        tickBolt(ticking, random);
    CHECK(ticking.life == 6 && boltFlash(ticking) == 127);
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
        CHECK(r.leftLanes() == 2 && r.rightLanes() == 2 && r.leftEdge() == 16.0f && r.rightEdge() == 16.0f);
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
    CHECK(x.loopStart == 2 && x.loopEnd == 20 && x.tags.at(0x92) == 1 && x.tags.at(0x06) == 25);

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

    // Version 4 has its slots from 20, after the audio's size and -1: the
    // same bank so laid out keeps its slot numbers.
    Bytes v4 = {'B', 'N', 'K', 'l', 4, 0, 4, 0};
    put32(v4, 128 + 8);
    put32(v4, uint32_t(b.size() - 128));
    put32(v4, 0xffffffffu);
    v4.insert(v4.end(), b.begin() + 12, b.end()); // offsets count from each slot, so they still hold
    CHECK(!parseSoundBank(v4, bank));
    CHECK(bank.version == 4 && bank.slots.size() == 4 && bank.slots[0] && !bank.slots[1] && bank.slots[2] && !bank.slots[3]);

    // Engines as the game pitches them, against its voices: the Corvette's
    // engine (root key 74, fine tune 28, range 15) idling at 900 of a
    // 6000 redline was at bend 16, -2497 cents; a range-6 sound at bend 77,
    // 121 cents.
    SoundHeader corvette;
    corvette.tags = {{0x07, 74}, {0x0A, 15}, {0x10, 28}};
    CHECK(engineBend(900, 6000) == 16);
    CHECK(patchCents(corvette, 60, 16) == -2497);
    CHECK(engineBend(6000, 6000) == 100 && engineBend(3000, 6000) == 54);
    SoundHeader plane;
    plane.tags = {{0x0A, 6}};
    CHECK(patchCents(plane, 60, 77) == 121 && patchCents(plane, 60, 64) == 0);
    SoundHeader flat;
    flat.tags = {{0x10, 249}}; // a signed byte
    CHECK(patchCents(flat, 60, 64) == -7);
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
    Bytes map = {'P', 'F', 'D', 'x', 0, 0, 3, 16, 0, 0, 0, 2};
    auto section = [&](uint8_t part, std::vector<std::array<uint8_t, 3>> routes) {
        Bytes r(28, 0);
        r[0] = part;
        r[1] = uint8_t(routes.size());
        r[2] = r[3] = 0xff;
        for (size_t k = 0; k < routes.size(); ++k)
            for (int i = 0; i < 3; ++i)
                r[4 + 3 * k + i] = routes[k][i];
        map.insert(map.end(), r.begin(), r.end());
    };
    section(0, {{0, 49, 1}, {60, 100, 2}});
    section(1, {{0, 100, 0}});
    section(1, {});
    // Two rows of events, for parts 0 and 1: event 4 goes to 2, then 0.
    for (uint8_t to : {uint8_t(2), uint8_t(0)}) {
        Bytes row(16, 0);
        row[4] = to;
        row[9] = 77; // a section that isn't there
        map.insert(map.end(), row.begin(), row.end());
    }
    for (uint32_t o : {0u, second, 0x01020304u})
        put32be(map, o);
    MusicMap m;
    CHECK(!parseMusicMap(map, m));
    CHECK(m.sections.size() == 3 && m.events.size() == 2 && m.offsets.size() == 3);
    CHECK(m.offsets[1] == second && m.offsets[2] == 0x01020304u);
    CHECK(m.next(0, 10) == 1 && m.next(0, 60) == 2 && m.next(0, 100) == 2 && m.next(1, 70) == 0);
    CHECK(m.next(0, 55) == 0);                                  // no route for 55: the section again
    CHECK(m.next(2, 0) == -1 && m.next(5, 0) == -1 && m.sections[2].part == 1);
    CHECK(m.event(0, 4) == 2 && m.event(1, 4) == 0 && m.event(2, 4) == 0);
    CHECK(m.event(0, 9) == -1 && m.event(0, 16) == -1 && m.event(7, 4) == -1);
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

Bytes madFrame(char kind, const MadBits& bits, int qscale = 1, int width = 16) {
    Bytes f = {'M', 'A', 'D', uint8_t(kind)};
    const Bytes body = bits.bytes();
    put32(f, uint32_t(24 + body.size()));
    put32(f, 0);
    put32(f, 4369067); // 66.67 ms, in 1/65536 ms
    put16(f, uint16_t(width));
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
    d.setArithmetic(MadDecoder::Arithmetic::Reference);
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

    // As the game shows it: at twice the size. Y 138 is the 7-bit 69 and
    // chroma 128 the 7-bit 64, half a step above the centre 63.5; the
    // colour by mad.md's formula, cut to 5:6:5 and widened again.
    Surface shown;
    d.toGameSurface(shown, MadDecoder::Display::Interpolate);
    CHECK(shown.w == 32 && shown.h == 32);
    {
        const int l = 2 * (138 >> 1) + 1;
        const int r = l + 2 * int(std::nearbyint(1.402 * 0.5));
        const int g = l + 2 * (int(std::nearbyint(-0.3441 * 0.5)) + int(std::nearbyint(-0.7141 * 0.5)));
        const int b = l + 2 * int(std::nearbyint(1.772 * 0.5));
        auto widen5 = [](int q) { return q << 3 | q >> 2; };
        auto widen6 = [](int q) { return q << 2 | q >> 4; };
        // Column 0 rounded, column 1 truncated.
        CHECK(shown.row(0)[0] == widen5(std::min((r + 4) >> 3, 31)) && shown.row(0)[1] == widen6(std::min((g + 2) >> 2, 63)) &&
              shown.row(0)[2] == widen5(std::min((b + 4) >> 3, 31)));
        CHECK(shown.row(0)[4] == widen5(r >> 3) && shown.row(0)[6] == widen5(b >> 3));
    }
    // Mode 3 leaves each line's last 4 pixels and the last line black;
    // mode 1 leaves the odd lines black.
    CHECK(shown.row(0)[28 * 4] == 0 && shown.row(0)[27 * 4] != 0 && shown.row(31)[0] == 0 && shown.row(1)[0] != 0);
    d.toGameSurface(shown, MadDecoder::Display::Repeat);
    CHECK(shown.row(1)[0] == 0 && shown.row(2)[0] != 0 && shown.row(0)[31 * 4] != 0);

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

    // The game's arithmetic: 7-bit samples about 64, so 10 above the middle
    // is 5; the escaped block's coefficient is 100 x Q, Q = 16 x 2^12 x 5906
    // / 2^16 = 5906, spread from about +17 to -18 across.
    MadDecoder g;
    g.setArithmetic(MadDecoder::Arithmetic::Game);
    CHECK(!g.decode(intra.data(), intra.size()));
    auto gy = [&](int x, int row) { return g.plane(0)[row * g.stride(0) + x]; };
    CHECK(gy(0, 0) == 69 && gy(8, 0) == 50 && gy(0, 8) == 64);
    CHECK(gy(8, 8) == 81 && gy(15, 15) == 46);
    Surface grgb;
    g.toSurface(grgb);
    CHECK(grgb.row(0)[0] == 138);
    // Its copies add d itself.
    CHECK(!g.decode(inter.data(), inter.size()));
    CHECK(gy(0, 0) == 72);

    // Its quantiser matrix applied transposed (its scan is the zigzag
    // transposed): the escaped level 5 at scan position 14, row 0 column 4,
    // takes W[4][0] = 22, not W[0][4] = 26. Q = 22 x 2^12 x 8192 / 2^16 =
    // 11264, and 5 Q = 56320, across as + - - + + - - +: bits 16-23 give 0
    // and -1 (with 26, 66560: 1 and -2).
    MadBits t;
    for (int j = 0; j < 6; ++j) {
        t.put(0, 8);
        if (j == 0) {
            t.code("000001");
            t.put(5, 10);
            t.put(13, 6);
        }
        t.code("10");
    }
    const Bytes transposed = madFrame('k', t);
    CHECK(!g.decode(transposed.data(), transposed.size()));
    CHECK(gy(0, 0) == 64 && gy(1, 0) == 63 && gy(2, 0) == 63 && gy(3, 0) == 64 && gy(5, 7) == 63 && gy(7, 7) == 64);

    // Its copies clamp to 0-127, as its output table does: 124 (DC 120)
    // copied 16 brighter ("1", "0", "1111") is 127.
    MadBits bright;
    bright.put(120, 8);
    bright.code("10");
    for (int j = 1; j < 6; ++j) {
        bright.put(0, 8);
        bright.code("10");
    }
    const Bytes brightFrame = madFrame('k', bright);
    MadBits brighter;
    brighter.code("1");  // all copied
    brighter.code("00"); // vector 0, 0
    brighter.code("101111");
    for (int j = 1; j < 6; ++j)
        brighter.code("0");
    const Bytes brighterFrame = madFrame('m', brighter);
    CHECK(!g.decode(brightFrame.data(), brightFrame.size()) && gy(0, 0) == 124);
    CHECK(!g.decode(brighterFrame.data(), brighterFrame.size()) && gy(0, 0) == 127 && gy(8, 0) == 64);

    // Its chroma vector is the luma one shifted right, rounding down (-1:
    // -1), where the reference halves it toward zero (0). 32x16: the first
    // macroblock's U is 10 above the middle; the second copies its U one
    // pixel left, the rest copied as they are.
    MadBits two;
    for (int j = 0; j < 12; ++j) {
        two.put(j == 4 ? 20 : 0, 8);
        two.code("10");
    }
    MadBits left;
    left.code("1" "00"); // the first macroblock copied, vector 0, 0
    for (int j = 0; j < 6; ++j)
        left.code("0");
    left.code("01");
    left.put(16, 6);      // U alone copied
    left.code("111111"); // x -1
    left.code("0");      // y 0
    for (int j = 0; j < 6; ++j) {
        if (j == 4) {
            left.code("0");
            continue;
        }
        left.put(0, 8);
        left.code("10");
    }
    const Bytes twoFrame = madFrame('k', two, 1, 32), leftFrame = madFrame('m', left, 1, 32);
    auto gu = [&](MadDecoder& dec, int x) { return dec.plane(1)[x]; };
    CHECK(!g.decode(twoFrame.data(), twoFrame.size()) && gu(g, 7) == 74 && gu(g, 8) == 64);
    CHECK(!g.decode(leftFrame.data(), leftFrame.size()) && gu(g, 7) == 74 && gu(g, 8) == 74 && gu(g, 9) == 64);
    MadDecoder r;
    r.setArithmetic(MadDecoder::Arithmetic::Reference);
    CHECK(!r.decode(twoFrame.data(), twoFrame.size()) && gu(r, 7) > 128 && gu(r, 8) == 128);
    CHECK(!r.decode(leftFrame.data(), leftFrame.size()) && gu(r, 8) == 128);

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
    fresh.setArithmetic(MadDecoder::Arithmetic::Reference);
    CHECK(!movie.decodeFrame(fresh, 0) && !movie.decodeFrame(fresh, 1));
    CHECK(fresh.plane(0)[0] == 144);
    CHECK(movie.decodeFrame(fresh, 2));
    Bytes cut(mad.begin(), mad.end() - 3);
    CHECK(movie.open(cut));
}

// THRASH's fog as voodoo2a.dll has Glide draw it (thrash.md, Fog).
void testThrashFog() {
    CHECK(glideFog(0, 5) == 0);
    CHECK(std::fabs(glideFog(52428.8f, 5) - 1) < 1e-4f);
    CHECK(std::fabs(glideFog(1, 5) - (1 - std::exp(-0.2f))) < 1e-4f);
    CHECK(glideFog(100, 500) < glideFog(1000, 500));
    CHECK(glideFog(1000, 0) == 0);

    // Triangles over pixel (10, 10), black, at the w given; red fog of 5.
    x86::Cpu cpu(1u << 21);
    const uint32_t target = 0x10000, verts = 0x1000;
    NativeThrash t(cpu);
    t.setTarget(target, 640 * 2, 640, 480);
    auto put = [&](int i, float x, float y, float w) {
        const float f[4] = {x, y, 0.5f, 1 / w};
        std::memcpy(cpu.memory().data() + verts + 32 * i, f, 16);
        const uint32_t black = 0xff000000u;
        std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &black, 4);
    };
    auto draw = [&](float w) {
        put(0, 0, 0, w);
        put(1, 40, 0, w);
        put(2, 0, 40, w);
        t.triangle(verts, verts + 32, verts + 64);
        uint16_t p;
        std::memcpy(&p, cpu.memory().data() + target + 10 * 1280 + 10 * 2, 2);
        return p;
    };
    t.setState(0xf, 0xffff0000u);
    t.setState(0xe, 5);
    CHECK(draw(50000) == 0xf800); // all fog
    const uint16_t near = draw(1);   // 18% of it, by entry 0 of the table
    CHECK((near >> 11) >= 4 && (near >> 11) <= 6 && (near & 0x7ff) == 0);
    t.setState(0x15, 0);
    CHECK(draw(50000) == 0); // off
    t.setState(0x15, 4);
    CHECK(draw(50000) == 0xf800); // on again, with the table it had
    t.setState(0xe, 0);
    CHECK(draw(50000) == 0);

    // Dithered to 5:6:5 as Glide does (state 5, on from the start): a
    // 4x4 block of grey 100 averages 100 in red (12.15 of 31 steps), where
    // truncation gives 12 steps (98.7) everywhere; state 5 = 0 truncates.
    {
        auto grey = [&](uint32_t colour) {
            for (int i = 0; i < 3; ++i)
                std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &colour, 4);
            t.triangle(verts, verts + 32, verts + 64);
            float sum = 0;
            int distinct = 0, last = -1;
            for (int y = 4; y < 8; ++y)
                for (int x = 4; x < 8; ++x) {
                    uint16_t p;
                    std::memcpy(&p, cpu.memory().data() + target + y * 1280 + x * 2, 2);
                    sum += float(p >> 11);
                    distinct += int(p >> 11) != last && last >= 0 ? 1 : 0;
                    last = int(p >> 11);
                }
            return std::make_pair(sum / 16, distinct);
        };
        put(0, 0, 0, 1);
        put(1, 40, 0, 1);
        put(2, 0, 40, 1);
        const auto dithered = grey(0xff646464u);
        CHECK(std::fabs(dithered.first - 100.0f * 31 / 255) < 0.07f && dithered.second > 0);
        t.setState(0x5, 0);
        const auto plain = grey(0xff646464u);
        CHECK(plain.first == 12 && plain.second == 0);
        t.setState(0x5, 1);
    }

    // Recorded for the GPU, a batch carries its fog.
    t.setState(0xe, 250);
    t.setRecording(true);
    draw(100);
    const ThrashFrame frame = t.takeFrame();
    CHECK(frame.batches.size() == 1 && frame.batches[0].fog == 250 && frame.batches[0].fogColour == 0xff0000);
}

} // namespace

// DirectInput effects as the game would set them, in a little memory of
// their own: DIEFFECT read, and what playing effects add up to.
void testForceEffects() {
    std::map<uint32_t, uint32_t> memory;
    auto read32 = [&](uint32_t a) { return memory.count(a) ? memory[a] : 0u; };
    // A DIEFFECT at 0x100 (56 bytes): object offsets, cartesian, half a
    // second, gain 8000, on X alone, coming from -X; its DICONSTANTFORCE at
    // 0x200 and an envelope at 0x300.
    auto effect = [&](uint32_t flags, uint32_t axes, uint32_t typeBytes) {
        memory[0x100] = 56;
        memory[0x104] = flags;
        memory[0x108] = 500000;
        memory[0x110] = 8000;
        memory[0x11c] = axes;
        memory[0x120] = 0x180; // rgdwAxes
        memory[0x124] = 0x190; // rglDirection
        memory[0x128] = 0;
        memory[0x12c] = typeBytes;
        memory[0x130] = 0x200;
        memory[0x134] = 0;
    };
    effect(0x2 | 0x10, 1, 4);
    memory[0x180] = 0;                  // X, by offset
    memory[0x190] = uint32_t(-1);       // from -X
    memory[0x200] = 5000;               // magnitude
    DiEffect push;
    CHECK(DiEffect::kindOf(0x13541c20, push.kind) && push.kind == DiEffect::Constant);
    CHECK(!DiEffect::kindOf(0x6f1d2b60, push.kind));
    push.set(read32, 0x100, 0x3ff);
    CHECK(push.durationUs == 500000 && push.gain == 8000 && push.xAxis == 0 && push.magnitude == 5000);
    CHECK(push.alongX() == -1.0f);
    // Not playing: nothing. Playing, with the device at half gain:
    // 5000/10000 x 0.8 x 0.5, from -X.
    CHECK(sumForces({&push}, 10000, 1000).constant == 0);
    push.playing = true;
    push.startedMs = 1000;
    GamepadForce f = sumForces({&push}, 5000, 1200);
    CHECK(std::fabs(f.constant + 0.2f) < 1e-4f);
    // Done after its half second (one iteration); for ever with infinite.
    CHECK(sumForces({&push}, 10000, 1500).constant == 0);
    push.iterations = DiEffect::kInfinite;
    CHECK(sumForces({&push}, 10000, 9000).constant != 0);
    push.iterations = 1;

    // An envelope: attack from 0 over 0.1 s, so halfway through it half.
    memory[0x128] = 0x300;
    memory[0x300] = 20;
    memory[0x304] = 0;
    memory[0x308] = 100000;
    push.set(read32, 0x100, 0x80); // DIEP_ENVELOPE alone
    CHECK(push.envelope && push.attackUs == 100000);
    f = sumForces({&push}, 10000, 1050);
    CHECK(std::fabs(f.constant + 0.2f) < 1e-3f); // -(5000/2)/10000 x 0.8

    // Two axes, polar, from the east (90 degrees): all of it along +X.
    effect(0x2 | 0x20, 2, 4);
    memory[0x180] = 0;
    memory[0x184] = 4;
    memory[0x190] = 9000;
    DiEffect polar;
    polar.set(read32, 0x100, 0x3ff);
    CHECK(polar.axisCount == 2 && std::fabs(polar.alongX() - 1.0f) < 1e-4f);
    // On Y alone: none of it along X.
    effect(0x2 | 0x10, 1, 4);
    memory[0x180] = 4;
    DiEffect onY;
    onY.set(read32, 0x100, 0x3ff);
    CHECK(onY.xAxis == -1 && onY.alongX() == 0);

    // A sine (DIPERIODIC: magnitude, offset, phase, period in us) and a
    // spring (DICONDITION), as the game makes them for the engine and the
    // wheel's centring.
    effect(0x2 | 0x10, 1, 16);
    memory[0x180] = 0;
    memory[0x190] = 1;
    memory[0x200] = 3000;
    memory[0x204] = 0;
    memory[0x20c] = 83000;
    DiEffect sine;
    sine.kind = DiEffect::Sine;
    sine.set(read32, 0x100, 0x3ff);
    sine.playing = true;
    sine.durationUs = DiEffect::kInfinite;
    effect(0x2 | 0x10, 1, 24);
    memory[0x200] = 1000;   // offset
    memory[0x204] = 7000;   // + coefficient
    memory[0x208] = 7000;   // - coefficient
    memory[0x20c] = 9500;   // + saturation
    memory[0x210] = 9500;
    DiEffect spring;
    spring.kind = DiEffect::Spring;
    spring.set(read32, 0x100, 0x3ff);
    spring.playing = true;
    spring.durationUs = DiEffect::kInfinite;
    f = sumForces({&sine, &spring}, 10000, 5000);
    CHECK(std::fabs(f.vibration - 0.24f) < 1e-4f && f.vibrationPeriodMs == 83 && !f.vibrationSquare);
    CHECK(std::fabs(f.spring - 0.56f) < 1e-4f && std::fabs(f.springCentre - 0.1f) < 1e-4f &&
          std::fabs(f.springSaturation - 0.95f) < 1e-4f);
    CHECK(f.constant == 0 && f.damper == 0);
    // SetParameters with only the type-specific part keeps the rest.
    memory[0x204] = 2000;
    memory[0x208] = 2000;
    memory[0x108] = 1;
    spring.set(read32, 0x100, 0x100);
    CHECK(spring.durationUs == DiEffect::kInfinite && spring.positiveCoefficient == 2000);
}

// DirectSound 3D's model (sound3d.h): distance, pan, cone and Doppler.
void testSound3d() {
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-4; };
    Ds3dListener listener;
    Ds3dSource s;
    // Ahead, within the minimum distance: as it is.
    s.position[2] = 0.5f;
    Ds3dResult r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1) && near(r.doppler, 1));
    // Ahead at 2 and 4 times the minimum distance: -6 and -12 dB or so;
    // twice the rolloff, faster; past the maximum distance, no quieter.
    s.position[2] = 2;
    CHECK(near(spatialize(listener, s).left, 0.5));
    s.position[2] = 4;
    CHECK(near(spatialize(listener, s).left, 0.25));
    listener.rolloffFactor = 2;
    CHECK(near(spatialize(listener, s).left, 1.0 / 7));
    listener.rolloffFactor = 1;
    s.maxDistance = 2;
    CHECK(near(spatialize(listener, s).left, 0.5));
    s.maxDistance = 1e9f;
    // Straight to the right: the left speaker 20 dB down; to the left, the
    // other way round; 30 degrees right, the left down by 10 dB.
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, 0.1));
    s.position[0] = -1;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 0.1));
    s.position[0] = 0.5f;
    s.position[2] = std::sqrt(0.75f);
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, std::pow(10.0, -0.5)));
    // The listener turned to face +x: a source at +x is ahead; one at +z is
    // now to its left.
    listener.front[0] = 1;
    listener.front[2] = 0;
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1));
    s.position[0] = 0;
    s.position[2] = 1;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 0.1));
    // Straight behind (-x for this listener): both speakers 3 dB down and
    // muffled to 4 kHz; beside, neither.
    s.position[0] = -1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.behind, 1) && near(r.left, std::pow(10.0, -0.15)) && near(r.right, r.left));
    CHECK(near(rearCutoff(r.behind), 4000) && near(rearCutoff(0), 20000) && near(rearGain(0), 1));
    CHECK(near(lowPassCoefficient(4000, 44100), 1 - std::exp(-2 * 3.14159265358979 * 4000 / 44100)));
    // Head-relative: the position is the listener's own, whichever way it
    // faces; disabled: as it is, wherever.
    s.mode = 1;
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, 0.1));
    s.mode = 2;
    s.position[0] = 100;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1));
    s = Ds3dSource();
    listener = Ds3dListener();
    // A cone pointing away (-z) from a listener at the origin, the source
    // ahead of it at z 1: outside, so -20 dB.
    s.position[2] = 1;
    s.insideCone = 90;
    s.outsideCone = 180;
    s.coneOrientation[2] = 1;
    s.coneOutsideVolume = -2000;
    CHECK(near(spatialize(listener, s).left, 0.1));
    s.coneOrientation[2] = -1; // pointing at the listener: inside
    CHECK(near(spatialize(listener, s).left, 1));
    s = Ds3dSource();
    // Doppler: a source 10 units ahead coming at the listener at a tenth of
    // the speed of sound sounds higher, 1 / 0.9; going away, lower.
    s.position[2] = 10;
    s.velocity[2] = -34.33f;
    CHECK(near(spatialize(listener, s).doppler, 1 / 0.9));
    s.velocity[2] = 34.33f;
    CHECK(near(spatialize(listener, s).doppler, 1 / 1.1));
    listener.dopplerFactor = 0;
    CHECK(near(spatialize(listener, s).doppler, 1));
}

void testComputerPursuits() {
    // Lanes 4 wide (bytes 8), two each side, walls 10 away.
    TrackCollision col = straightRoad(200);
    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.maxVelocity = 70;
    data.topSpeedCap = 80;
    std::vector<uint8_t> flat(16, 0);
    flat.insert(flat.end(), 100, 30);

    // The side offset as the game measures it (0x4a2bc0): from the slice's
    // point along its right, so on a bend it moves across the slice while
    // the car goes straight on.
    TrackCollision bent = straightRoad(20);
    for (size_t i = 6; i < bent.road.size(); ++i)
        bent.road[i].position[0] = 0.5f * static_cast<float>(i - 5);
    AiCar measured;
    measured.slice = 6;
    measured.halfWidth = 1;
    measured.offset = measured.target = measured.side = measured.lateral = 2;
    measured.sideKept = true;
    measured.along = 0.5f;
    measured.speed = measured.wanted = 30;
    std::vector<AiCar> one{measured};
    aiStep(one, 0, bent, flat, data, 0, 1, false);
    CHECK(std::fabs(one[0].side - one[0].lateral - 0.25f) < 1e-4f); // half the slice's 0.5

    // A strip (spike_lay 0x4c9dd0): across lane 7's middle, 1.5 lanes long.
    float from = 0, to = 0;
    aiStripSpan(col.road[0], 7, from, to);
    CHECK(from == -1 && to == 5);
    aiStripSpan(col.road[0], 6, from, to);
    CHECK(from == -5 && to == 1);

    // The police car ordered to lay one (0x4039c0's order: kind 0 from its
    // slice - 6, 3 slices): it turns back to the range, goes to the
    // roadside (3), stops off the lanes (4) and lays it in lane 7 (an even
    // cop), radioing (0x40b9c0).
    AiCar cop;
    cop.cop = true;
    cop.racer = false;
    cop.slice = 100;
    cop.halfWidth = 1;
    cop.offset = cop.target = 2;
    cop.phase = 1;
    AiCar racer;
    racer.slice = 60;
    racer.halfWidth = 1;
    racer.offset = racer.target = 2;
    racer.speed = racer.wanted = 30;
    std::vector<AiCar> cars{racer, cop};
    aiOrderStrip(cars[1], 0, 200);
    CHECK(cars[1].orderKind == 0 && cars[1].orderFrom == 94 && cars[1].orderLength == 3 && cars[1].stripWanted);
    int laid = -1;
    for (int step = 0; step < 64 * 20 && laid < 0; ++step) {
        cars[1].along = 0;
        aiCopStep(cars, 1, col, data, step, 1, false);
        if (step % 16 == 0) // on along its way, a slice each 16 steps at most
            cars[1].slice = std::clamp(cars[1].slice + (cars[1].speed > 1 ? cars[1].way : cars[1].speed < -1 ? -cars[1].way : 0), 0, 199);
        if (cars[1].stripOn)
            laid = step;
    }
    CHECK(laid > 0 && cars[1].copState == 4 && cars[1].way == -1 && cars[1].stripLane == 7 && cars[1].radio == 0x40b9c0);
    // Laid where it gets off the lanes, from the range's start on along its
    // way: turning out at 6.5 m/s (ai_ctl_slow) takes it a few seconds,
    // and this loop moves it on a slice each 16 steps.
    CHECK(cars[1].stripSlice <= 97 && cars[1].stripSlice > 70);
    // A racer 1-5 slices before it in that lane: -50 there, its speed
    // wanted x 0.9 (0x40d720), so it moves over.
    cars[0].slice = cars[1].stripSlice - 3;
    cars[0].offset = cars[0].target = cars[0].side = cars[0].lateral = 2;
    cars[0].sideKept = true;
    aiStep(cars, 0, col, flat, data, 0, 1, false);
    CHECK(cars[0].pick != 7 && std::fabs(cars[0].wanted - 30 * 0.9f) < 1e-4f);
    // Its front wheels 0.6 m short of the strip's line: not on it (out
    // codes against the wheels' +-0.5 m boxes, 0x4ca160).
    cars[0].slice = cars[1].stripSlice - 1;
    cars[0].along = (4.0f - 1.3f - 0.6f) / 4.0f;
    cars[0].offset = cars[0].target = cars[0].side = cars[0].lateral = 2;
    cars[0].speed = 40;
    cars[0].overspeed = 15; // as a cop's look at it left +0x6F0
    aiCopStep(cars, 1, col, data, 1, 1, false);
    CHECK(cars[1].stripOn && !cars[0].pulledOver);
    // Run over (the front wheels on its line), it is pulled over (0x401650)
    // and the cop takes it as its target; the racer pulls over (5), and
    // stops (6).
    cars[0].along = (4.0f - 1.3f) / 4.0f;
    aiCopStep(cars, 1, col, data, 1, 1, false);
    CHECK(!cars[1].stripOn && cars[0].pulledOver && cars[0].spiked && cars[1].chasing == 0 && cars[1].copState == 5 &&
          cars[1].siren && cars[0].recordStep == 1 + 320);
    cars[0].slice = cars[1].stripSlice;
    cars[0].along = 0;
    aiStep(cars, 0, col, flat, data, 8, 1, false);
    CHECK(cars[0].pursuit == 5 && cars[0].wanted == 11);
    cars[0].speed = 2;
    aiStep(cars, 0, col, flat, data, 16, 1, false);
    CHECK(cars[0].pursuit == 6);
    // The cop within 16 m of it: the stop (6), and 384 steps later the
    // ticket (0x401ff0): free of chases for 960 steps, back on patrol.
    cars[1].slice = cars[0].slice;
    cars[1].way = 1;
    cars[1].offset = cars[1].target = cars[1].side = cars[1].lateral = cars[0].lateral;
    aiCopStep(cars, 1, col, data, 2 * 64 + 6, 1, false); // (67 + 1) & 3 = 0
    CHECK(cars[1].copState == 6 && !cars[1].siren && cars[0].recordStep == 134 + 384);
    aiCopStep(cars, 1, col, data, 134 + 384, 1, false); // not past it yet
    CHECK(cars[1].copState == 6);
    aiCopStep(cars, 1, col, data, 134 + 392, 1, false);
    CHECK(cars[1].copState == 1 && cars[1].chasing == -1 && cars[0].tickets == 1 && !cars[0].pulledOver &&
          cars[0].chaseFree == 526 + 960 && cars[1].radio == 0x40b670 && cars[1].orderKind == 2);
    aiStep(cars, 0, col, flat, data, 528, 1, false);
    CHECK(cars[0].pursuit == 1);

    // The chase: past its stopped target by more than 7 slices and under
    // 3 m/s, the cop reverses at 9.83 m/s (0x402420), its aim the road's
    // point (ai_reverse_end); then slices x 5 back.
    AiCar chaser = cop;
    chaser.phase = 0;
    chaser.slice = 120;
    chaser.copState = 5;
    chaser.chasing = 0;
    chaser.speed = 0;
    std::vector<AiCar> chase{racer, chaser};
    chase[0].slice = 110;
    chase[0].speed = 0;
    chase[0].pulledOver = true;
    chase[0].pursuit = 6;
    chase[0].recordStep = 1000; // the stop's step not yet past (0x402110)
    aiCopStep(chase, 1, col, data, 0, 1, false);
    CHECK(chase[1].reversing && std::fabs(chase[1].wanted + 9.834f) < 1e-4f && chase[1].offset == 0);
    aiCopStep(chase, 1, col, data, 8, 1, false);
    CHECK(chase[1].reversing && chase[1].wanted == -50 && chase[1].speed < 0);

    // A roadblock (0x403b20's order, kind 4) at its own slice with no racer
    // within 240 m: in place (10), radioing (0x40bac0), then across the road
    // (11, 0x403180); the racer 5 slices past it (0x403ed0): patrol.
    AiCar blocker = cop;
    blocker.phase = 0;
    blocker.slice = 50;
    blocker.speed = 0;
    std::vector<AiCar> block{racer, blocker};
    block[0].slice = 150; // 400 m off
    aiOrderRoadblock(block[1], 0, 50);
    aiCopStep(block, 1, col, data, 0, 1, false);
    CHECK(block[1].copState == 11 && block[1].across && block[1].siren);
    block[0].slice = 56;
    aiCopStep(block, 1, col, data, 8, 1, false);
    CHECK(!block[1].across && !block[1].blockWatch && block[1].orderKind == 2);
    CHECK(std::string(aiRadioCall(0x40b670)) == "suspect ticketed" && std::string(aiRadioCall(0x123)).empty());
}

void testSkinPaint() {
    // The loader's recolouring (0x49cd00, 0x49cc50): alpha under 128 the
    // paint, 128-254 its secondary colour, 255 kept, 0 transparent.
    Model::Colour paint{{44, 12, 128, 128}}, second{{126, 48, 38, 128}};
    const auto painted = skinTexel({105, 105, 106, 50}, &paint, &second);
    // t = 104 / 127, the value trunc(128 t) = 104, at hue 44 and saturation
    // 12, to 5 bits.
    const auto expect = hsvToRgb({44, 12, 104});
    CHECK(painted[0] == ((expect[0] >> 3) << 3) && painted[1] == ((expect[1] >> 3) << 3) && painted[2] == ((expect[2] >> 3) << 3) &&
          painted[3] == 255);
    const auto window = skinTexel({120, 117, 116, 200}, &paint, &second);
    const auto expectWindow = hsvToRgb({126, 48, 33}); // trunc(38 x 112 / 127)
    CHECK(window[2] == ((expectWindow[2] >> 3) << 3) && window[0] == ((expectWindow[0] >> 3) << 3));
    CHECK((skinTexel({10, 20, 30, 255}, &paint, &second) == std::array<uint8_t, 4>{10, 20, 30, 255}));
    CHECK(skinTexel({10, 20, 30, 0}, &paint, &second)[3] == 0);
    CHECK((skinTexel({10, 20, 30, 50}, nullptr, nullptr) == std::array<uint8_t, 4>{10, 20, 30, 255}));
    // The rows as stored (0x49cd00): a TGA stored from the bottom (bit 5 of
    // byte 17 clear) is turned back over after decoding; one from the top
    // is left.
    image::Image two;
    two.w = 1;
    two.h = 2;
    two.rgba = {1, 1, 1, 1, 2, 2, 2, 2};
    std::vector<uint8_t> header(18, 0);
    skinRowsAsStored(header, two);
    CHECK(two.rgba[0] == 2 && two.rgba[4] == 1);
    header[17] = 0x28;
    skinRowsAsStored(header, two);
    CHECK(two.rgba[0] == 2);
    // The colour a paint goes with (0x49d240), by RGB distance.
    Model m;
    m.primaryColours = {Model::Colour{{0, 255, 99, 108}}, Model::Colour{{44, 12, 128, 128}}};
    m.secondaryColours = {Model::Colour{{1, 1, 1, 1}}, Model::Colour{{2, 2, 2, 2}}};
    CHECK(primaryFor(m, {43, 12, 128}) == 1 && secondaryFor(m, {0, 250, 100}) == 0);
}

// The police stand-in's rules from November 2026's reading (d6): the stop's
// hold by race distance, the strip's quad and the wheels on it, the calls
// for help by heat, and the bend a car cuts as it steers at a point ahead.
void testCopCalls() {
    // cop_ai_stop_check (0x402110, 0x40237a): 160 / 192 / 224 steps.
    CHECK(aiStopHold(false, 61) == 160 && aiStopHold(false, 60) == 192 && aiStopHold(false, -61) == 224 &&
          aiStopHold(false, 0) == 192 && aiStopHold(true, 100) == 192);

    // spike_lay (0x4c9dd0): the quad's ends on the road, 0.5 above them.
    TrackCollision col = straightRoad(200);
    const auto quad = aiStripQuad(col.road[10], 7);
    CHECK(quad[0][0] == -1 && quad[1][0] == 5 && quad[0][2] == 40 && quad[2][1] == 0.5f && quad[3][0] == -1);
    // spike_hit_test (0x4ca160): the wheels within 0.5 m of it, a bit each.
    const std::array<float, 3> right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    const std::array<std::array<float, 2>, 4> wheels{{{-0.8f, 1.3f}, {0.8f, 1.3f}, {-0.8f, -1.3f}, {0.8f, -1.3f}}};
    CHECK(aiStripWheels(quad, {2, 0, 39}, right, up, forward, wheels) == 3);   // the front ones 0.3 m past it
    CHECK(aiStripWheels(quad, {2, 0, 38.1f}, right, up, forward, wheels) == 0); // 0.6 m short
    CHECK(aiStripWheels(quad, {2, 0, 41.2f}, right, up, forward, wheels) == 12); // the rear ones on it
    CHECK(aiStripWheels(quad, {7, 0, 38.7f}, right, up, forward, wheels) == 0);  // beside it
    CHECK(aiStripWheels(quad, {5.6f, 0, 38.7f}, right, up, forward, wheels) == 1); // the left front at its end

    // The calls for help (0x4032e5, cop_ai_call_strip 0x4039c0): a cop
    // chasing a racer whose heat (chases lost + cops on it + tickets +
    // time / 2048) is over 6 radios for a strip at its first call, and at
    // a later one (another cop's, or its own after one that only cleared
    // its turn) orders the free cop 51+ slices ahead to the roadside.
    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.maxVelocity = 70;
    AiCar racer;
    racer.slice = 100;
    racer.halfWidth = 1;
    racer.offset = racer.target = 2;
    racer.speed = racer.wanted = 40;
    racer.recordOpen = true;
    racer.chasesLost = 6;
    AiCar chaser;
    chaser.cop = true;
    chaser.racer = false;
    chaser.slice = 98;
    chaser.halfWidth = 1;
    chaser.offset = chaser.target = 2;
    chaser.copState = 5;
    chaser.chasing = 0;
    chaser.siren = true;
    chaser.speed = 40;
    AiCar free = chaser;
    free.slice = 170;
    free.copState = 1;
    free.chasing = -1;
    free.siren = false;
    free.phase = 1;
    free.index = 1;
    std::vector<AiCar> cars{racer, chaser, free};
    aiCopStep(cars, 1, col, data, 400, 1, false);
    CHECK(cars[1].copState == 5 && cars[1].radio == 0x40b910 && (cars[0].calls & 2) && cars[0].caller == 1 &&
          cars[0].callAt == 400 && cars[2].orderKind == -1);
    aiCopStep(cars, 1, col, data, 720, 1, false); // within 320 steps: nothing
    CHECK(cars[0].callAt == 400);
    aiCopStep(cars, 1, col, data, 728, 1, false); // its own turn: only cleared
    CHECK(cars[0].callAt == 728 && cars[0].caller == -1 && cars[2].orderKind == -1);
    aiCopStep(cars, 1, col, data, 1056, 1, false);
    CHECK(cars[2].orderKind == 0 && cars[2].stripWanted && cars[2].watching == 0 && cars[2].orderFrom == 164 &&
          cars[0].stripCop == 2);
    // Cool, it asks nothing: heat 0 + 1 + 0 - 3 + 0 is not over 3.
    std::vector<AiCar> cool{racer, chaser, free};
    cool[0].chasesLost = 0;
    aiCopStep(cool, 1, col, data, 400, 1, false);
    CHECK(cool[0].calls == 0 && cool[1].radio != 0x40b910);

    // The ticket (0x401ff0, cop_after_ticket 0x4040e0): every cop on the
    // racer or watching it lets it go, no chase lost, and patrols.
    std::vector<AiCar> stop{racer, chaser, free};
    stop[1].copState = 6;
    stop[0].recordStep = 10;
    stop[2].watching = 0;
    stop[2].stripWanted = stop[2].stripOn = true;
    stop[0].stripCop = 2;
    aiCopStep(stop, 1, col, data, 400, 1, false);
    CHECK(stop[0].tickets == 1 && stop[0].chasesLost == 0 && stop[0].overspeed == 0 && !stop[0].recordOpen &&
          stop[1].chasing == -1 && stop[1].copState == 1 && !stop[2].stripOn && stop[2].orderKind == 2 &&
          stop[0].stripCop == -1);

    // The model across the road (aiSteerStep: car_ai_model 0x408600).
    // ai_ctl_rails (0x4076b0), lined up at 30 m/s on the lanes: the side
    // speed wanted -5 x (0 - 4) = 20, held to half the speed (15), against
    // none along the right 2 slices on; force 4 x 1.045 x 15, kept to
    // 38 x 1.045; 1/32 s of it.
    AiCar steer;
    steer.slice = 20;
    steer.speed = 30;
    steer.offset = 4;
    steer.halfWidth = 1;
    steer.sideKept = true;
    steer.velSlice = 20;
    aiSteerStep(steer, col, 0);
    CHECK(steer.steer == AiCar::Rails && std::fabs(steer.sideSpeed - 38 * 1.045f / 32) < 1e-4f &&
          std::fabs(steer.side - steer.sideSpeed / 64) < 1e-5f);
    for (int k = 1; k < 192; ++k)
        aiSteerStep(steer, col, k);
    CHECK(std::fabs(steer.side - 4) < 0.1f && std::fabs(steer.sideSpeed) < 0.5f); // settled in 3 s
    // Stopped, it doesn't move across (ai_ctl_slide, nothing wanted at 0).
    AiCar still = steer;
    still.speed = 0;
    still.offset = -4;
    for (int k = 0; k < 128; ++k)
        aiSteerStep(still, col, k);
    CHECK(still.steer == AiCar::Slide && std::fabs(still.side - steer.side) < 0.05f);
    // Slow (ai_ctl_slow under 13.41 m/s), it turns towards the point and
    // goes there, not past the wall (10, less its half width).
    AiCar slow = steer;
    slow.speed = 8;
    slow.offset = 7;
    slow.heading = slow.yawRate = slow.carSide = slow.sideSpeed = 0;
    float most = 0;
    for (int k = 0; k < 64 * 8; ++k) {
        aiSteerStep(slow, col, k);
        most = std::max(most, slow.side);
    }
    CHECK(slow.side > 6 && most <= 9.0f + 1e-4f && slow.heading != 0);
    // A car pushed at the wall stops there.
    AiCar wall = steer;
    wall.offset = 30;
    for (int k = 0; k < 256; ++k)
        aiSteerStep(wall, col, k);
    CHECK(std::fabs(wall.lateral - 9) < 1e-4f);
    // On a bend the velocity is kept as the slice is left: running straight
    // on, the car is carried wide of the turned road and pushed back.
    TrackCollision bent = straightRoad(60);
    for (size_t i = 30; i < bent.road.size(); ++i) {
        const float a = 0.05f * static_cast<float>(i - 29);
        bent.road[i].forward = {std::sin(a), 0, std::cos(a)};
        bent.road[i].right = {std::cos(a), 0, -std::sin(a)};
    }
    AiCar turn = steer;
    turn.slice = 29;
    turn.velSlice = 29;
    turn.side = turn.lateral = turn.offset = 0;
    turn.sideSpeed = 0;
    turn.slice = 30;
    aiSteerStep(turn, bent, 1);
    CHECK(std::fabs(turn.sideSpeed - 30 * std::sin(-0.05f)) < 1e-3f);
}

int main() {
    const stdfs::path tmp = stdfs::temp_directory_path() / "reluva_nfs_tests";
    stdfs::remove_all(tmp);
    testDetection(tmp);
    testFolders(tmp);
    testListings(tmp);
    testRefPack();
    testViv();
    testShapes();
    testCockpit();
    testTextTable();
    testFont();
    testGameFiles(tmp);
    testModel();
    testTrack();
    testLightLook();
    testParticles();
    testPolygonAnimation();
    testReflectionUv();
    testPlacedSounds();
    testHeadlights();
    testViewAndRoadFrame();
    testCarLighting();
    testCarReflection();
    testLightEffects();
    testComputerDriving();
    testComputerCatchUpAndCops();
    testComputerPursuits();
    testCopCalls();
    testSkinPaint();
    testHorizon();
    testCollision();
    testSoundBank();
    testMusic();
    testMovie();
    testForceEffects();
    testSound3d();
    testThrashFog();
    stdfs::remove_all(tmp);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
