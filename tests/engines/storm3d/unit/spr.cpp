// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: sprite tables (.spr), on tables written here in
// the forms Armada's take, and where a sprite's frames are.

#include "engines/storm3d/formats/sprite_table.h"
#include "engines/storm3d/pages/sprite_page.h"

#include "tests/support/test.h"

#include <map>
#include <string>

using namespace reluva;
using reluva::storm3d::SpritePage;
using reluva::storm3d::SpriteTable;

namespace {

SpriteTable::Loader loaderFor(const std::map<std::string, std::string> &files) {
    return [&files](std::string_view name, std::string &text) {
        const auto it = files.find(std::string(name));
        if (it == files.end())
            return Error::fail(std::string(name) + " not found");
        text = it->second;
        return Error::success();
    };
}

// Sprites with the reference size, colour key and material in force where
// they are; an include; animations of each kind of keyframe; an emitter.
void testRead() {
    const std::map<std::string, std::string> files = {
        {"sprites.spr", "sprite_table\r\n\r\n# Sprite tables to load :-\r\n@include tex_anim.spr\r\n"
                        "@referenceWidth=128\r\n@referenceHeight=64\r\n@skip=(0,0,0)\r\n@tmaterial=interface\r\n"
                        "# Name\tFile\tU\tV\tW\tH\r\n"
                        "c_select\tcurs_24\t0\t32\t32\t32\t@anim=cursor4x1 @cursor @origin=(16,16)\r\n"
                        "@reference=256\r\n@animation fade\r\ncolour 2 4.0 linear\r\n@keyframes\r\n0.0\t(1,1,1)\r\n"
                        "4.0\t(0,0,0)\r\n@animation hold\r\ncolour 2 2.0 linear\r\n0.0\t(0.2,1.0,0.3)\r\n"
                        "1.0\t(0.1,0.8,0.2)\r\n\r\n@emitter rift\r\ntype=spray\r\nvelocity=12\r\n@end_emitter\r\n"
                        "@sprite_node a1 asteroid1 const (2,2) (1,1,.6) billboard\r\n"
                        "big\tpanel\t0\t0\t256\t24\r\n"},
        {"tex_anim.spr", "sprite_table\r\n@animation cursor4x1\r\noffset 4 1.0 step\r\n#Auto generate keyframes\r\n"
                         "@auto=row\r\n@keyframes\r\n"},
    };
    SpriteTable table;
    CHECK(!table.load("sprites.spr", loaderFor(files)));
    CHECK((table.tables() == std::vector<std::string>{"sprites.spr", "tex_anim.spr"}));
    CHECK(table.problems().empty());
    CHECK(table.sprites().size() == 2);
    const SpriteTable::Sprite *s = table.sprite("c_select");
    CHECK(s && s->texture == "curs_24" && s->v == 32 && s->h == 32);
    CHECK(s && s->referenceWidth == 128 && s->referenceHeight == 64 && s->skip == "(0,0,0)" &&
          s->material == "interface");
    CHECK(s && s->animations == std::vector<std::string>{"cursor4x1"});
    CHECK(s && s->parameters.count("cursor") && s->parameters.at("origin") == "(16,16)");
    const SpriteTable::Sprite *big = table.sprite("big");
    CHECK(big && big->referenceWidth == 256 && big->referenceHeight == 256);

    const SpriteTable::Animation *row = table.animation("cursor4x1");
    CHECK(row && row->kind == "offset" && row->count == 4 && row->autoLayout == "row" && row->keyframes.empty());
    const SpriteTable::Animation *fade = table.animation("fade");
    CHECK(fade && fade->keyframes.size() == 2 && fade->keyframes[1] == std::vector<double>({4, 0, 0, 0}));
    // Keyframes with no @keyframes before them, as animation.spr's holdbeam.
    const SpriteTable::Animation *hold = table.animation("hold");
    CHECK(hold && hold->keyframes.size() == 2 && hold->keyframes[0][2] == 1.0);

    CHECK(table.emitters().size() == 1 && table.emitters()[0].settings.at("velocity") == "12");
    CHECK(table.spriteNodes().size() == 1);
    // A node's fields; a model's node finds it by its name, numbered or not.
    const SpriteTable::SpriteNode &a1 = table.spriteNodes()[0];
    CHECK(a1.name == "a1" && a1.sprite == "asteroid1" && a1.animation == "const" && a1.width == 2 && a1.height == 2 &&
          a1.colour[2] == 0.6 && a1.billboard);
    CHECK(table.spriteNodeFor("A1") == &a1 && table.spriteNodeFor("a1_3") == &a1 &&
          table.spriteNodeFor("a1_1_2") == &a1);
    CHECK(!table.spriteNodeFor("a1x") && !table.spriteNodeFor("a1_") && !table.spriteNodeFor("b1_1"));
}

// A line of no known form is kept as a problem; a missing include fails.
void testProblems() {
    SpriteTable table;
    CHECK(!table.load("a.spr", loaderFor({{"a.spr", "sprite_table\nwhat is this\n12 13\n"}})));
    CHECK(table.problems().size() == 2);
    CHECK(table.load("a.spr", loaderFor({{"a.spr", "@include b.spr\n"}})));
}

// A font's character: its keyframe's place and width; a flipbook's frame:
// its cell in the square; all scaled from the reference size to the
// texture's.
void testFrames() {
    SpriteTable::Sprite ch;
    ch.h = 10;
    ch.referenceWidth = ch.referenceHeight = 128;
    SpriteTable::Animation uv;
    uv.kind = "offset";
    uv.keyframes = {{0, 0}, {4, 0}, {6, 11}};
    SpriteTable::Animation width;
    width.kind = "width";
    width.keyframes = {{3}, {1}, {5}};
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    SpritePage::frameRect(ch, &uv, &width, 2, 128, 128, x, y, w, h);
    CHECK(x == 6 && y == 11 && w == 5 && h == 10);
    SpritePage::frameRect(ch, &uv, &width, 2, 256, 256, x, y, w, h);
    CHECK(x == 12 && y == 22 && w == 10 && h == 20);

    SpriteTable::Sprite flip;
    flip.w = flip.h = 32;
    flip.referenceWidth = flip.referenceHeight = 128;
    SpriteTable::Animation square;
    square.kind = "offset";
    square.count = 16;
    square.autoLayout = "square";
    SpritePage::frameRect(flip, &square, nullptr, 9, 128, 128, x, y, w, h);
    CHECK(x == 32 && y == 64 && w == 32 && h == 32);
    square.autoLayout = "column";
    SpritePage::frameRect(flip, &square, nullptr, 2, 128, 128, x, y, w, h);
    CHECK(x == 0 && y == 64);
}

// The tables' slips in sprite nodes: a bracket closed twice or left open,
// and a flag commented out.
void testSpriteNodeSlips() {
    SpriteTable::SpriteNode n;
    CHECK(SpriteTable::parseSpriteNode(" asteroid6 asteroid6 const (2.5,2.5) (.8,.7,.6)) billboard", n));
    CHECK(n.width == 2.5 && n.colour[0] == 0.8 && n.colour[2] == 0.6 && n.billboard);
    CHECK(SpriteTable::parseSpriteNode(" redconst AmberMulti const (.7,.7)) (1,.8,0)", n));
    CHECK(n.width == 0.7 && n.colour[1] == 0.8 && n.colour[2] == 0 && !n.billboard);
    CHECK(SpriteTable::parseSpriteNode(" crewfre23 crew3 fire (2.35,10.88) (1,1,0.5", n));
    CHECK(n.height == 10.88 && n.colour[2] == 0.5);
    CHECK(SpriteTable::parseSpriteNode(" whitestrobe Ballflare strobe (.5,.5) (1,1,1) //billboard", n));
    CHECK(!n.billboard);
    CHECK(!SpriteTable::parseSpriteNode(" lonely sprite", n));
}

} // namespace

static const reluva::test::Suite
    kSuite("storm3d.unit.spr", {
                                   {"read", [](const std::filesystem::path &) { testRead(); }},
                                   {"problems", [](const std::filesystem::path &) { testProblems(); }},
                                   {"frames", [](const std::filesystem::path &) { testFrames(); }},
                                   {"sprite node slips", [](const std::filesystem::path &) { testSpriteNodeSlips(); }},
                               });
