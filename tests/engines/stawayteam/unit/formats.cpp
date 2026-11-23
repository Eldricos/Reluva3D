// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's unit tests: its formats: frames, objects in text
// and in binary, and scripts.

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/formats/object_schema.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/formats/text_object.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <array>
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
    // As the game reads them: text before a label dropped, a blank line
    // after one kept, trailing spaces trimmed, the first of two labels the
    // one found.
    const RdsScript edges = readRdsScript("stray\r\n:A\r\n\r\nText  \t\r\n:A\r\nSecond\r\n");
    CHECK(edges.entries.size() == 2 && edges.find("A") && *edges.find("A") == "\nText");
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

// A CSequence (version 586): its ten properties, the first its frame count,
// then its box and its frames, the second missing.
void testBinarySequence() {
    BinaryWriter w;
    w.object(586.0f).i32(2).f32(30.0f).u8(1).str("Transparent Mask").u8(1).u8(0).u8(0).u8(0).i32(3).i32(-4);
    w.i32(-1).i32(-1).i32(2).i32(2);
    w.u8(1).bytes(frameFile(2, 2, 1, 1, 0x20, {{{0, 0, 0, 0}, {0, 0, 0, 0}}}, true)).u8(0);
    BinaryObject seq;
    CHECK(!readBinaryObject(w.out.data(), w.out.size(), "", seq));
    CHECK(seq.object.className == "CSequence" && seq.object.fields.size() == 10);
    CHECK(seq.object.number("Frames Per Second") == 30.0 && *seq.object.value("Blit Type") == "Transparent Mask");
    // Grouped keys ("Render/X Offset") keep their last part, as in text.
    CHECK(seq.object.integer("X Offset") == 3 && seq.object.integer("Y Offset") == -4);
    const std::array<int32_t, 4> box = {-1, -1, 2, 2};
    CHECK(seq.sequence && seq.sequence->box == box);
    CHECK(seq.sequence && seq.sequence->frames.size() == 2 && seq.sequence->frames[0] &&
          seq.sequence->frames[0]->width == 2 && !seq.sequence->frames[1]);
    // Of another version than its class's (586 made 2,344), it isn't read.
    w.out[7] = 0x45;
    CHECK(bool(readBinaryObject(w.out.data(), w.out.size(), "", seq)));
}

// A CWayPointMap: two waypoints on one line.
void testBinaryWayPoints() {
    BinaryWriter w;
    w.object(101.0f).i32(30).i32(59).i32(2);
    w.object(142.0f).i32(0).i32(10).i32(20).i32(1).i32(0);
    w.object(142.0f).i32(4).i32(110).i32(20).i32(1).i32(0);
    w.i32(1).object(283.0f).i32(0).f32(24.0f).f32(100.0f).f32(0.0f).i32(0).i32(1);
    BinaryObject map;
    CHECK(!readBinaryObject(w.out.data(), w.out.size(), "CWayPointMap", map));
    CHECK(map.object.integer("MinDistBetweenWayPoints") == 30);
    CHECK(map.wayPoints.size() == 2 && map.wayPoints[1].x == 110 && map.wayPoints[1].flags == 4 &&
          map.wayPoints[1].lines == std::vector<int32_t>({0}));
    CHECK(map.lines.size() == 1 && map.lines[0].length == 100.0f && map.lines[0].b == 1);
    // Something after the object: not the game's.
    w.u8(0);
    CHECK(bool(readBinaryObject(w.out.data(), w.out.size(), "CWayPointMap", map)));
}

// An object property: its class's name, then the object; empty for none.
void testBinaryObjectProperty() {
    const ClassSchema *item = findClass("CActionQueueItem");
    CHECK(item && findClass("CAction") && !findClass("CNoSuchClass"));
    if (!item)
        return;
    BinaryWriter w;
    w.object(item->version).f32(1.5f).str("CAction").object(1.0f).i32(-1).i32(0).i32(0).i32(0);
    BinaryObject o;
    CHECK(!readBinaryObject(w.out.data(), w.out.size(), "CActionQueueItem", o));
    const TextObject *action = o.object.object("Action");
    CHECK(action && action->className == "CAction" && o.object.integer("Instigator") == -1);
    BinaryWriter none;
    none.object(item->version).f32(0.0f).str("").i32(0).i32(0).i32(0).i32(0);
    CHECK(!readBinaryObject(none.out.data(), none.out.size(), "CActionQueueItem", o) && !o.object.object("Action"));
    // A class the game doesn't have.
    BinaryWriter odd;
    odd.object(item->version).f32(0.0f).str("CNoSuchClass").object(1.0f);
    CHECK(bool(readBinaryObject(odd.out.data(), odd.out.size(), "CActionQueueItem", o)));
}

// A model as modelFile builds it: what the viewer's tests rely on.
void testBinaryModel() {
    const auto bytes = modelFile("Test", 2, 3);
    BinaryObject model;
    CHECK(!readBinaryObject(bytes.data(), bytes.size(), "CModel", model));
    CHECK(model.object.className == "CModel" && *model.object.value("Legacy Name") == "Test");
    CHECK(model.sequences.size() == 1 && model.sequences[0].name == "Idle" && model.sequences[0].sequence);
    if (model.sequences.size() == 1 && model.sequences[0].sequence) {
        const BinaryObject &seq = *model.sequences[0].sequence;
        CHECK(seq.object.integer("Num Rotations") == 2 && seq.sequence && seq.sequence->frames.size() == 6);
        CHECK(seq.sequence && seq.sequence->frameExtra.size() == 6);
    }
    // A font: a kind of sequence, told by its version.
    const auto font = fontFile({solidFrame(3, 5, 0xffff)}, 65);
    BinaryObject f;
    CHECK(!readBinaryObject(font.data(), font.size(), "", f) && f.object.className == "CFont" &&
          f.object.integer("First ASCII") == 65);
}

} // namespace

static const reluva::test::Suite
    kSuite("stawayteam.unit.formats",
           {
               {"frame", [](const std::filesystem::path &) { testFrame(); }},
               {"binary sequence", [](const std::filesystem::path &) { testBinarySequence(); }},
               {"binary waypoints", [](const std::filesystem::path &) { testBinaryWayPoints(); }},
               {"binary model", [](const std::filesystem::path &) { testBinaryModel(); }},
               {"binary object property", [](const std::filesystem::path &) { testBinaryObjectProperty(); }},
               {"object", [](const std::filesystem::path &) { testObject(); }},
               {"object lines", [](const std::filesystem::path &) { testObjectLines(); }},
               {"script", [](const std::filesystem::path &) { testScript(); }},
           });
