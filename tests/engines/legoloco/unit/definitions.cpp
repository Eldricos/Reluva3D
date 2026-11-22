// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: object definitions (.dat), on text written here in
// the files' own shape.

#include "engines/legoloco/formats/object_definition.h"

#include "tests/support/test.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::legoloco;

namespace {

// A building's two parts: where it stands, then its pictures.
constexpr const char *kBuilding = "physical_occupancy\r\n"
                                  "\r\n"
                                  "3 2 2\r\n"
                                  "1 1 1\r\n"
                                  "1 1 1\r\n"
                                  "\r\n"
                                  "0 1 1\r\n"
                                  "0 0 0\r\n"
                                  "\r\n"
                                  "bitmap_occupancy\r\n"
                                  "2 3\r\n"
                                  "0 0\r\n"
                                  "1 2\r\n"
                                  "1 1\r\n"
                                  "RMBSeq 2062\r\n"
                                  "EasterEgg -1 0  -1 0 0  -1 -1 R 0 0\r\n"
                                  "-9\r\n"
                                  "button offset 3 3 3\r\n"
                                  "Hotspot 19 58\r\n"
                                  "total_number_of_frames 6\r\n"
                                  "number_of_frame_sets 3\r\n"
                                  "cursor/default_frame_set 0 0\r\n"
                                  "\r\n"
                                  "default 0 0 0 0  0 0 0 0 0  0\r\n"
                                  "walk-left\t11 0   4 0    0 2     0 0 0    1\r\n"
                                  "angry\t\t1 5  3 0    -1 -1  20777 70000 2 0\r\n"
                                  "-9\r\n"
                                  "\r\n"
                                  "// frameset name, start frame, end frame\r\n";

void testParts() {
    ObjectDefinition def;
    CHECK(!parseObjectDefinition(kBuilding, def));
    CHECK(def.parts.size() == 2);
    const DefinitionLine *rmb = def.find("rmbseq");
    CHECK(rmb && rmb->values.size() == 1 && rmb->values[0] == "2062" && rmb->line == 15);
    const DefinitionLine *egg = def.find("EasterEgg");
    CHECK(egg && egg->values.size() == 10 && egg->values[7] == "R");
    const DefinitionLine *button = def.find("button");
    CHECK(button && button->values.size() == 4 && button->values[0] == "offset");
    CHECK(def.find("frameset") == nullptr);
}

void testOccupancy() {
    ObjectDefinition def;
    CHECK(!parseObjectDefinition(kBuilding, def));
    CHECK(def.physical && def.physical->columns == 3 && def.physical->rows == 2 && def.physical->layers == 2);
    CHECK(def.physical && def.physical->at(0, 1, 2) == 1 && def.physical->at(1, 0, 0) == 0 &&
          def.physical->at(1, 0, 1) == 1 && def.physical->at(1, 1, 2) == 0);
    CHECK(def.bitmap && def.bitmap->columns == 2 && def.bitmap->rows == 3 && def.bitmap->layers == 1);
    CHECK(def.bitmap && def.bitmap->at(0, 1, 1) == 2);
    // The lines keep the numbers read into the fields, without the cells.
    const DefinitionLine *physical = def.find("physical_occupancy");
    CHECK(physical && physical->values == std::vector<std::string>({"3", "2", "2"}));
}

void testFrameSets() {
    ObjectDefinition def;
    CHECK(!parseObjectDefinition(kBuilding, def));
    CHECK(def.totalFrames == 6);
    CHECK(def.frameSets.size() == 3);
    if (def.frameSets.size() != 3)
        return;
    // One frame naming itself next stops instead; a delay of 0 is 1.
    CHECK(def.frameSets[0].name == "default" && def.frameSets[0].next == -1 && def.frameSets[0].delay == 1);
    const FrameSet &left = def.frameSets[1];
    CHECK(left.name == "walk-left" && left.start == 11 && left.end == 0 && left.delay == 4 && left.next == 2 &&
          left.flipX == 1);
    const FrameSet &angry = def.frameSets[2];
    CHECK(angry.restartDelay == -1 && angry.next == -1 && angry.sound == 20777 && angry.replayDelay == 70000 &&
          angry.unnamed == 2);
}

void testNoFrames() {
    ObjectDefinition def;
    CHECK(!parseObjectDefinition("button offset 0 0 0\ntotal_number_of_frames 0\nnumber_of_frame_sets 0\n"
                                 "cursor_Frame_set 0 0\n-9\n",
                                 def));
    CHECK(def.totalFrames == 1 && def.frameSets.empty() && def.parts.size() == 1);
}

void testTooFewNumbers() {
    ObjectDefinition def;
    CHECK(bool(parseObjectDefinition("number_of_frame_sets 1\ncursor_frame_set 0 0\nnormal 0 0 0\n", def)));
    CHECK(bool(parseObjectDefinition("physical_occupancy 2 1 1\n1\n", def)));
    CHECK(bool(parseObjectDefinition("bitmap_occupancy 2 x\n", def)));
}

void testGridTooLarge() {
    ObjectDefinition def;
    CHECK(bool(parseObjectDefinition("bitmap_occupancy 14 1\n", def)));
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.definitions",
           {
               {"parts", [](const std::filesystem::path &) { testParts(); }},
               {"occupancy", [](const std::filesystem::path &) { testOccupancy(); }},
               {"frame sets", [](const std::filesystem::path &) { testFrameSets(); }},
               {"no frames", [](const std::filesystem::path &) { testNoFrames(); }},
               {"too few numbers", [](const std::filesystem::path &) { testTooFewNumbers(); }},
               {"grid too large", [](const std::filesystem::path &) { testGridTooLarge(); }},
           });
