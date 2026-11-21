// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's unit tests: its text formats, objects and scripts.

#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/formats/text_object.h"

#include "tests/support/test.h"

#include <string>

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

} // namespace

static const reluva::test::Suite kSuite("stawayteam.unit.formats",
                                        {
                                            {"object", [](const std::filesystem::path &) { testObject(); }},
                                            {"object lines", [](const std::filesystem::path &) { testObjectLines(); }},
                                            {"script", [](const std::filesystem::path &) { testScript(); }},
                                        });
