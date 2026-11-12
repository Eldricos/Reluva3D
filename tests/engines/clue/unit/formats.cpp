// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Clue engine's unit tests: Clue's files: packed archives, images,
// animations, the game's folders, objects, levels and dialogs
// (docs/engines/clue/formats/: res.md, level.md, resi.md).

#include "engines/clue/ai.h"
#include "engines/clue/board.h"
#include "engines/clue/frontend.h"
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include "engines/clue/netplay.h"
#include "engines/clue/resi.h"
#include "engines/clue/resources.h"
#include "engines/clue/rules.h"
#include "engines/clue/ui.h"
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
    CHECK(a.name.empty() && a.framesPerSecond == 6 && a.flags == Animation::BackAndForth && a.width == 75 && a.height == 120);
    CHECK(a.images.size() == 2 && a.images[1] == "test:Ib");
    CHECK(a.layers.size() == 2 && a.frameCount() == 2 && a.isometric && !a.shortForm);
    CHECK(a.layers[1][1].image == 1 && a.layers[1][1].x == 11 && a.layers[1][1].y == -5);
    CHECK(a.frames[0].anchorX == -44 && a.frames[0].anchorY == -106 && a.frames[1].height == 101);
    CHECK(a.frames[0].sizeX == 16 && a.frames[0].sizeY == 16 && a.frames[0].solid == 1);
    // 6 FPS back and forth: 0 1 0 1 ...; in a window, round and round.
    CHECK(a.frameAt(0, true) == 0 && a.frameAt(170, true) == 1 && a.frameAt(340, true) == 0 && a.frameAt(170, false) == 1);
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

static void testObjectsAndLevel() {
    ObjectDef o;
    CHECK(!parseObject(objectRecord("test:Obench", 3, {{0, "test:Awalk"}, {21, "test:Awalk"}}, {{1600, 21}}), o));
    CHECK(o.name == "test:Obench" && o.type == 3 && o.subType == 1 && o.startState == 0 && o.startFrame == 0 && o.states.size() == 2);
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
        CHECK(p.flags == 0x62 && p.tag == 3 && p.object == 0 && p.x == -64 && p.y == 128 && p.z == -1 &&
              p.layer == 1);
        CHECK(p.timings.size() == 1 && p.timings[0].time == 1000 && p.timings[0].state == 10);
        CHECK(level.placements[2].tag == 1030 && level.placements[2].layer == 7);
        CHECK(level.workspace.size() == 32);
    }
    // A placement naming an object that isn't there.
    CHECK(!d.load(levelFile({"test:Ofloor"}, {{0x40, 0, 4, 0, 0, 1, 5, {}}})));
    CHECK(parseLevel(d, level));
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

static const reluva::test::Suite suite("clue.unit.formats", {
    {"unpack", [](const std::filesystem::path&) { testUnpack(); }},
    {"archives and images", [](const std::filesystem::path&) { testDriveAndImages(); }},
    {"animations", [](const std::filesystem::path&) { testAnimations(); }},
    {"game folders", [](const std::filesystem::path& tmp) { testGameData(tmp / "gamedata"); }},
    {"objects and levels", [](const std::filesystem::path&) { testObjectsAndLevel(); }},
    {"dialogs", [](const std::filesystem::path&) { testResi(); }},
});
