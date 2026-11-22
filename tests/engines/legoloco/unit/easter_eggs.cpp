// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: EE.INI and the area layouts' arrangements (.lay),
// on text written here in the files' shape.

#include "engines/legoloco/formats/area_layout.h"
#include "engines/legoloco/formats/easter_eggs.h"

#include "tests/support/test.h"

#include <filesystem>

using namespace reluva;
using namespace reluva::legoloco;

namespace {

constexpr const char *kEggs = "// start day, start mon (0 = any), end date, end mon (0 = any),\r\n"
                              "\r\n"
                              "[TimeEvents]\r\n"
                              "001=001,000,031,000,001,000,023,00, 14451,0,  310,S, 0,-1\r\n"
                              "002=024,012,030,012,000,000,023,59, 14458,2, 2160,P,-1,40\r\n"
                              "004=001,000,031,000,001,000,023,00, 14460,0,  900,P, 0,-1\r\n"
                              "\r\n"
                              "[LoadEvents]\r\n"
                              "001=001,004,014,004, 1024, 1080\r\n"
                              "002=030,010,001,011, 4096, 4268\r\n";

void testEasterEggs() {
    EasterEggs eggs;
    CHECK(!parseEasterEggs(kEggs, eggs));
    // 003 is missing, so 004 isn't read, as the game reads them.
    CHECK(eggs.timeEvents.size() == 2);
    if (eggs.timeEvents.size() == 2) {
        const TimeEvent &e = eggs.timeEvents[1];
        CHECK(e.startDay == 24 && e.startMonth == 12 && e.endDay == 30 && e.endMonth == 12 && e.endHour == 23 &&
              e.endMinute == 59 && e.resource == 14458 && e.frameSet == 2 && e.period == 2160 && e.type == 'P' &&
              e.x == -1 && e.y == 40);
    }
    CHECK(eggs.loadEvents.size() == 2);
    if (eggs.loadEvents.size() == 2)
        CHECK(eggs.loadEvents[1].startDay == 30 && eggs.loadEvents[1].endMonth == 11 &&
              eggs.loadEvents[1].resource == 4096 && eggs.loadEvents[1].replacement == 4268);
}

void testEasterEggsBadLine() {
    EasterEggs eggs;
    CHECK(bool(parseEasterEggs("[LoadEvents]\n001=001,004,014\n", eggs)));
    CHECK(!parseEasterEggs("[Other]\n001=x\n", eggs));
    CHECK(eggs.timeEvents.empty() && eggs.loadEvents.empty());
}

void testAreaLayout() {
    AreaLayout layout;
    CHECK(!parseAreaLayout("4\r\n2\r\n2\r\narea1.sav\r\narea3.sav\r\narea7.sav\r\narea9.sav\r\n", layout));
    CHECK(layout.areas == 4 && layout.columns == 2 && layout.rows == 2 && layout.boards.size() == 4 &&
          layout.boards[3] == "area9.sav");
    CHECK(bool(parseAreaLayout("3\r\n3\r\n1\r\narea13.sav\r\n", layout)));
    CHECK(bool(parseAreaLayout("x\r\n", layout)));
}

void testLayoutIndex() {
    const auto names = parseLayoutIndex("3 X 2\r\n3 X 1\r\n\r\n");
    CHECK(names.size() == 2 && names[0] == "3 X 2" && names[1] == "3 X 1");
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.easter_eggs",
           {
               {"easter eggs", [](const std::filesystem::path &) { testEasterEggs(); }},
               {"easter eggs bad line", [](const std::filesystem::path &) { testEasterEggsBadLine(); }},
               {"area layout", [](const std::filesystem::path &) { testAreaLayout(); }},
               {"layout index", [](const std::filesystem::path &) { testLayoutIndex(); }},
           });
