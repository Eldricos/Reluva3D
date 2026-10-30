// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Clue engine's readers, on files made up here, and its rules.

#include "engines/clue/ai.h"
#include "engines/clue/board.h"
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include "engines/clue/resi.h"
#include "engines/clue/ui.h"
#include "engines/clue/resources.h"
#include "engines/clue/rules.h"
#include "zip_writer.h"

#include <cstdio>
#include <algorithm>
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
static Bytes animation(bool shortForm, int solid = 1, int sizeX = 16, int sizeY = 16) {
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
        for (int i = 0; i < 5; ++i)
            put16(isoa, uint32_t(i));
        put16(isoa, uint32_t(solid & 0xffff));
        put16(isoa, uint32_t(sizeX)); // the box on the floor
        put16(isoa, uint32_t(sizeY));
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
    CHECK(a.frames[0].anchorX == -44 && a.frames[0].anchorY == -106 && a.frames[1].height == 101);
    CHECK(a.frames[0].sizeX == 16 && a.frames[0].sizeY == 16 && a.frames[0].solid == 1 && a.framesPerSecond == 10);
    CHECK(a.frames[0].unknown[4] == 4 && a.frames[0].extras.empty() && a.frames[1].extras.size() == 1);
    CHECK(a.frames[1].extras.size() == 1 && a.frames[1].extras[0][0] == 0x88);

    CHECK(!parseAnimation(animation(true), a));
    CHECK(a.isometric && a.shortForm && a.frames[1].anchorY == -106 && a.frames[1].height == 0);

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

// An object record: its states and timings.
static Bytes objectRecord(const char* name, uint8_t type, const std::vector<std::pair<int, const char*>>& states,
                          const std::vector<std::pair<int, int>>& timings = {}) {
    Bytes r;
    put16(r, 0); // the length, filled in below
    r.push_back(1);
    Bytes field(32, 0);
    std::memcpy(field.data(), name, std::strlen(name));
    field[std::strlen(name) + 1] = 0x28; // what memory held after the name
    r.insert(r.end(), field.begin(), field.end());
    r.push_back(type);
    put32(r, 1);
    put16(r, uint32_t(states.size()));
    for (const auto& [id, anim] : states) {
        put16(r, uint32_t(id));
        Bytes a(32, 0);
        std::memcpy(a.data(), anim, std::strlen(anim));
        r.insert(r.end(), a.begin(), a.end());
    }
    put16(r, uint32_t(timings.size()));
    for (const auto& [time, state] : timings) {
        put16(r, uint32_t(time));
        r.push_back(0);
        put16(r, uint32_t(state));
        put16(r, 0);
    }
    r[0] = uint8_t(r.size());
    r[1] = uint8_t(r.size() >> 8);
    r.insert(r.end(), {0x4e, 0x00}); // the archive's size takes in the next record's first 2 bytes
    return r;
}

struct TestPlacement {
    uint8_t flags;
    int tag, object, x, y, id, layer;
    std::vector<std::pair<int, int>> timings;
};

// clue.lvl: World.dat's names and Objects.dat's placements.
static Bytes levelFile(const std::vector<const char*>& names, const std::vector<TestPlacement>& placements) {
    Bytes world(40, 0);
    std::memcpy(world.data(), "Unnamed", 7);
    put16(world, uint32_t(names.size()));
    for (const char* n : names)
        putText(world, n);
    Bytes objects;
    put16(objects, uint32_t(placements.size()));
    for (const TestPlacement& p : placements) {
        Bytes r;
        r.push_back(p.flags);
        if (p.flags & 0x02)
            put16(r, uint32_t(p.tag));
        put16(r, uint32_t(p.object));
        put16(r, uint32_t(p.x & 0xffff));
        put16(r, uint32_t(p.y & 0xffff));
        put16(r, uint32_t(p.id & 0xffff));
        r.push_back(uint8_t(p.layer));
        if (p.flags & 0x20) {
            put16(r, uint32_t(p.timings.size()));
            for (const auto& [time, state] : p.timings) {
                put16(r, uint32_t(time));
                r.push_back(0);
                put16(r, uint32_t(state));
                put16(r, 0);
            }
        }
        put16(objects, uint32_t(r.size() + 2));
        objects.insert(objects.end(), r.begin(), r.end());
    }
    return drive({{"/World.dat", world}, {"/Info.dat", {0, 0}}, {"/Objects.dat", objects},
                  {"/Workspace.dat", Bytes(32, 0)}});
}

static void testObjectsAndLevel() {
    ObjectDef o;
    CHECK(!parseObject(objectRecord("test:Obench", 3, {{0, "test:Awalk"}, {21, "test:Awalk"}}, {{1600, 21}}), o));
    CHECK(o.name == "test:Obench" && o.type == 3 && o.unknown == 1 && o.states.size() == 2);
    CHECK(o.state(21) && o.state(21)->animation == "test:Awalk" && !o.state(5));
    CHECK(o.timings.size() == 1 && o.timings[0].time == 1600 && o.timings[0].state == 21);
    Bytes cut = objectRecord("test:Ox", 0, {{0, "test:Awalk"}});
    cut[0] += 7; // longer than it is
    CHECK(parseObject(cut, o));
    cut = objectRecord("test:Ox", 0, {{0, "test:Awalk"}});
    cut[0] -= 1; // a byte left over
    CHECK(parseObject(cut, o));

    DataDrive d;
    CHECK(!d.load(levelFile({"test:Ofloor", "test:Obench"},
                            {{0x40, 0, 1, 100, 60, 1, 5, {}},
                             {0x62, 3, 0, -64, 128, -1, 1, {{1000, 10}}},
                             {0x42, 1030, 1, 436, 948, 1, 7, {}}})));
    Level level;
    CHECK(!parseLevel(d, level));
    CHECK(level.title == "Unnamed" && level.objects.size() == 2 && level.placements.size() == 3);
    if (level.placements.size() == 3) {
        const Placement& p = level.placements[1];
        CHECK(p.flags == 0x62 && p.tag == 3 && p.object == 0 && p.x == -64 && p.y == 128 && p.id == -1 &&
              p.layer == 1);
        CHECK(p.timings.size() == 1 && p.timings[0].time == 1000 && p.timings[0].state == 10);
        CHECK(level.placements[2].tag == 1030 && level.placements[2].layer == 7);
        CHECK(level.workspace.size() == 32);
    }
    // A placement naming an object that isn't there.
    CHECK(!d.load(levelFile({"test:Ofloor"}, {{0x40, 0, 4, 0, 0, 1, 5, {}}})));
    CHECK(parseLevel(d, level));
}

static void testDepth() {
    // Separate in x or y: the further on is in front.
    const WorldBox a{0, 0, 16, 16, 50}, beyondX{16, 0, 16, 16, 50}, beyondY{0, 20, 16, 16, 50};
    CHECK(compareDepth(beyondX, a) == 1 && compareDepth(a, beyondX) == -1);
    CHECK(compareDepth(beyondY, a) == 1 && compareDepth(a, beyondY) == -1);
    CHECK(compareDepth(a, WorldBox{8, 8, 16, 16, 50}) == 0);

    // A long wall in front of a small object whose centre is nearer the
    // camera than the wall's: the boxes say the wall goes last.
    const WorldBox wall{0, 48, 128, 16, 100}, thing{100, 20, 16, 16, 50}, far{1000, 1000, 16, 16, 50};
    const ScreenRect over{0, 0, 100, 100}, away{5000, 5000, 10, 10};
    std::vector<size_t> order = depthOrder({wall, thing, far}, {over, over, away});
    CHECK(order.size() == 3);
    const auto pos = [&](size_t i) { return std::find(order.begin(), order.end(), i) - order.begin(); };
    CHECK(pos(1) < pos(0));

    // Three boxes each in front of the next make a cycle; each still comes
    // out once.
    order = depthOrder({{0, 0, 10, 40, 10}, {10, 0, 40, 10, 10}, {5, 10, 10, 10, 10}}, {over, over, over});
    std::sort(order.begin(), order.end());
    CHECK(order == std::vector<size_t>({0, 1, 2}));
}

static void testMansion(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    const auto res = tmp / "Data" / "Classic" / "Resource";
    test::writeFile(res / "test.res", pack(drive({{"Ia", image200()}, {"Ib", image100()}})));
    test::writeFile(res / "test.a", pack(drive({{"Awalk", animation(false)}, {"Afloor", animation(false, -1, 64, 64)}})));
    test::writeFile(res / "test.o", drive({{"Obench", objectRecord("test:Obench", 3, {{0, "test:Awalk"}})},
                                          {"Ofloor", objectRecord("test:Ofloor", 1, {{0, "test:Afloor"}})},
                                          {"Obroken", objectRecord("test:Obroken", 3, {{0, "test:Anothere"}})}}));
    test::writeFile(tmp / "Data" / "Classic" / "Levels" / "clue.lvl",
                    levelFile({"test:Obench", "test:Ofloor", "test:Obroken"},
                              {{0x40, 0, 0, 200, 100, 1, 5, {}},
                               {0x40, 0, 1, 0, 0, -1, 1, {}},
                               {0x40, 0, 2, 0, 0, 1, 5, {}}}));
    GameData data;
    CHECK(!data.open(tmp, Language::US));
    DataDrive file;
    Level level;
    Mansion m;
    CHECK(!file.open(*data.find("clue.lvl")) && !parseLevel(file, level) && !m.build(data, level));
    CHECK(m.items().size() == 2 && m.problems().size() == 1); // the broken one is left out
    if (m.items().size() == 2) {
        const Mansion::Item& bench = m.items()[0];
        CHECK(!bench.flat && bench.box.x == 200 && bench.box.y == 100 && bench.box.sizeX == 16);
        CHECK(bench.anchorX == 100 && bench.anchorY == 150); // x - y, (x + y) / 2
        CHECK(m.items()[1].flat);
        CHECK(m.drawOrder() == std::vector<size_t>({1, 0})); // the floor first
        // Frame 0's pieces: image Ia (3x2) at the frame's anchor offset plus its layer's.
        CHECK(bench.rect.x == 100 - 44 && bench.rect.y == 150 - 106 - 5);
        // Actors go into the order by their boxes: before the bench when
        // behind it, after it when in front; on the floor,
        // after the floors and before everything else.
        Mansion::Actor actor;
        actor.frame = &(*bench.frames)[0];
        actor.box = {199, 100, 16, 16, 100}; // overlapping it on screen (its picture is 3x2)
        CHECK(m.insertionPoint(actor) == 1);
        actor.box = {202, 101, 16, 16, 100};
        CHECK(m.insertionPoint(actor) == 2);
        actor.flat = true;
        CHECK(m.insertionPoint(actor) == 1);
        Surface s;
        s.create(640, 480, PixelFormat::RGBA32);
        MansionLook look;
        look.characters = false;
        m.draw(s, 0, 0, look, 0, {actor}); // drawn, whatever its place
    }
    std::filesystem::remove_all(tmp);
}

// A RESI string: its length, the characters and a NUL (nothing for "").
static void putResiText(Bytes& b, const std::string& s) {
    b.push_back(uint8_t(s.size()));
    if (s.empty())
        return;
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

// A control: its 112-byte block, its two strings and its list's items.
static void putControl(Bytes& b, int type, int id, int l, int t, int r, int bot, int centreX, int font, int group,
                       const std::string& text, const std::string& anim, const std::vector<std::string>& items = {}) {
    std::array<int32_t, 28> v{};
    v[0] = type;
    v[1] = id;
    v[2] = l;
    v[3] = t;
    v[4] = r;
    v[5] = bot;
    v[6] = 0x01bbaa28; // pointers the file holds as they were
    v[9] = centreX;
    v[10] = 1;
    v[14] = font;
    v[15] = font;
    v[16] = group;
    v[19] = 55;
    v[22] = -1;
    for (int32_t x : v)
        put32(b, uint32_t(x));
    putResiText(b, text);
    putResiText(b, anim);
    put16(b, uint32_t(items.size()));
    for (const auto& item : items)
        putResiText(b, item);
}

static Bytes resiFile() {
    Bytes b = {'R', 'E', 'S', 'I', 1, 1}; // a dialog, a font
    put32(b, 6);
    putResiText(b, "largeu:Alargeu");
    put32(b, 0);
    put32(b, 6);
    for (int i = 0; i < 9; ++i)
        put32(b, i == 0 ? 7 : i == 6 ? 6 : 0);
    putResiText(b, "menu:AexitBgnd");
    b.push_back(4);
    putControl(b, 1, 9999, 180, 120, 458, 183, 1, 6, 0, "Are you sure you wish|to exit Clue?", "");
    putControl(b, 3, 10001, 260, 185, 380, 220, 1, 6, 0, "Yes", "menu:AexitYesBtn");
    putControl(b, 3, 10000, 260, 228, 380, 260, 1, 6, 1, "No", "menu:AexitNoBtn");
    putControl(b, 5, 10, 210, 258, 380, 0, 0, 6, 0, "", "", {"one", "two"});
    return b;
}

static void testResi() {
    Resi resi;
    CHECK(!parseResi(resiFile(), resi));
    CHECK(resi.fonts.size() == 1 && resi.fonts[0].id == 6 && resi.fonts[0].animation == "largeu:Alargeu" &&
          resi.fonts[0].lineSpacing == 6);
    CHECK(resi.font(6) && !resi.font(1));
    const ResiDialog* d = resi.dialog(7);
    CHECK(d && d->background == "menu:AexitBgnd" && d->controls.size() == 4 && !resi.dialog(1));
    if (d && d->controls.size() == 4) {
        const ResiControl* yes = d->control(10001);
        CHECK(yes && yes->type == ResiControl::TextButton && yes->text == "Yes" && yes->animation == "menu:AexitYesBtn");
        CHECK(yes && yes->left == 260 && yes->top == 185 && yes->right == 380 && yes->centreX == 1 && yes->font == 6);
        CHECK(d->controls[0].text == "Are you sure you wish|to exit Clue?" && d->controls[0].animation.empty());
        CHECK(d->controls[2].group == 1 && d->controls[3].items == std::vector<std::string>({"one", "two"}));
    }
    Bytes cut = resiFile();
    cut.resize(cut.size() - 5);
    CHECK(parseResi(cut, resi));
    Bytes more = resiFile();
    more.push_back(0);
    CHECK(parseResi(more, resi));
    CHECK(parseResi({'R', 'I', 'F', 'F', 0, 0}, resi));
    CHECK(splitLines("a|b\r\nc") == std::vector<std::string>({"a", "b", "c"}));
}

static void testDialog(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "Data" / "Classic");
    GameData data;
    CHECK(!data.open(tmp, Language::US));
    Resi resi;
    CHECK(!parseResi(resiFile(), resi));
    const Fonts fonts(data, resi); // its animation isn't there: a font without glyphs
    DialogView view(data, fonts, *resi.dialog(7));
    // Group 1 starts hidden; the game shows it.
    CHECK(view.visible(10001) && !view.visible(10000));
    view.setGroupVisible(1, true);
    CHECK(view.visible(10000));
    view.setText(10001, "Oui");
    CHECK(view.text(10001) == "Oui" && view.text(9999) == "Are you sure you wish|to exit Clue?");
    // A click is a press and a release on the same button.
    const auto mouse = [&](EventType type, int x, int y) {
        Event e;
        e.type = type;
        e.mouseX = x;
        e.mouseY = y;
        e.button = MouseButton::Left;
        return view.handleEvent(e);
    };
    CHECK(mouse(EventType::MouseDown, 300, 200) == -1 && mouse(EventType::MouseUp, 300, 200) == 10001);
    CHECK(mouse(EventType::MouseDown, 300, 200) == -1 && mouse(EventType::MouseUp, 300, 240) == -1);
    view.setEnabled(10001, false);
    CHECK(mouse(EventType::MouseDown, 300, 200) == -1 && mouse(EventType::MouseUp, 300, 200) == -1);
    // Typing goes to the edit box that has the focus.
    view.focus(10);
    Event typed;
    typed.type = EventType::TextInput;
    std::snprintf(typed.text, sizeof typed.text, "Jo\xc3\xa9"); // é, kept as Windows-1252
    view.handleEvent(typed);
    CHECK(view.text(10) == "Jo\xe9");
    Event back;
    back.type = EventType::KeyDown;
    back.key = key::Backspace;
    view.handleEvent(back);
    CHECK(view.text(10) == "Jo");
    Surface s;
    s.create(640, 480, PixelFormat::RGBA32);
    view.draw(s); // without its pictures: nothing to draw, nothing to fail
    std::filesystem::remove_all(tmp);
}

// A small mansion: two rows of corridor (y 2 and 3, x 0-26) and, above
// them, the nine rooms, 2 x 2 each, three columns apart, each with a door
// down to the corridor at its left column. The suspects start on row 3,
// five apart; the kitchen and the study have the secret passages.
static Level testBoardLevel() {
    Level level;
    const auto object = [&](const std::string& name) {
        for (size_t i = 0; i < level.objects.size(); ++i)
            if (level.objects[i] == name)
                return uint16_t(i);
        level.objects.push_back(name);
        return uint16_t(level.objects.size() - 1);
    };
    const auto place = [&](const std::string& name, int x, int y, int layer) {
        Placement p;
        p.object = object(name);
        p.x = int16_t(x);
        p.y = int16_t(y);
        p.layer = uint8_t(layer);
        level.placements.push_back(p);
    };
    const auto at = [](int square) { return 16 + 64 * square; };
    for (int x = 0; x <= 26; ++x)
        for (int y : {2, 3})
            place("r_hwfloor:Ohwfloor", at(x), at(y), 0);
    static const char* const kFolders[kRoomCount] = {"hallroom", "lounge", "diningroom", "kitchen", "ballroom",
                                                     "conservatory", "billiardroom", "library", "study"};
    for (int r = 0; r < kRoomCount; ++r) {
        for (int x = 3 * r; x < 3 * r + 2; ++x)
            for (int y : {0, 1})
                place("r_woodfloor:Owoodfloor", at(x), at(y), 1);
        place(std::string(kFolders[r]) + ":Ochair", at(3 * r + 1) + 8, at(0) + 8, 5);
        place("wall:Ohwdrtop_na", at(3 * r), at(1) + 48, 3);
    }
    place("secretpass:Oktsp", at(9) + 8, at(0) + 8, 5);
    place("secretpass:Osp_w", at(24) + 8, at(0) + 8, 5);
    static const char* const kNames[kSuspectCount] = {"scarlet", "mustard", "white", "green", "peacock", "plum"};
    for (int s = 0; s < kSuspectCount; ++s)
        place(std::string("character:O") + kNames[s], at(5 * s) + 24, at(3) + 24, 7);
    return level;
}

static void testBoard() {
    Board board;
    CHECK(!board.build(testBoardLevel()));
    CHECK(board.corridor().size() == 54);
    CHECK(board.doors().size() == size_t(kRoomCount));
    for (int r = 0; r < kRoomCount; ++r) {
        CHECK(board.roomSquares(r).size() == 4);
        CHECK((board.roomAt({3 * r + 1, 1}) == r));
    }
    CHECK((board.roomAt({2, 0}) == kNoRoom && board.isCorridor({2, 2}) && !board.isCorridor({2, 1})));
    CHECK((board.doors()[0].room == Hall && board.doors()[0].inside == Square{0, 1} &&
          board.doors()[0].outside == Square{0, 2}));
    CHECK((board.start(Plum) == Square{25, 3}));
    CHECK(board.passage(Kitchen) == Study && board.passage(Study) == Kitchen && board.passage(Hall) == kNoRoom);
    // The chair's square comes last of a room's places to stand.
    CHECK((board.standing(Lounge).back() == Square{4, 0}));
    int wx, wy;
    Board::standingPoint({29, 11}, wx, wy);
    CHECK(wx == 1896 && wy == 744); // where clue.lvl stands Miss Scarlet
    CHECK((Board::squareAt(1896, 744) == Square{29, 11} && Board::squareAt(15, 15) == Square{-1, -1}));
    // Steps: along the corridor, in at a door, out of a room by its doors.
    const auto n = board.neighbours({{3, 2}, kNoRoom});
    CHECK((n.size() == 4 && std::count(n.begin(), n.end(), Place{{}, Lounge}) == 1));
    CHECK((board.neighbours({{}, Lounge}) == std::vector<Place>({{{3, 2}, kNoRoom}})));
    // Without one of the rooms the board can't be made.
    Level partial = testBoardLevel();
    partial.placements.erase(std::remove_if(partial.placements.begin(), partial.placements.end(),
                                            [&](const Placement& p) {
                                                return partial.objects[p.object] == "study:Ochair";
                                            }),
                             partial.placements.end());
    CHECK(board.build(partial));
}

static void testRules() {
    Board board;
    CHECK(!board.build(testBoardLevel()));
    Game game;
    CHECK((game.start(board, {true, true, false, false, false, false}, false, 1))); // two aren't enough
    const std::array<bool, kSuspectCount> three{true, false, true, false, true, false};
    CHECK(!game.start(board, three, false, 7));
    // The deal: 18 cards, six each, none of them the case file's.
    size_t dealt = 0;
    for (int s = 0; s < kSuspectCount; ++s) {
        dealt += game.seat(s).hand.size();
        CHECK(game.seat(s).hand.size() == (three[size_t(s)] ? 6u : 0u));
        for (int c : game.seat(s).hand)
            CHECK(!game.solution().has(c));
    }
    CHECK(dealt == 18);
    Game same;
    same.start(board, three, false, 7);
    CHECK(same.solution() == game.solution() && same.seat(White).hand == game.seat(White).hand);

    // Miss Scarlet first: she can only roll (no passage, no room).
    CHECK(game.turn() == Scarlet && game.phase() == Game::Phase::Start);
    CHECK(game.canRoll() && !game.canSuggest() && !game.canTakePassage() && game.canAccuse() && !game.canEndTurn());
    const int roll = game.rollDie();
    CHECK(roll >= 1 && roll <= 6 && game.phase() == Game::Phase::Moving && !game.canRoll());
    // From (0, 3): up to (0, 2), then along row 2. Into the hall from (0, 2).
    std::vector<Place> path{{{0, 2}, kNoRoom}};
    for (int x = 1; int(path.size()) < roll; ++x)
        path.push_back({{x, 2}, kNoRoom});
    CHECK(game.canWalk(path));
    if (roll > 1) {
        CHECK((game.canWalk({{{0, 2}, kNoRoom}, {{}, Hall}}))); // a room ends the move early
        std::vector<Place> shortPath(path.begin(), path.end() - 1);
        CHECK(!game.canWalk(shortPath)); // short of the die's number
    }
    CHECK((!game.canWalk({{{1, 3}, kNoRoom}, {{0, 3}, kNoRoom}}))); // back to where she stood
    CHECK((!game.canWalk({{{1, 2}, kNoRoom}})));                    // not a neighbour
    const auto steps = game.nextSteps({{{0, 2}, kNoRoom}});
    CHECK((std::count(steps.begin(), steps.end(), Place{{}, Hall}) == (roll > 1 ? 1 : 0)));
    const auto all = game.destinations();
    CHECK(!all.empty());
    for (const auto& d : all)
        CHECK(game.canWalk(d));
    CHECK((!game.walk({{{0, 2}, kNoRoom}, {{}, Hall}}) == (roll > 1)));
    if (roll == 1)
        CHECK((!game.walk({{{0, 2}, kNoRoom}})));
    CHECK(game.phase() == Game::Phase::Moved);
    CHECK(game.canSuggest() == (roll > 1) && game.canEndTurn());
    CHECK(!game.endTurn() && game.turn() == White);

    // No Die Roll: nine moves; suggestions, passages and accusations three.
    Game nd;
    CHECK((!nd.start(board, {true, true, true, false, false, false}, true, 3)));
    CHECK(nd.moves() == 9 && !nd.canRoll() && nd.canMove());
    CHECK((nd.canWalk({{{0, 2}, kNoRoom}}) && nd.canWalk({{{0, 2}, kNoRoom}, {{}, Hall}})));
    CHECK((!nd.walk({{{0, 2}, kNoRoom}, {{}, Hall}}) && nd.moves() == 7 && nd.place(Scarlet) == Place{{}, Hall}));
    // Mr. Green isn't played; he is brought to the hall all the same.
    const Accusation s{Green, Rope, Hall};
    CHECK(nd.canSuggest() && !nd.suggest(Green, Rope) && nd.moves() == 4);
    CHECK((nd.place(Green) == Place{{}, Hall} && nd.suggestion() == s && !nd.canSuggest()));
    // The first to her left with a card named shows one.
    int expect = -1;
    for (int k : {Mustard, White})
        if (expect < 0 && std::any_of(nd.seat(k).hand.begin(), nd.seat(k).hand.end(), [&](int c) { return s.has(c); }))
            expect = k;
    CHECK(nd.disprover() == expect);
    if (expect >= 0) {
        CHECK(nd.phase() == Game::Phase::Disprove && !nd.disproving().empty());
        CHECK(nd.show(roomCard(Study) == nd.disproving()[0] ? -1 : roomCard(Study)));
        CHECK(!nd.show(nd.disproving()[0]) && nd.shown() >= 0 && s.has(nd.shown()));
    }
    CHECK(nd.phase() == Game::Phase::Suggested && nd.canAccuse());
    // Wrong: out, and on to the next. Mustard, then, right: he wins.
    Accusation wrong = nd.solution();
    wrong.room = (wrong.room + 1) % kRoomCount;
    CHECK(!nd.accuse(wrong) && nd.seat(Scarlet).out && nd.turn() == Mustard);
    CHECK(!nd.accuse(nd.solution()) && nd.phase() == Game::Phase::Over && nd.winner() == Mustard);

    // A suspect blocking the only door traps the one inside.
    Game trap;
    trap.start(board, {true, true, true, false, false, false}, true, 5);
    CHECK((!trap.walk({{{0, 2}, kNoRoom}, {{}, Hall}}) && !trap.endTurn()));     // Scarlet into the hall
    CHECK((!trap.walk({{{4, 3}, kNoRoom}, {{3, 3}, kNoRoom}, {{2, 3}, kNoRoom}, {{1, 3}, kNoRoom},
                      {{1, 2}, kNoRoom}, {{0, 2}, kNoRoom}}) && !trap.endTurn())); // Mustard before its door
    while (trap.turn() != Scarlet)
        CHECK(!trap.walk(trap.destinations().front()) && !trap.endTurn());
    CHECK(trap.trapped() && !trap.canMove() && trap.canEndTurn() && trap.destinations().empty());
    // Two suspects in a room stand apart.
    CHECK(trap.standingSquare(Scarlet) == board.standing(Hall)[0]);
}

// The notebook's deductions, on a made-up deal.
static void testNotebook() {
    Board board;
    CHECK(!board.build(testBoardLevel()));
    Game game;
    CHECK(!game.start(board, {true, true, true, false, false, false}, false, 9));
    const Accusation& answer = game.solution();
    Notebook n;
    n.reset(game, Scarlet, true, true);
    // Its own cards; the seats not playing hold nothing.
    for (int c : game.seat(Scarlet).hand)
        CHECK(n.holder(c) == Scarlet && n.outOfCaseFile(c));
    CHECK(n.mark(0, White + 1) == Notebook::No);
    CHECK(!n.complete());
    // Mustard shown holding a card: nobody else has it.
    const int m = game.seat(Mustard).hand[0];
    n.holds(Mustard, m);
    CHECK(n.holder(m) == Mustard && n.mark(m, White) == Notebook::No && n.outOfCaseFile(m));
    // White, who showed one of three without it being seen, two of them
    // being known to be elsewhere: the third is hers.
    const int w = game.seat(White).hand[0];
    std::array<int, 3> three{w, game.seat(Scarlet).hand[0], m};
    std::sort(three.begin(), three.end());
    n.holdsOneOf(White, three);
    CHECK(n.holder(w) == White);
    // Told everything but the answer's: the answer is found.
    for (int s : {Mustard, White})
        for (int c : game.seat(s).hand)
            n.holds(s, c);
    CHECK(n.complete() && n.solved(0) == suspectCard(answer.suspect) && n.solved(1) == weaponCard(answer.weapon) &&
          n.solved(2) == roomCard(answer.room));
    // Counting: a player whose hand is all found holds nothing else; one
    // lacking all cards but as many as they hold, holds those.
    Notebook c;
    c.reset(game, Scarlet, true, true);
    for (int card : game.seat(White).hand)
        c.holds(White, card);
    for (int card = 0; card < kCardCount; ++card)
        if (std::find(game.seat(White).hand.begin(), game.seat(White).hand.end(), card) == game.seat(White).hand.end())
            CHECK(c.mark(card, White) == Notebook::No);
    Notebook d;
    d.reset(game, Scarlet, true, true);
    for (int card = 0; card < kCardCount; ++card)
        if (std::find(game.seat(Mustard).hand.begin(), game.seat(Mustard).hand.end(), card) ==
            game.seat(Mustard).hand.end())
            d.lacks(Mustard, card);
    for (int card : game.seat(Mustard).hand)
        CHECK(d.holder(card) == Mustard);
    // Without counting (a novice), the same leaves them open.
    Notebook e;
    e.reset(game, Scarlet, false, false);
    for (int card = 0; card < kCardCount; ++card)
        if (std::find(game.seat(Mustard).hand.begin(), game.seat(Mustard).hand.end(), card) ==
            game.seat(Mustard).hand.end())
            e.lacks(Mustard, card);
    CHECK(e.holder(game.seat(Mustard).hand[0]) == -1); // White's or the case file's, for all it can tell
    // A wrong accusation naming the two cards known to be the answer's:
    // its room isn't.
    Notebook f;
    f.reset(game, Scarlet, true, true);
    for (int s : {Mustard, White})
        for (int card : game.seat(s).hand)
            if (card < roomCard(0))
                f.holds(s, card);
    CHECK(f.solved(0) == suspectCard(answer.suspect) && f.solved(1) == weaponCard(answer.weapon));
    int wrongRoom = -1;
    for (int r = 0; r < kRoomCount; ++r)
        if (r != answer.room && !f.outOfCaseFile(roomCard(r)))
            wrongRoom = r;
    CHECK(wrongRoom >= 0);
    f.notAll({suspectCard(answer.suspect), weaponCard(answer.weapon), roomCard(wrongRoom)});
    CHECK(f.outOfCaseFile(roomCard(wrongRoom)));
}

// Computer players alone, to the end: someone must win, and as they only
// accuse when sure, nobody wrongly.
static void testComputerPlayers() {
    Board board;
    CHECK(!board.build(testBoardLevel()));
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        const bool noDie = seed % 2 == 0;
        const std::array<bool, kSuspectCount> playing{true, seed % 3 != 0, true, true, seed % 5 != 0, true};
        Game game;
        CHECK(!game.start(board, playing, noDie, seed));
        std::array<ComputerPlayer, kSuspectCount> ai;
        for (int s = 0; s < kSuspectCount; ++s)
            if (playing[size_t(s)])
                ai[size_t(s)].reset(game, s, seed * 7 + uint32_t(s));
        int actions = 0;
        while (game.phase() != Game::Phase::Over && actions++ < 20000) {
            ComputerPlayer& me = ai[size_t(game.turn())];
            switch (me.decide(game)) {
            case ComputerPlayer::Action::Roll: CHECK(game.rollDie() > 0); break;
            case ComputerPlayer::Action::Passage: CHECK(!game.takePassage()); break;
            case ComputerPlayer::Action::Walk: CHECK(!game.walk(me.chooseWalk(game))); break;
            case ComputerPlayer::Action::Suggest: {
                const Accusation a = me.chooseSuggestion(game);
                CHECK(!game.suggest(a.suspect, a.weapon));
                if (game.phase() == Game::Phase::Disprove)
                    CHECK(!game.show(ai[size_t(game.disprover())].chooseShow(game, game.turn())));
                for (int s = 0; s < kSuspectCount; ++s)
                    if (playing[size_t(s)])
                        ai[size_t(s)].observe(game);
                break;
            }
            case ComputerPlayer::Action::Accuse: CHECK(!game.accuse(me.chooseAccusation())); break;
            case ComputerPlayer::Action::EndTurn: CHECK(!game.endTurn()); break;
            }
        }
        CHECK(game.phase() == Game::Phase::Over && game.winner() >= 0);
        for (int s = 0; s < kSuspectCount; ++s)
            CHECK(!game.seat(s).out);
    }
}

int main() {
    testUnpack();
    testDriveAndImages();
    testAnimations();
    testGameData(std::filesystem::temp_directory_path() / "reluva_clue_tests");
    testObjectsAndLevel();
    testDepth();
    testMansion(std::filesystem::temp_directory_path() / "reluva_clue_mansion");
    testResi();
    testDialog(std::filesystem::temp_directory_path() / "reluva_clue_dialog");
    testBoard();
    testRules();
    testNotebook();
    testComputerPlayers();
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
