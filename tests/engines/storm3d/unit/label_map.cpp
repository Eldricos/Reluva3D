// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: the front end's text (label.map), written here in
// the game's layout.

#include "engines/storm3d/formats/label_map.h"

#include "tests/support/test.h"

#include <string>

using reluva::storm3d::LabelMap;

namespace {

// Sections of entries, with the file's comments, tabs, line ends and a
// Windows-1252 character.
void testRead() {
    const std::string file = "##########\r\n##  STAR TREK ARMADA\r\n\r\n"
                             "single_player {\r\n"
                             "\t+ title\t\t\t{SINGLE PLAYER}\r\n"
                             "\t+ federation_incomplete\t{Federation campaign not complete}\r\n"
                             "}\r\n\r\n"
                             "mission_select {\r\n"
                             "\t+ fed1.bzn\t\t{Premonitions}\r\n"
                             "        + bullet\t\t{\x95}\r\n"
                             "\t+ empty\t{}\r\n"
                             "}\r\n";
    LabelMap map;
    CHECK(!map.parse(file));
    CHECK(map.entries().size() == 5);
    CHECK(map.text("single_player", "title") == "SINGLE PLAYER");
    CHECK(map.text("SINGLE_PLAYER", "Federation_Incomplete") == "Federation campaign not complete");
    CHECK(map.text("mission_select", "fed1.bzn") == "Premonitions");
    CHECK(map.text("mission_select", "bullet") == "\xe2\x80\xa2");
    CHECK(map.text("mission_select", "empty", "none").empty());
    CHECK(map.text("mission_select", "title", "none") == "none");
    CHECK(map.text("single_player", "fed1.bzn").empty());
}

// What isn't the layout is refused, with where.
void testBroken() {
    LabelMap map;
    CHECK(map.parse("+ title {outside}\n"));
    CHECK(map.parse("a {\n+ title {open}\n"));
    CHECK(map.parse("a {\n+ title no braces\n}\n"));
    CHECK(map.parse("a {\nb {\n}\n}\n"));
    CHECK(map.parse("}\n"));
    const reluva::Error err = map.parse("a {\n+ x {y}\nwhat\n}\n");
    CHECK(err && err.message.find("line 3") != std::string::npos);
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.label_map",
                                        {
                                            {"read", [](const std::filesystem::path &) { testRead(); }},
                                            {"broken", [](const std::filesystem::path &) { testBroken(); }},
                                        });
