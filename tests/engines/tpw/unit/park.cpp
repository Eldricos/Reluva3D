// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park being played: a new one set up from settings and a mission
// built in the test; its calendar and its cells.

#include "common/archive.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/park/park.h"

#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::tpw;

namespace {

void writeText(const std::filesystem::path &path, const std::string &text) {
    test::writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
}

// A park set up from the general settings, a world's and a mission's:
// its money, fixed items, objective and boundary.
void testNewPark(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 50000\r\nBankAccountInfo.InitialAdmissionFee 25\r\n");
    writeText(data / "levels" / "science" / "Standard.sam",
              "FixedItemInfo.NumTicketBooths 2\r\nTicketBooth[0].PosX 45\r\nTicketBooth[0].PosY 11\r\n"
              "TicketBooth[0].Angle 512\r\nTicketBooth[1].PosX 45\r\nTicketBooth[1].PosY 8\r\n"
              "FixedItemInfo.EntranceAPosX 47\r\nFixedItemInfo.EntranceAPosY 17\r\n"
              "FixedItemInfo.EntranceBPosX 48\r\nFixedItemInfo.EntranceBPosY 17\r\n"
              "FixedItemInfo.BusStopCPosX 61\r\nFixedItemInfo.BusStopCPosY 7\r\n");
    writeText(data / "missions" / "miss00.sam",
              "Info.Number 0\r\nExitStatistic[0].Type 8\r\nExitStatistic[0].TargetValue 40\r\nObstacle.Type 1700\r\n"
              "BoundaryClicks[0].X 0\r\nBoundaryClicks[0].Y 0\r\nBoundaryClicks[1].X 2\r\nBoundaryClicks[1].Y -6\r\n");
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.cash() == 50000 && park.admissionFee() == 25 && park.visitors() == 0 && park.day() == 0);
    const Park::FixedItems &fixed = park.fixedItems();
    CHECK(fixed.ticketBooths.size() == 2 && (fixed.ticketBooths[1] == Park::Cell{45, 8}));
    CHECK(fixed.ticketBoothAngles[0] == 512 && (fixed.entrance[1] == Park::Cell{48, 17}));
    CHECK((fixed.busStops[2] == Park::Cell{61, 7}) && (fixed.busStops[0] == Park::Cell{0, 0}));
    const Park::Mission &m = park.mission();
    CHECK(m.number == 0 && m.statistic == 8 && m.target == 40 && m.obstacle == 1700);
    CHECK(m.boundary.size() == 2 && (m.boundary[1] == Park::Cell{2, -6}));
    CHECK(park.load(files, "levels/science", 1)); // no miss01.sam
    CHECK(park.load(files, "levels/water", 7));
}

// The clock: a day every four seconds of play, the calendar's months and
// leap years; a cell's middle in the world.
void testCalendar() {
    Park park;
    park.advance(Park::kDayMillis * 13 + 3999);
    CHECK(park.day() == 13);
    const Park::Date jan14 = Park::dateOf(13);
    CHECK(jan14.day == 14 && jan14.month == 0 && jan14.year == 2000);
    const Park::Date feb29 = Park::dateOf(31 + 28);
    CHECK(feb29.day == 29 && feb29.month == 1);
    const Park::Date mar1 = Park::dateOf(31 + 29);
    CHECK(mar1.day == 1 && mar1.month == 2);
    const Park::Date nextYear = Park::dateOf(366);
    CHECK(nextYear.day == 1 && nextYear.month == 0 && nextYear.year == 2001);
    const Park::Date feb2001 = Park::dateOf(366 + 31 + 28);
    CHECK(feb2001.day == 1 && feb2001.month == 2);
    const gfx::Vec3 c = Park::cellCentre({47, 17});
    CHECK(c[0] == 475 && c[1] == 0 && c[2] == 175);
}

} // namespace

static const reluva::test::Suite kSuite("tpw.unit.park",
                                        {
                                            {"new-park", [](const std::filesystem::path &tmp) { testNewPark(tmp); }},
                                            {"calendar", [](const std::filesystem::path &) { testCalendar(); }},
                                        });
