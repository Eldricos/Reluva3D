// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: The mansion: what's drawn in front of what,
// hot spots, lighting, and a mansion built from a level.

#include "engines/clue/game/ai.h"
#include "engines/clue/game/board.h"
#include "engines/clue/ui/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/game/netplay.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/formats/resources.h"
#include "engines/clue/game/rules.h"
#include "engines/clue/ui/ui.h"
#include "tests/engines/clue/support.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::clue;
using namespace reluva::clue::testing;

static void testDepth() {
    // Separate in x or y: the further on is in front.
    const WorldBox a{0, 0, 16, 16, 50}, beyondX{16, 0, 16, 16, 50}, beyondY{0, 20, 16, 16, 50};
    CHECK(compareDepth(beyondX, a) == 1 && compareDepth(a, beyondX) == -1);
    CHECK(compareDepth(beyondY, a) == 1 && compareDepth(a, beyondY) == -1);
    CHECK(compareDepth(a, WorldBox{8, 8, 16, 16, 50}) == 0);

    // A hot spot's click: in its box's outline on the screen, the hexagon
    // (0,-40) (64,-8) (64,32) (0,64) (-64,32) (-64,-8) for this one.
    const WorldBox spot{0, 0, 64, 64, 40};
    CHECK(onBox(spot, 0, 20) && onBox(spot, 63, 0) && onBox(spot, -60, 30) && onBox(spot, 0, -39));
    CHECK(!onBox(spot, 0, -41) && !onBox(spot, 60, 40) && !onBox(spot, 70, 0) && !onBox(spot, 0, 70));

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

static void testLighting() {
    // ImageRLE16.dll's shades, on a colour's 5 bits (31: 248 in 8 bits).
    CHECK(shade(248, kShadeNone) == 248);
    CHECK(shade(248, 6) == ((28 << 3) | (28 >> 2)));         // 31 - 3
    CHECK(shade(248, 12) == ((15 << 3) | (15 >> 2)));        // 31 / 2
    CHECK(shade(248, 16) == ((7 << 3) | (7 >> 2)));          // 31 / 4
    CHECK(shade(248, kShadeUnlit) == ((6 << 3) | (6 >> 2))); // 7 - 1
    // Clue.exe's steps: max + min / 2, full within 80, then each 16 a step.
    CHECK(lightShade(0, 0, 79, 0) == kShadeNone);
    CHECK(lightShade(0, 0, 60, 40) == 6); // 60 + 20 = 80
    CHECK(lightShade(0, 0, 96, 0) == 7);
    CHECK(lightShade(0, 0, 239, 0) == 15);
    CHECK(lightShade(0, 0, 240, 0) == 16);
    CHECK(lightShade(0, 0, 1000, 1000) == 16);
}

static void testMansion(const std::filesystem::path &tmp) {
    std::filesystem::remove_all(tmp);
    const auto res = tmp / "Data" / "Classic" / "Resource";
    test::writeFile(res / "test.res", pack(drive({{"Ia", image200()}, {"Ib", image100()}})));
    test::writeFile(res / "test.a",
                    pack(drive({{"Awalk", animation(false)}, {"Afloor", animation(false, -1, 64, 64)}})));
    test::writeFile(res / "test.o", drive({{"Obench", objectRecord("test:Obench", 3, {{0, "test:Awalk"}})},
                                           {"Ofloor", objectRecord("test:Ofloor", 1, {{0, "test:Afloor"}})},
                                           {"Obroken", objectRecord("test:Obroken", 3, {{0, "test:Anothere"}})}}));
    test::writeFile(
        tmp / "Data" / "Classic" / "Levels" / "clue.lvl",
        levelFile({"test:Obench", "test:Ofloor", "test:Obroken"},
                  {{0x40, 0, 0, 200, 100, 1, 5, {}}, {0x40, 0, 1, 0, 0, -1, 1, {}}, {0x40, 0, 2, 0, 0, 1, 5, {}}}));
    GameData data;
    CHECK(!data.open(tmp, Language::US));
    DataDrive file;
    Level level;
    Mansion m;
    CHECK(!file.open(*data.find("clue.lvl")) && !parseLevel(file, level) && !m.build(data, level));
    CHECK(m.items().size() == 2 && m.problems().size() == 1); // the broken one is left out
    if (m.items().size() == 2) {
        const Mansion::Item &bench = m.items()[0];
        // The box from the anchor by the frame's offsets (its bytes 10-13: 3, 4).
        CHECK(!bench.flat && bench.box.x == 203 && bench.box.y == 104 && bench.box.sizeX == 16);
        CHECK(bench.box.z == 1 && bench.anchorX == 100 && bench.anchorY == 149); // x - y, (x + y) / 2 - z
        CHECK(m.items()[1].flat);
        CHECK(m.drawOrder() == std::vector<size_t>({1, 0})); // the floor first
        // Frame 0's pieces: image Ia (3x2) at the frame's anchor offset plus its layer's.
        CHECK(bench.rect.x == 100 - 44 && bench.rect.y == 149 - 106 - 5);
        // Actors go into the order by their boxes: before the bench when
        // behind it, after it when in front; on the floor,
        // after the floors and before everything else.
        Mansion::Actor actor;
        actor.frame = &(*bench.frames)[0];
        actor.box = {199, 100, 16, 16, 100}; // overlapping it on screen (its picture is 3x2)
        CHECK(m.insertionPoint(actor) == 1);
        // In front by the boxes (beyond the bench's along x), its picture
        // put over the bench's.
        Mansion::Frame over = *actor.frame;
        for (Mansion::Piece &p : over) {
            p.dx -= 29; // the bench's anchor is 29 left of this one's on screen,
            p.dy -= 16; // 16 above
        }
        actor.frame = &over;
        actor.box = {230, 101, 16, 16, 100};
        CHECK(m.insertionPoint(actor) == 2);
        actor.frame = &(*bench.frames)[0];
        actor.flat = true;
        CHECK(m.insertionPoint(actor) == 1);
        Surface s;
        s.create(640, 480, PixelFormat::RGBA32);
        MansionLook look;
        look.characters = false;
        m.draw(s, 0, 0, look, 0, {actor}); // drawn, whatever its place

        // What stands within 208 units beyond a revealing actor along x and
        // y (Clue.exe's 0x428190) is see-through or left out: the bench
        // (furniture) beyond one at (190, 90); not from a room (only walls
        // then), nor from beyond it.
        const auto benchPixel = [&](MansionLook::InFront inFront, int x, int y, bool inRoom) {
            Mansion::Actor a;
            a.reveal = true;
            a.inRoom = inRoom;
            a.box = {x, y, 16, 16, 100};
            MansionLook l;
            l.characters = false;
            l.inFront = inFront;
            std::fill(s.pixels.begin(), s.pixels.end(), uint8_t(0));
            m.draw(s, 0, 0, l, 0, {a});
            // The brightness of the bench's rectangle (the floor is elsewhere).
            int sum = 0;
            for (int y = bench.rect.y; y < bench.rect.y + bench.rect.h; ++y)
                for (int x = bench.rect.x; x < bench.rect.x + bench.rect.w; ++x)
                    for (int c = 0; c < 3; ++c)
                        sum += s.pixels[size_t(y) * size_t(s.pitch) + size_t(x) * 4 + size_t(c)];
            return sum;
        };
        const int solid = benchPixel(MansionLook::InFront::Solid, 190, 90, false);
        CHECK(solid > 0);
        CHECK(benchPixel(MansionLook::InFront::Hidden, 190, 90, false) == 0);
        const int faded = benchPixel(MansionLook::InFront::SeeThrough, 190, 90, false);
        CHECK(faded > 0 && faded < solid);
        CHECK(benchPixel(MansionLook::InFront::Hidden, 190, 90, true) == solid);
        CHECK(benchPixel(MansionLook::InFront::Hidden, 260, 90, false) == solid); // the bench behind it
        CHECK(benchPixel(MansionLook::InFront::Hidden, -10, 90, false) == solid); // more than 208 away
    }
    std::filesystem::remove_all(tmp);
}

static const reluva::test::Suite
    suite("clue.unit.mansion", {
                                   {"depth and hot spots", [](const std::filesystem::path &) { testDepth(); }},
                                   {"lighting", [](const std::filesystem::path &) { testLighting(); }},
                                   {"mansion", [](const std::filesystem::path &tmp) { testMansion(tmp / "mansion"); }},
                               });
