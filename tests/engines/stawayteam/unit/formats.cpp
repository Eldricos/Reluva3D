// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's unit tests: its formats: frames, objects in text,
// and scripts.

#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/formats/text_object.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

// Written as the game writes it: a tab a level, CRLF.
const std::string kMission = "CMissionDescription\r\n"
                             "{\r\n"
                             "\tMission Name=Klingon Base \r\n"
                             "\tShow Briefing=1\r\n"
                             "\tSpeed=0.25\r\n"
                             "\tScript=CBriefingScript\r\n"
                             "\t{\r\n"
                             "\t\tScript=Array\r\n"
                             "\t\t{\r\n"
                             "\t\t\tArray Count=2\r\n"
                             "\t\t\tArray Item=CBriefingScriptLine\r\n"
                             "\t\t\t{\r\n"
                             "\t\t\t\tText=One\r\n"
                             "\t\t\t}\r\n"
                             "\t\t\tArray Item=CBriefingScriptLine\r\n"
                             "\t\t\t{\r\n"
                             "\t\t\t\tText=Two\r\n"
                             "\t\t\t}\r\n"
                             "\t\t}\r\n"
                             "\t}\r\n"
                             "\tActor=Array\r\n"
                             "\t{\r\n"
                             "\t\tActor=Data\r\n"
                             "\t\tActor=Worf\r\n"
                             "\t}\r\n"
                             "}\r\n";

void testObject() {
    TextObject mission;
    CHECK(!readTextObject(kMission, mission));
    CHECK(mission.className == "CMissionDescription" && mission.fields.size() == 5);
    // A trailing space is part of the value: the game's names have them.
    CHECK(mission.value("Mission Name") && *mission.value("Mission Name") == "Klingon Base ");
    CHECK(mission.integer("Show Briefing") == 1 && mission.number("Speed") == 0.25);
    CHECK(!mission.integer("Speed") && !mission.integer("Missing") && !mission.value("Missing"));
    // An array with its count, and its items.
    const TextObject *script = mission.object("Script");
    CHECK(script && script->className == "CBriefingScript");
    const TextObject *lines = script ? script->object("Script") : nullptr;
    CHECK(lines && lines->className == "Array");
    if (lines) {
        const auto items = lines->items();
        CHECK(items.size() == 2 && items[0]->object && items[1]->object);
        CHECK(items.size() == 2 && items[1]->object && *items[1]->object->value("Text") == "Two");
    }
    // One without a count, of plain values under a key that repeats.
    const TextObject *actors = mission.object("Actor");
    CHECK(actors && actors->items().size() == 2 && actors->items()[1]->value == "Worf");
    // Written back as it was.
    CHECK(writeTextObject(mission) == kMission);
}

void testObjectLines() {
    // LF alone, spaces to indent, blank lines, and a line without '=' (skipped,
    // as the game does); values with '{' after them only are objects.
    TextObject o;
    CHECK(!readTextObject("\n  CThing\n  {\n\n    Name=a b\n    stray words\n    Inner=CInner\n    {\n    "
                          "  X=1\n    }\n    Empty=\n  }\n",
                          o));
    CHECK(o.className == "CThing" && o.fields.size() == 3);
    CHECK(*o.value("Name") == "a b" && o.object("Inner") && *o.object("Inner")->value("X") == "1");
    CHECK(o.value("Empty") && o.value("Empty")->empty() && !o.object("Empty"));
    // What isn't an object.
    CHECK(bool(readTextObject("", o)));
    CHECK(bool(readTextObject("Key=Value\r\n{\r\n}\r\n", o)));
    CHECK(bool(readTextObject("CThing\r\nName=a\r\n", o)));
    CHECK(bool(readTextObject("CThing\r\n{\r\n\tName=a\r\n", o)));
}

void testScript() {
    const RdsScript script = readRdsScript("//***Mission 01\r\n"
                                           "\r\n"
                                           ":Dead Body\r\n"
                                           "This is the dead body of $trigger.  {Inventory It contains $inventory.}\r\n"
                                           "\r\n"
                                           ":Both Scientists \r\n"
                                           "First paragraph.\r\n"
                                           "\r\n"
                                           "Second paragraph.\r\n"
                                           "\r\n"
                                           "\r\n"
                                           "//***Mission 02\r\n"
                                           ":Empty\r\n");
    CHECK(script.entries.size() == 3);
    CHECK(script.find("Dead Body") &&
          *script.find("Dead Body") == "This is the dead body of $trigger.  {Inventory It contains $inventory.}");
    // Labels keep their spaces; blank lines between paragraphs stay, those
    // after an entry don't.
    CHECK(!script.find("Both Scientists") && script.find("Both Scientists ") &&
          *script.find("Both Scientists ") == "First paragraph.\n\nSecond paragraph.");
    CHECK(script.find("Empty") && script.find("Empty")->empty());
    // Comments alone: no entries.
    CHECK(readRdsScript("//***Mission 01\r\n\r\n//***Mission 02\r\n").entries.empty());
}

void testFrame() {
    // A raw 2x2, as the textures are.
    const auto raw = frameFile(2, 2, 0, 0, 0x20, {{{0x00, 0xf8, 0xe0, 0x07}, {0x1f, 0x00, 0xff, 0xff}}}, true);
    Frame f;
    size_t used = 0;
    CHECK(!readFrame(raw.data(), raw.size(), f, &used) && used == raw.size());
    CHECK(f.width == 2 && f.height == 2 && f.flags == 0x20);
    CHECK(f.pixels == std::vector<uint16_t>({0xf800, 0x07e0, 0x001f, 0xffff}));
    CHECK(f.alpha == std::vector<uint8_t>(4, 255));
    // RGB 5:6:5 widened to 8 bits each, white staying white.
    const auto img = frameImage(f);
    CHECK(img.pixel(0, 0)[0] == 255 && img.pixel(0, 0)[1] == 0 && img.pixel(1, 0)[1] == 255 &&
          img.pixel(0, 1)[2] == 255 && img.pixel(1, 1)[0] == 255 && img.pixel(1, 1)[3] == 255);

    // Run-length coded, 4x2: a skip, two colours of their own, one colour
    // for one pixel; then one colour for four. The skipped pixel blended in,
    // its alpha in block 1.
    const auto coded = frameFile(4, 2, -3, 5, 0x2024,
                                 {{{0x81, 0x42, 0x01, 0x00, 0x02, 0x00, 0x01, 0x03, 0x00}, {0x04, 0x04, 0x00}},
                                  {{0x80}, {}},
                                  {{0x01, 0x05, 0x00, 0x83}, {0x84}}});
    CHECK(!readFrame(coded.data(), coded.size(), f, &used) && used == coded.size());
    CHECK(f.originX == -3 && f.originY == 5);
    CHECK(f.pixels == std::vector<uint16_t>({5, 1, 2, 3, 4, 4, 4, 4}));
    CHECK(f.alpha == std::vector<uint8_t>({0x80, 255, 255, 255, 255, 255, 255, 255}));

    // Damaged: cut short, a row longer than the frame, blended pixels
    // without their alpha, and something else altogether.
    std::vector<uint8_t> cut(coded.begin(), coded.end() - 10);
    CHECK(bool(readFrame(cut.data(), cut.size(), f)));
    const auto wide = frameFile(2, 1, 0, 0, 0x2024, {{{0x03, 0x00, 0x00}}});
    CHECK(bool(readFrame(wide.data(), wide.size(), f)));
    const auto noAlpha = frameFile(1, 1, 0, 0, 0x2024, {{{0x81}}, {}, {{0x01, 0x00, 0x00}}});
    CHECK(bool(readFrame(noAlpha.data(), noAlpha.size(), f)));
    const std::vector<uint8_t> other = {'F', 'R', 'A', 'M', 'E', 0};
    CHECK(bool(readFrame(other.data(), other.size(), f)));
}

} // namespace

static const reluva::test::Suite kSuite("stawayteam.unit.formats",
                                        {
                                            {"frame", [](const std::filesystem::path &) { testFrame(); }},
                                            {"object", [](const std::filesystem::path &) { testObject(); }},
                                            {"object lines", [](const std::filesystem::path &) { testObjectLines(); }},
                                            {"script", [](const std::filesystem::path &) { testScript(); }},
                                        });
