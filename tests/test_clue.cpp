// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Clue engine's readers, on files made up here.

#include "engines/clue/game_data.h"
#include "engines/clue/resources.h"
#include "zip_writer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::clue;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
static void put32(Bytes& b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}
static void putText(Bytes& b, const char* s) {
    b.push_back(uint8_t(std::strlen(s)));
    b.insert(b.end(), s, s + std::strlen(s));
}

// A zlib stream holding the data in one stored (uncompressed) block.
static Bytes zlibStored(const Bytes& data) {
    Bytes z = {0x78, 0x01, 0x01};
    put16(z, uint32_t(data.size()));
    put16(z, uint32_t(~data.size() & 0xffff));
    z.insert(z.end(), data.begin(), data.end());
    uint32_t a = 1, b = 0;
    for (uint8_t v : data) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    const uint32_t adler = b << 16 | a;
    for (int s = 24; s >= 0; s -= 8)
        z.push_back(uint8_t(adler >> s));
    return z;
}

static Bytes pack(const Bytes& data) {
    const Bytes z = zlibStored(data);
    Bytes p = {'P', 'A', 'C', 'K'};
    put32(p, uint32_t(z.size()));
    put32(p, uint32_t(data.size()));
    p.insert(p.end(), z.begin(), z.end());
    return p;
}

// A DDRV archive of named records, its directory in reverse name order as
// the game's aren't in file order either.
static Bytes drive(const std::vector<std::pair<std::string, Bytes>>& files) {
    Bytes d = {'D', 'D', 'R', 'V', 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint32_t> offsets;
    for (const auto& f : files) {
        offsets.push_back(uint32_t(d.size()));
        d.insert(d.end(), f.second.begin(), f.second.end());
    }
    const uint32_t directory = uint32_t(d.size());
    for (size_t i = files.size(); i-- > 0;) {
        put32(d, 0);
        put32(d, 0x01bddc75); // a FILETIME
        put32(d, 0xad5a42e0);
        put32(d, offsets[i]);
        put32(d, uint32_t(files[i].second.size()));
        Bytes name(128, 0);
        std::memcpy(name.data(), files[i].first.data(), files[i].first.size());
        d.insert(d.end(), name.begin(), name.end());
    }
    put32(d, uint32_t(files.size()));
    const uint32_t count = uint32_t(files.size());
    std::memcpy(d.data() + 4, &directory, 4);
    std::memcpy(d.data() + 8, &count, 4);
    return d;
}

// A version 0x200 EIMG: 3x2, row 0 a skip of 1 then red and green, row 1
// blue then nothing.
static Bytes image200() {
    const Bytes pixels = {0x81, 0x02, 0x00, 0xf8, 0xe0, 0x07, 0x80, 0x01, 0x1f, 0x00, 0x80};
    Bytes r = {'E', 'I', 'M', 'G'};
    put32(r, 0x200);
    put32(r, uint32_t(45 + pixels.size()));
    put32(r, 2);
    put32(r, 3);
    put32(r, 2);
    r.push_back(3);
    put32(r, 8);
    r.insert(r.end(), 12, 0);
    put32(r, 3);
    put32(r, 2);
    put32(r, 0xff00);
    put32(r, uint32_t(pixels.size()));
    r.insert(r.end(), pixels.begin(), pixels.end());
    return r;
}

// A version 0x100 EIMG: a 2x1 white glyph at (1, 3) in a 5x8 cell; its
// rows end with 0.
static Bytes image100() {
    const Bytes pixels = {0x02, 0xff, 0xff, 0xff, 0xff, 0x00};
    Bytes r = {'E', 'I', 'M', 'G'};
    put32(r, 0x100);
    put32(r, uint32_t(40 + pixels.size()));
    put16(r, 2);
    put16(r, 1);
    put32(r, 16);
    put32(r, 2);
    r.insert(r.end(), 12, 0);
    put16(r, 1);
    put16(r, 3);
    put16(r, 5);
    put16(r, 8);
    put32(r, 0xff00);
    put32(r, uint32_t(pixels.size()));
    r.insert(r.end(), pixels.begin(), pixels.end());
    return r;
}

// An ANIM record of 2 frames, 2 layers and 2 images, with an ISOA block in
// the long form (one extra on frame 1) or the short one.
static Bytes animation(bool shortForm) {
    Bytes r = {'A', 'N', 'I', 'M'};
    put16(r, 0x200);
    r.push_back(0x80);
    r.insert(r.end(), 128, 0xcd); // unnamed
    r.insert(r.end(), {6, 2, 2});  // kind, kind, layers
    put16(r, 2);                   // frames
    put16(r, 2);                   // images
    put16(r, 75);
    put16(r, 120);
    put32(r, 10);
    put32(r, 136);
    put32(r, 4 + 2 * 2 * 10);
    putText(r, "test:Ia");
    putText(r, "test:Ib");
    for (int layer = 0; layer < 2; ++layer) {
        put32(r, uint32_t(layer));
        for (int frame = 0; frame < 2; ++frame) {
            put16(r, uint32_t(layer));                  // image
            put16(r, uint32_t(layer * 10 + frame));     // x
            put16(r, uint32_t(-5 & 0xffff));            // y
            put16(r, 0);
            put16(r, 0);
        }
    }
    r.insert(r.end(), {'I', 'S', 'O', 'A'});
    Bytes isoa;
    for (int frame = 0; frame < 2; ++frame) {
        put16(isoa, uint32_t(-44 & 0xffff));
        put16(isoa, uint32_t(-106 & 0xffff));
        if (shortForm) {
            isoa.insert(isoa.end(), 6, 0);
            continue;
        }
        for (int i = 0; i < 8; ++i)
            put16(isoa, uint32_t(i));
        put16(isoa, 100 + uint32_t(frame));
        put16(isoa, uint32_t(frame)); // extras
        if (frame == 1)
            isoa.insert(isoa.end(), {0x88, 0x13, 0, 0, 0, 0, 0});
    }
    put32(r, uint32_t(isoa.size()));
    r.insert(r.end(), isoa.begin(), isoa.end());
    return r;
}

static void testUnpack() {
    const Bytes data = {'h', 'e', 'l', 'l', 'o'};
    Bytes out;
    CHECK(!unpack(pack(data), out) && out == data);
    CHECK(!unpack(data, out) && out == data); // not packed: as it is
    Bytes damaged = pack(data);
    damaged.back() ^= 1; // the Adler-32
    CHECK(unpack(damaged, out));
    Bytes cut = pack(data);
    cut.resize(cut.size() - 3);
    CHECK(unpack(cut, out));
}

static void testDriveAndImages() {
    DataDrive d;
    CHECK(!d.load(drive({{"Ired", image200()}, {"Iglyph", image100()}})));
    CHECK(d.entries().size() == 2);
    const DataDrive::Entry* e = d.find("IRED");
    CHECK(e && e->name == "Ired" && e->time == 0xad5a42e001bddc75ull);
    Image img;
    CHECK(e && !decodeImage(d.read(*e), img));
    CHECK(img.version == 0x200 && img.pixels.w == 3 && img.pixels.h == 2 && img.cellW == 3);
    const auto px = [&](int x, int y) {
        const uint8_t* p = img.pixels.pixel(x, y);
        return std::vector<int>{p[0], p[1], p[2], p[3]};
    };
    CHECK(px(0, 0) == std::vector<int>({0, 0, 0, 0}));     // skipped
    CHECK(px(1, 0) == std::vector<int>({255, 0, 0, 255})); // 0xf800
    CHECK(px(2, 0) == std::vector<int>({0, 255, 0, 255})); // 0x07e0
    CHECK(px(0, 1) == std::vector<int>({0, 0, 255, 255})); // 0x001f
    CHECK(px(1, 1)[3] == 0);

    e = d.find("Iglyph");
    CHECK(e && !decodeImage(d.read(*e), img));
    CHECK(img.version == 0x100 && img.pixels.w == 2 && img.pixels.h == 1);
    CHECK(img.cellW == 5 && img.cellH == 8 && img.offsetX == 1 && img.offsetY == 3);
    CHECK(px(1, 0) == std::vector<int>({255, 255, 255, 255}));

    // A row running past the width, and data cut short.
    Bytes wide = image200();
    wide[57] = 0x83; // skip 3, then 2 pixels: past a width of 3
    CHECK(decodeImage(wide, img));
    Bytes cut = image200();
    cut.resize(cut.size() - 4);
    CHECK(decodeImage(cut, img));
    CHECK(decodeImage(animation(false), img)); // not an image

    CHECK(d.load({'D', 'D', 'R', 'V', 0xff, 0xff, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}));
    // A directory in the last 4 bytes, and one too short for its entries.
    CHECK(d.load({'D', 'D', 'R', 'V', 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(d.load({'D', 'D', 'R', 'V', 12, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}));
    CHECK(!d.load({'D', 'D', 'R', 'V', 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}) && d.entries().empty());
}

static void testAnimations() {
    Animation a;
    CHECK(!parseAnimation(animation(false), a));
    CHECK(a.name.empty() && a.kind[0] == 6 && a.kind[1] == 2 && a.width == 75 && a.height == 120);
    CHECK(a.images.size() == 2 && a.images[1] == "test:Ib");
    CHECK(a.layers.size() == 2 && a.frameCount() == 2 && a.isometric && !a.shortForm);
    CHECK(a.layers[1][1].image == 1 && a.layers[1][1].x == 11 && a.layers[1][1].y == -5);
    CHECK(a.frames[0].anchorX == -44 && a.frames[0].anchorY == -106 && a.frames[1].duration == 101);
    CHECK(a.frames[0].words[7] == 7 && a.frames[0].extras.empty() && a.frames[1].extras.size() == 1);
    CHECK(a.frames[1].extras.size() == 1 && a.frames[1].extras[0][0] == 0x88);

    CHECK(!parseAnimation(animation(true), a));
    CHECK(a.isometric && a.shortForm && a.frames[1].anchorY == -106 && a.frames[1].duration == 0);

    Bytes bad = animation(false);
    bad.resize(bad.size() - 3);
    CHECK(parseAnimation(bad, a));
    bad = animation(false);
    // The first layer's first part names image 9 of 2.
    const size_t firstPart = 4 + 2 + 1 + 128 + 3 + 20 + 8 + 8 + 4;
    bad[firstPart] = 9;
    CHECK(parseAnimation(bad, a));
    // 0xffff is a part showing nothing.
    bad[firstPart] = 0xff;
    bad[firstPart + 1] = 0xff;
    CHECK(!parseAnimation(bad, a) && a.layers[0][0].image == Animation::kNoImage);

    ResourceName n;
    CHECK(splitResourceName("c_scarlet:Imsidlee_01", n) && n.drive == "c_scarlet" && n.name == "Imsidlee_01");
    CHECK(!splitResourceName("nocolon", n) && !splitResourceName(":Ix", n) && !splitResourceName("x:", n));
}

static void testGameData(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    // An install: Data/Classic with its folders, and a newer copy of a file
    // in the game folder itself, which wins.
    const auto classic = tmp / "install" / "Data" / "Classic";
    test::writeFile(classic / "Resource" / "test.res", pack(drive({{"Ia", image200()}, {"Ib", image100()}})));
    test::writeFile(classic / "Resource" / "test.a", pack(drive({{"Awalk", animation(false)}})));
    test::writeFile(classic / "Text" / "Setup.txt", {'o', 'l', 'd'});
    test::writeFile(tmp / "install" / "setup.TXT", {'n', 'e', 'w'});
    GameData data;
    CHECK(!data.open(tmp / "install", Language::US));
    const std::filesystem::path* setup = data.find("SETUP.txt");
    CHECK(setup && setup->filename() == "setup.TXT");
    CHECK(data.withExtension(".res").size() == 1);
    const Image* img = data.image("test:Ia");
    CHECK(img && img->pixels.w == 3);
    CHECK(data.image("TEST:IA") == img); // kept, and found ignoring case
    CHECK(!data.image("test:Inothere") && !data.image("nothere:Ia") && !data.image("bad name"));
    const Animation* a = data.animation("test:Awalk");
    CHECK(a && a->frameCount() == 2 && data.image(a->images[1]));

    // The CD: the language's folder, then the shared ones.
    const auto cd = tmp / "cd" / "Data";
    test::writeFile(cd / "US" / "Text" / "Setup.txt", {'U', 'S'});
    test::writeFile(cd / "UK" / "Text" / "Setup.txt", {'U', 'K'});
    test::writeFile(cd / "LangIndepend" / "Resource" / "test.res", pack(drive({{"Ia", image200()}})));
    CHECK(!data.open(tmp / "cd", Language::UK));
    setup = data.find("setup.txt");
    CHECK(setup && setup->parent_path().parent_path().filename() == "UK");
    CHECK(data.image("test:Ia"));
    CHECK(!data.open(tmp / "cd", Language::US));
    setup = data.find("setup.txt");
    CHECK(setup && setup->parent_path().parent_path().filename() == "US");

    CHECK(data.open(tmp / "nothing", Language::US));
    std::filesystem::remove_all(tmp);
}

int main() {
    testUnpack();
    testDriveAndImages();
    testAnimations();
    testGameData(std::filesystem::temp_directory_path() / "reluva_clue_tests");
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
