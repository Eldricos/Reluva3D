// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park being played: a new one set up from settings and a mission
// built in the test; its calendar and its cells.

#include "common/archive.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/park/attractions.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/options.h"
#include "engines/tpw/saves.h"
#include "engines/tpw/park/path_tiles.h"

#include "tests/engines/tpw/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
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
    // Every visitor type but the last a grandparent at half the pace.
    std::string typeLines;
    for (int i = 0; i < 23; ++i) {
        const std::string base = "PeepTypes[" + std::to_string(i) + "].";
        typeLines += base + "PreferredExcitement.StartingCash.BoredomThreshold.Generation 90 100 60 2\r\n" + base +
                     "SpeedMultiplier 0.5\r\n";
    }
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 50000\r\nBankAccountInfo.InitialAdmissionFee 25\r\n"
              "Arrival.MinPeople 1\r\nArrival.TimeBetweenArrivals 25\r\nArrival.FixedRate 15\r\n"
              "Arrival.NewParkBonus 20\r\nArrival.PointsPerVisitor 25\r\n" +
                  typeLines);
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
    // The type the settings leave out is a grandparent by its place, at the
    // children's pace.
    const std::vector<Park::VisitorType> &types = park.visitorTypes();
    CHECK(types.size() == 24 && types[0].generation == 2 && types[0].speed == 0.5f);
    CHECK(types.size() == 24 && types[23].generation == 2 && types[23].speed == 1);
    const Park::FixedItems &fixed = park.fixedItems();
    CHECK(fixed.ticketBooths.size() == 2 && (fixed.ticketBooths[1] == Park::Cell{45, 8}));
    CHECK(fixed.ticketBoothAngles[0] == 512 && (fixed.entrance[1] == Park::Cell{48, 17}));
    CHECK((fixed.busStops[2] == Park::Cell{61, 7}) && (fixed.busStops[0] == Park::Cell{0, 0}));
    const Park::Mission &m = park.mission();
    CHECK(m.number == 0 && m.statistic == 8 && m.target == 40 && m.obstacle == 1700);
    CHECK(m.boundary.size() == 2 && (m.boundary[1] == Park::Cell{2, -6}));
    // One visitor wanted, (15 + 20) / 25; they come at 25 s, walk the way
    // in past the booth, paying, and through the entrance.
    CHECK(park.visitorsWanted() == 1 && park.wayIn().size() == 6);
    park.advance(24900);
    CHECK(park.visitorsAbout().empty());
    park.advance(200);
    CHECK(park.visitorsAbout().size() == 1 && !park.visitorsAbout()[0].paid);
    park.setPaused(true);
    park.advance(60000);
    CHECK(park.cash() == 50000 && park.day() == 6);
    park.setPaused(false);
    park.advance(120000); // at half the pace
    CHECK(park.cash() == 50025 && park.visitors() == 1 && park.visitorsAbout().size() == 1);
    CHECK(park.visitorsAbout()[0].inPark && park.visitorsAbout()[0].generation == 2);
    CHECK(park.visitorsAbout()[0].walked > 40);
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

// The ground by the attribute map, its tile in row x, column y: path where
// it says so, bare land, the rest not the park's; the prebuilt park's path
// and queue over it, if it's the world's; lengths of path laid, paid for,
// over bare land only.
void testPaths(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 100\r\nCosts.PathCell 10\r\nCosts.QueueCell 10\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    tiles[size_t(2) * kColumns + 1] = 8; // cell (2, 1)
    tiles[size_t(2) * kColumns + 2] = 8; // cell (2, 2)
    tiles[size_t(5) * kColumns + 4] = 1; // cell (5, 4): not the park's
    test::writeFile(data / "levels" / "science" / "terrain" / "base.map",
                    tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    // The prebuilt park, of this world: its tiles the map's; path at (6, 1),
    // a queue at (6, 2) to an entrance at (6, 3); and another entrance at
    // (7, 3), without a queue.
    auto prebuilt = [&](bool sameWorld, bool second = false) {
        return tpw::testing::makeSavedPark(
            [&](int x, int y, tpw::testing::Bytes &body) {
                if (x < kRows && y < kColumns)
                    body[46] = tiles[size_t(x) * kColumns + size_t(y)];
                if (!sameWorld && x == 0 && y == 0)
                    body[46] = 3;
                if (x == 6 && y == 1)
                    body[13] = 1;
                if (x == 6 && y == 2) {
                    body[13] = 2; // a queue's one cell, from the path to an entrance
                    body[25] = 3;
                }
                if ((x == 6 || (second && x == 7)) && y == 3)
                    body[25] = 9;
                if (x == 4 && y == 3)
                    body[25] = 4; // an object's
            },
            {{1101, 0x480, 0x380, 0}, {1601, 0x80, 0x80, 0}});
    };
    test::writeFile(data / "PreBuilt" / "prebuilt.TPWS", prebuilt(true));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.prebuilt() && park.isPath({6, 1}) && park.ground({6, 2}) == Park::Ground::kQueue);
    CHECK(park.ground({4, 3}) == Park::Ground::kTaken && park.ground({6, 3}) == Park::Ground::kTaken);
    // Its queue followed from the entrance to the path: the piece by the
    // path, its posts north to it.
    CHECK(park.queues().size() == 1 && park.queues()[0].atPath && (park.queues()[0].pathEnd == Park::Cell{6, 1}));
    const std::vector<Park::QueuePiece> pieces = park.queuePieces();
    CHECK(pieces.size() == 1 && pieces[0].model == Park::QueueModel::kJoin && pieces[0].angle == 0);
    CHECK(park.queueFrom({6, 3}) == -1);
    CHECK(park.objects().size() == 1 && park.objects()[0].id == 1101 && (park.objects()[0].at == Park::Cell{4, 3}));
    CHECK(park.groundWidth() == kRows && park.groundHeight() == kColumns && park.pathCost() == 10);
    CHECK(park.isPath({2, 1}) && park.isPath({2, 2}) && park.ground({1, 2}) == Park::Ground::kOpen);
    CHECK(park.ground({5, 4}) == Park::Ground::kOutside && park.ground({-1, 0}) == Park::Ground::kOutside);
    CHECK(park.ground({kRows, 0}) == Park::Ground::kOutside);

    // Along whichever way is the longer, from the start.
    const std::vector<Park::Cell> run = Park::pathRun({2, 2}, {3, 5});
    CHECK(run.size() == 4 && (run[0] == Park::Cell{2, 2}) && (run[3] == Park::Cell{2, 5}));
    const std::vector<Park::Cell> back = Park::pathRun({7, 4}, {2, 3});
    CHECK(back.size() == 6 && (back[5] == Park::Cell{2, 4}));
    CHECK(park.buildPath(run) == 3 && park.cash() == 70 && park.isPath({2, 5}));
    // Not over (5, 4), nor again over (2, 4).
    CHECK(park.buildPath(back) == 4 && park.cash() == 30 && park.isPath({7, 4}) && !park.isPath({5, 4}));
    // Backspace takes the length back, paid back; laid again, as before.
    std::vector<Park::Cell> laid;
    park.undoPath({{7, 4}, {6, 4}, {4, 4}, {3, 4}});
    CHECK(park.cash() == 70 && !park.isPath({7, 4}) && park.isPath({2, 4}));
    CHECK(park.buildPath(back, &laid) == 4 && park.cash() == 30 && laid.size() == 4 && (laid[0] == Park::Cell{7, 4}));
    // The money runs out three cells along.
    CHECK(park.buildPath(Park::pathRun({0, 0}, {0, 5})) == 3 && park.cash() == 0 && !park.isPath({0, 3}));

    // Another world's prebuilt park isn't laid.
    test::writeFile(data / "PreBuilt" / "prebuilt.TPWS", prebuilt(false));
    SearchSet other;
    CHECK(!mountGame(other, tmp / "game", warnings));
    CHECK(!park.load(other, "levels/science", 0));
    CHECK(!park.prebuilt() && !park.isPath({6, 1}) && park.ground({6, 2}) == Park::Ground::kOpen);
    CHECK(park.objects().empty() && park.queues().empty());

    // A queue laid from the other entrance: up two cells, then towards the
    // path, which it meets and joins; at 10 a cell.
    test::writeFile(data / "PreBuilt" / "prebuilt.TPWS", prebuilt(true, true));
    SearchSet laying;
    CHECK(!mountGame(laying, tmp / "game", warnings));
    CHECK(!park.load(laying, "levels/science", 0));
    const int q = park.queueFrom({7, 3});
    CHECK(q >= 0 && park.queueCost() == 10 && park.queueFrom({6, 3}) == -1);
    if (q < 0)
        return;
    const Park::Queue preview = park.queueWith(q, Park::pathRun({7, 3}, {7, 0}));
    CHECK(preview.cells.size() == 3 && !preview.atPath && park.queues()[size_t(q)].cells.empty());
    CHECK(park.layQueue(q, Park::pathRun({7, 3}, {7, 1})) == 2 && park.cash() == 80);
    CHECK(park.queueFrom({7, 1}) == q && park.ground({7, 2}) == Park::Ground::kQueue);
    park.undoQueue(q, 1);
    CHECK(park.cash() == 90 && park.ground({7, 1}) == Park::Ground::kOpen && park.queueFrom({7, 2}) == q);
    CHECK(park.layQueue(q, Park::pathRun({7, 2}, {7, 1})) == 1 && park.cash() == 80);
    // On to the path at (6, 1): it joins it at once.
    CHECK(park.layQueue(q, Park::pathRun({7, 1}, {3, 1})) == 0 && park.queues()[size_t(q)].atPath);
    CHECK((park.queues()[size_t(q)].pathEnd == Park::Cell{6, 1}) && park.queueFrom({7, 1}) == -1);

    // An attraction placed on bare land, paid for, its queue begun at its
    // entrance; not over path, nor for more than the money.
    const std::vector<std::string> stall{"*2"};
    CHECK(park.place(1300, 10, stall, true, {2, 2}, 0) == Park::kNotPlaced); // (2, 2) is path
    const int cash = park.cash();
    const int started = park.place(1300, 10, stall, true, {0, 4}, 0);
    CHECK(started >= 0 && park.cash() == cash - 10 && park.ground({1, 4}) == Park::Ground::kTaken);
    CHECK(started >= 0 && (park.queues()[size_t(started)].entrance == Park::Cell{1, 4}) &&
          park.queueFrom({1, 4}) == started && park.objects().back().id == 1300);
    CHECK(park.place(1300, 1000, stall, false, {0, 5}, 0) == Park::kNotPlaced);
    CHECK(park.place(1209, 0, {"*"}, false, {3, 4}, 0) == -1);
}

// A queue's pieces by its shape: straights, a bend each way, the piece by
// the path straight or bent (its posts towards the path), a dead end.
void testQueuePieces() {
    using Model = Park::QueueModel;
    Park::Queue q;
    q.entrance = {5, 10};
    q.cells = {{5, 9}, {5, 8}, {6, 8}, {7, 8}};
    q.atPath = true;
    q.pathEnd = {8, 8};
    std::vector<Park::QueuePiece> p = Park::queuePieces(q);
    CHECK(p.size() == 4 && p[0].model == Model::kStraight && p[0].angle == 0);
    CHECK(p.size() == 4 && p[1].model == Model::kBend && p[1].angle == 0);
    CHECK(p.size() == 4 && p[2].model == Model::kStraight && p[2].angle == 90);
    CHECK(p.size() == 4 && p[3].model == Model::kJoin && p[3].angle == 90);
    q.atPath = false;
    p = Park::queuePieces(q);
    CHECK(p.size() == 4 && p[3].model == Model::kDeadEnd && p[3].angle == 90);

    Park::Queue bent;
    bent.entrance = {6, 9};
    bent.cells = {{5, 9}, {5, 10}};
    bent.atPath = true;
    bent.pathEnd = {4, 10};
    p = Park::queuePieces(bent);
    CHECK(p.size() == 2 && p[0].model == Model::kBendOther && p[0].angle == 0);
    // In from the north, out west to the path: the bend turned a half, its
    // east side, with the posts, west.
    CHECK(p.size() == 2 && p[1].model == Model::kJoinBendEast && p[1].angle == 180);
    bent.cells = {{5, 9}};
    bent.pathEnd = {5, 8};
    p = Park::queuePieces(bent);
    CHECK(p.size() == 1 && p[0].model == Model::kJoinBendEast && p[0].angle == 270);
    bent.pathEnd = {5, 10};
    p = Park::queuePieces(bent);
    CHECK(p.size() == 1 && p[0].model == Model::kJoinBendSouth && p[0].angle == 0);
}

// The texture a path cell takes by its neighbours: alone, an end turned
// its way, straights from three, a corner with and without its notch, a
// three-way's notch either side, and a crossroads' notches.
void testPathTiles() {
    auto is = [](uint8_t around, const char *name, int turns, uint32_t roll = 0) {
        const PathTile t = pathTileFor(around, roll);
        return std::string(kPathTextures[t.texture]) == name && t.turns == turns;
    };
    CHECK(is(0, "ns_squ1", 0));
    CHECK(is(kPathN, "ns_end1", 0));
    CHECK(is(kPathE, "ns_end1", 1));
    CHECK(is(kPathW | kPathNE, "ns_end1", 3));
    CHECK(is(kPathE | kPathW, "ns_str1", 1, 0) && is(kPathE | kPathW, "ns_str2", 1, 1) &&
          is(kPathE | kPathW, "ns_pathcnr", 1, 2));
    CHECK(is(kPathN | kPathE | kPathNE, "ns_cnr1", 0));
    CHECK(is(kPathN | kPathE, "ns_cnr2", 0));
    CHECK(is(kPathS | kPathW | kPathSW, "ns_cnr1", 2));
    CHECK(is(kPathN | kPathE | kPathS | kPathNE, "ns_tju3", 0));
    CHECK(is(kPathN | kPathE | kPathS | kPathSE, "ns_tju4", 2));
    CHECK(is(kPathN | kPathE | kPathS, "ns_tju1", 0));
    const uint8_t all = kPathN | kPathE | kPathS | kPathW;
    CHECK(is(all | kPathNE | kPathSE | kPathSW | kPathNW, "ns_ctr1", 0));
    CHECK(is(all, "ns_xrd1", 0));
    CHECK(is(all | kPathNE | kPathSE | kPathNW, "ns_icn1", 0));
    CHECK(is(all | kPathNE | kPathSW, "ns_icn2", 1));
}

// A world's attractions by their settings, over their kind's: on sale
// with a price and research group 0; their shape, queue and ratings.
void testAttractions(const std::filesystem::path &tmp) {
    const std::filesystem::path world = tmp / "game" / "Data" / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nResearch.Group 0\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nInfo.Name \"Giant Robot Swing\"\r\nInfo.Shape\r\n---\r\n.**.\r\n*2N*\r\n---\r\n"
              "Upgrades[0].CostOfUpgrade 1400\r\nUpgrades[0].InitCapacity 8\r\nUsageInfo.ExcitementLevel 70\r\n"
              "Info.WorkingLife 75\r\n");
    test::writeFile(world / "rides" / "robot" / "robot.MD2", tpw::testing::Bytes{0});
    writeText(world / "rides" / "wheel" / "wheel.sam", "Info.Id 1160\r\nUpgrades[0].CostOfUpgrade 0\r\n");
    writeText(world / "shops" / "Shops.sam", "Info.HasQueue 0\r\nResearch.Group 0\r\n");
    writeText(world / "shops" / "drinks" / "drinks.sam", "Info.Id 1203\r\nUpgrades[0].CostOfUpgrade 500\r\n");
    writeText(world / "shops" / "gift" / "gift.sam",
              "Info.Id 1211\r\nUpgrades[0].CostOfUpgrade 750\r\nResearch.Group 5\r\n");
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Attractions attractions;
    CHECK(!attractions.load(files, "levels/science"));
    CHECK(attractions.all().size() == 4);
    const Attractions::Attraction *robot = attractions.find(1101);
    CHECK(robot && robot->kind == Attractions::Kind::kRide && robot->hasQueue && robot->available);
    CHECK(robot && robot->price == 1400 && robot->name == "Giant Robot Swing" && robot->shape.size() == 2);
    CHECK(robot && robot->capacity == 8 && robot->excitement == 70 && robot->workingLife == 75);
    CHECK(robot && robot->model.size() > 9 && robot->model.compare(robot->model.size() - 9, 9, "robot.MD2") == 0);
    CHECK(attractions.find(1160) && !attractions.find(1160)->available);
    CHECK(attractions.onSale(Attractions::Kind::kRide).size() == 1);
    const std::vector<const Attractions::Attraction *> shops = attractions.onSale(Attractions::Kind::kShop);
    CHECK(shops.size() == 1 && shops[0]->id == 1203 && !shops[0]->hasQueue);
}

// A shape's footprint, its last row at the first cell, turned east
// towards south; placed where it fits, paid for, its queue begun.
void testPlacing() {
    const std::vector<std::string> shape{".**.", "*2N*"};
    Park::Footprint f = Park::footprint(shape, {10, 10}, 0);
    CHECK(f.cells.size() == 6 && f.hasEntrance && (f.entrance == Park::Cell{11, 10}) && (f.exit == Park::Cell{12, 10}));
    CHECK((f.cells[0] == Park::Cell{11, 11}) && (f.cells[5] == Park::Cell{13, 10}));
    f = Park::footprint(shape, {10, 10}, 90);
    CHECK((f.entrance == Park::Cell{10, 11}) && (f.exit == Park::Cell{10, 12}));
    CHECK(Park::footprint({}, {3, 4}, 0).cells.size() == 1);

    Park bare; // without a map: nothing fits
    CHECK(!bare.canPlace(Park::footprint(shape, {1, 1}, 0)));
    CHECK(bare.place(1101, 0, shape, true, {1, 1}, 0) == Park::kNotPlaced);
}

// Visitors on a line of path, all children liking excitement 60: to the
// ride that matches it, up its queue, on when the ride takes them,
// paying its fare, off by its exit, happier; then to the balloon kiosk,
// which has no queue, for the happiness it gives (they're not thirsty or
// hungry, which it wouldn't help); home when their days are up.
void testRiders(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "100 200 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 1000\r\nCosts.QueueCell 10\r\nArrival.MinPeople 1\r\n"
              "Arrival.TimeBetweenArrivals 1\r\nArrival.FixedRate 1\r\nArrival.PointsPerVisitor 1\r\n"
              "PeepInfo.PercentageOfKids 100\r\nPeepInfo.PercentageOfParents 0\r\n"
              "PeepInfo.PercentageOfGrandparents 0\r\nPeepInfo.ExitLevel 100\r\nPeepInfo.ExitLevelVar 0\r\n" +
                  types);
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nInfo.AttractionValue 3\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nUsageInfo.InitPricePerUse 10\r\nUsageInfo.ExcitementLevel 60\r\n"
              "Info.Shape\r\n---\r\n2N\r\n---\r\n");
    writeText(world / "shops" / "Shops.sam", "Info.HasQueue 0\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "shops" / "balloon" / "balloon.sam",
              "Info.Id 1209\r\nUsageInfo.InitPricePerUse 5\r\nUsageInfo.HappinessEffect 20\r\n"
              "Info.Shape\r\n---\r\n*2\r\n---\r\n");
    // Without the world's settings its way in ends at (0, 4): path from
    // there along y = 4.
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.points() == 1 && park.visitorTypes()[3].cash == 100 && park.visitorTypes()[3].excitement == 60);
    const int q = park.place(1101, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.queueOf(0) == q && park.points() == 4 && park.visitorsWanted() == 4);
    if (q < 0)
        return;
    // The kiosk: its entrance at (5, 3), by the path at (5, 4).
    CHECK(park.place(1209, 0, {"*2"}, false, {4, 3}, 0) == -1);
    const int shop = park.queueOf(1);
    CHECK(shop >= 0 && park.queues()[size_t(shop)].kiosk && park.queues()[size_t(shop)].atPath);
    CHECK(shop >= 0 && (park.queues()[size_t(shop)].pathEnd == Park::Cell{5, 4}));
    CHECK(park.queuePieces().empty() && park.queueFrom({5, 3}) == -1);
    if (shop < 0)
        return;
    CHECK(park.layQueue(q, Park::pathRun({2, 2}, {2, 4})) == 1 && park.queues()[size_t(q)].atPath);
    const Park::Queue &queue = park.queues()[size_t(q)];
    CHECK(queue.fare == 10 && queue.hasExit && (queue.exit == Park::Cell{3, 2}));
    CHECK(park.waitingAt(q) == 0 && !park.board(q, 1));

    int waiting = 0;
    for (int t = 0; t < 600 && !waiting; ++t) {
        park.advance(100);
        waiting = park.waitingAt(q);
        CHECK(queue.line.size() <= size_t(2 * Park::kVisitorsPerQueueCell));
    }
    CHECK(waiting != 0);
    if (!waiting)
        return;
    const Park::Visitor *v = park.visitor(waiting);
    CHECK(v && v->doing == Park::Doing::kQueueing && v->x == 25 && v->z == 25);
    CHECK(v && v->generation == 0 && v->cash == 100); // these settings charge no admission
    const int cash = park.cash();
    CHECK(park.board(q, waiting) && park.cash() == cash + 10);
    v = park.visitor(waiting);
    CHECK(v && v->doing == Park::Doing::kRiding && v->cash == 90 && v->rides == 0);
    CHECK(!park.board(q, waiting) && !park.letOff(q, waiting + 100));
    park.advance(1000);
    CHECK(park.visitor(waiting)->doing == Park::Doing::kRiding);
    CHECK(park.letOff(q, waiting));
    v = park.visitor(waiting);
    // A perfect match: 25 happier.
    CHECK(v && v->doing == Park::Doing::kLeaving && v->x == 35 && v->z == 25 && (v->cell == Park::Cell{3, 4}));
    CHECK(v && v->rides == 1 && v->happiness == Park::kStartingHappiness + 25 && v->lastQueue == q);

    // Then the kiosk, the ride just ridden not chosen again at once.
    int atShop = 0;
    for (int t = 0; t < 300 && atShop != waiting; ++t) {
        park.advance(100);
        atShop = park.waitingAt(shop);
        if (atShop && atShop != waiting && park.board(shop, atShop))
            park.letOff(shop, atShop);
    }
    CHECK(atShop == waiting && park.board(shop, waiting) && park.letOff(shop, waiting));
    v = park.visitor(waiting);
    CHECK(v && v->cash == 85 && v->rides == 2 && v->happiness == Park::kStartingHappiness + 25 + 20);
    CHECK(v && v->x == 55 && v->z == 35 && (v->cell == Park::Cell{5, 4}));

    // A hundred days on (400 s), they've gone home, and others come.
    for (int t = 0; t < 6000; ++t) {
        park.advance(100);
        for (int k : {q, shop})
            if (const int id = park.waitingAt(k); id && park.board(k, id))
                park.letOff(k, id);
    }
    CHECK(!park.visitor(waiting) && !park.visitorsAbout().empty());
    CHECK(park.visitors() <= park.visitorsWanted());
}

// A park in snow, its January's only weather: fewer come. A kiosk sells
// umbrellas, which its buyer then carries; it leaves them litter to drop
// and makes them sick, which they are on the path at their next cell,
// thinking so.
void testConditions(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "100 200 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 1000\r\nArrival.MinPeople 1\r\nArrival.TimeBetweenArrivals 1\r\n"
              "Arrival.FixedRate 8\r\nArrival.PointsPerVisitor 1\r\nArrival.SnowPercentageModifier 50\r\n"
              "PeepInfo.PercentageOfKids 100\r\nPeepInfo.VomitCapacity 100\r\nPeepInfo.PrankeryLikelihood 0\r\n" +
                  types);
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "Standard.sam", "Seasons[0].PercentageChanceForSnow 100\r\n");
    writeText(world / "shops" / "Shops.sam", "Info.HasQueue 0\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "shops" / "brolly" / "brolly.sam",
              "Info.Id 1201\r\nUsageInfo.InitPricePerUse 5\r\nUsageInfo.HappinessEffect 30\r\n"
              "UsageInfo.AppearanceEffect 3\r\nUsageInfo.LitterEffect 400\r\nUsageInfo.VomitEffect 150\r\n"
              "WeatherModifier.SnowPercentage 200\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.weather() == Park::Weather::kSnow && park.visitorsWanted() == 4); // half of 8
    CHECK(park.place(1201, 0, {"*2"}, false, {4, 3}, 0) == -1);
    const int shop = park.queueOf(0);
    CHECK(shop >= 0);
    if (shop < 0)
        return;
    int waiting = 0;
    for (int t = 0; t < 600 && !waiting; ++t) {
        park.advance(100);
        waiting = park.waitingAt(shop);
    }
    CHECK(waiting != 0 && park.board(shop, waiting) && park.letOff(shop, waiting));
    const Park::Visitor *v = park.visitor(waiting);
    CHECK(v && v->appearance == 3 && v->carrying == 400 && v->sickness >= 100);
    CHECK(v && (v->person >= 0 && v->person <= 3) && v->female == (v->person == 1 || v->person == 2));
    // Off at the entrance, to the path at (5, 4): sick there, and their litter dropped.
    for (int t = 0; t < 50 && park.litter().size() < 2; ++t)
        park.advance(100);
    CHECK(park.litter().size() == 2);
    const auto vomit =
        std::count_if(park.litter().begin(), park.litter().end(), [](const Park::Litter &l) { return l.vomit; });
    CHECK(vomit == 1);
    v = park.visitor(waiting);
    CHECK(v && v->sickness == 0 && v->carrying == 0 && v->thought == Park::Thought::kSick &&
          v->thoughtUntil > park.millis());
}

// The staff agencies' candidates by the settings; a janitor hired, paid
// on the month's first day, sweeping up litter dropped near; scientists
// wanting a research room. Prices against what's fair, and a gate too
// dear turning visitors back.
void testStaff(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "100 200 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 1000\r\nArrival.MinPeople 1\r\nArrival.TimeBetweenArrivals 1\r\n"
              "Arrival.FixedRate 3\r\nArrival.PointsPerVisitor 1\r\nPeepInfo.PercentageOfKids 100\r\n"
              "PeepInfo.MinimumEntryFee 20\r\nStaffPoolInfo.BeginningNumberOfHandymen 3\r\n"
              "StaffPoolInfo.AvgExperienceOfHandymen 20000\r\nStaffPoolInfo.AvgWageDifference 0\r\n"
              "StaffAgencies[0].RequiredPromotionLevel 0\r\nStaffAgencies[0].InitialWageModifier 10\r\n"
              "StaffAgencies[0].InitialExperienceModifier 0\r\nHandyman.MinDetectionRange 5\r\n"
              "Handyman.MaxDetectionRange 5\r\n" +
                  types);
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "shops" / "Shops.sam", "Info.HasQueue 0\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "shops" / "balloon" / "balloon.sam",
              "Info.Id 1209\r\nUsageInfo.InitPricePerUse 5\r\nInfo.ExpectedPrice 5\r\nUsageInfo.HappinessEffect 30\r\n"
              "UsageInfo.LitterEffect 400\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));

    // Three janitors offered, at the average experience, paid 60% of what
    // it expects: 80 a fifth of the way up, 16, by 10 + 50 percent.
    const auto &janitors = park.candidates(Park::StaffKind::kJanitor);
    CHECK(janitors.size() == 3 && janitors[0].experience == 20000 && janitors[0].wage == 10);
    CHECK(janitors.size() == 3 && janitors[0].name != janitors[1].name);
    CHECK(park.hire(Park::StaffKind::kScientist, 0) == Park::Hiring::kNeedsLab);
    CHECK(park.hire(Park::StaffKind::kJanitor, 5) == Park::Hiring::kNoSuchCandidate);
    CHECK(park.hire(Park::StaffKind::kJanitor, 0) == Park::Hiring::kHired);
    CHECK(park.staff().size() == 1 && park.candidates(Park::StaffKind::kJanitor).size() == 2 && park.wages() == 10);

    // Prices: up to 0.75 of fair cheap, to 1.25 about right, below twice too high.
    CHECK(park.priceOpinion(7, 10) == Park::PriceOpinion::kCheap);
    CHECK(park.priceOpinion(12, 10) == Park::PriceOpinion::kAboutRight);
    CHECK(park.priceOpinion(19, 10) == Park::PriceOpinion::kTooHigh);
    CHECK(park.priceOpinion(20, 10) == Park::PriceOpinion::kMuchTooHigh);
    CHECK(park.fairEntryFee() == 20);

    // The kiosk by the path: its buyers drop its litter at once, which the
    // janitor sweeps up.
    CHECK(park.place(1209, 0, {"*2"}, false, {4, 3}, 0) == -1);
    const int shop = park.queueOf(0);
    bool littered = false;
    for (int t = 0; t < 1200; ++t) {
        park.advance(100);
        if (const int id = park.waitingAt(shop); id && park.board(shop, id))
            park.letOff(shop, id);
        littered = littered || !park.litter().empty();
    }
    CHECK(littered && park.litter().empty());
    // 120 seconds is 30 days: the 1st of February paid the wage.
    const int before = park.cash();
    for (int t = 0; t < 10 && park.cash() >= before; ++t)
        park.advance(4000);
    CHECK(park.cash() < before);

    // Far too dear to come in: they turn back at the booth, paying nothing.
    Park dear;
    CHECK(!dear.load(files, "levels/science", 0));
    dear.setAdmissionFee(100);
    dear.advance(60000);
    CHECK(dear.visitors() == 0 && dear.cash() == 1000);
}

// Upkeep: a ride worn out by a rider, broken, so none get on, till the
// engineer sent mends it; a plant drying and watered by a gardener; a
// gardener trained; patrols, wages, firing; underpaid staff demanding a
// raise, and a kind striking after a year; the park closed.
void testUpkeep(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "1000 2000 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(
        data / "levels" / "Standard.sam",
        "BankAccountInfo.InitialCash 100000\r\nCosts.QueueCell 10\r\nArrival.MinPeople 1\r\n"
        "Arrival.TimeBetweenArrivals 1\r\nArrival.FixedRate 2\r\nArrival.PointsPerVisitor 1\r\n"
        "PeepInfo.PercentageOfKids 100\r\nPeepInfo.ExitLevel 5000\r\n"
        "StaffPoolInfo.BeginningNumberOfMechanics 2\r\nStaffPoolInfo.BeginningNumberOfGardeners 2\r\n"
        "StaffPoolInfo.BeginningNumberOfHandymen 4\r\nStaffPoolInfo.AvgExperienceOfMechanics 40000\r\n"
        "StaffPoolInfo.AvgExperienceOfGardeners 40000\r\nStaffPoolInfo.AvgExperienceOfHandymen 1000\r\n"
        "StaffPoolInfo.AvgWageDifference 0\r\nStaffPoolInfo.MinExperience 1000\r\n"
        "StaffAgencies[0].InitialWageModifier 10\r\nStaffAgencies[0].InitialExperienceModifier 0\r\n"
        "StaffAgencies[0].TrainingExperienceGainModifier 10\r\nAllStaffConstants.MinExperienceForTraining 30000\r\n"
        "AllStaffConstants.TrainingExtraExperience 5000\r\nGardener.MinDetectionRange 10\r\n"
        "Gardener.MaxDetectionRange 10\r\nStrike.MinMonthsForStrike 12\r\nStrike.MinStaffOfTypeForStrike 3\r\n" +
            types);
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nUsageInfo.InitPricePerUse 10\r\nUsageInfo.ExcitementLevel 60\r\n"
              "Upgrades[0].WearRate 500\r\nUpgrades[0].CostOfUpgrade 1000\r\nUpgrades[0].ScrapValueYear1 50\r\n"
              "Info.Shape\r\n---\r\n2N\r\n---\r\n");
    writeText(world / "features" / "flobed" / "flobed.sam",
              "Info.Id 1401\r\nUsageInfo.RequiresWatering 1\r\nInfo.Shape\r\n---\r\n*\r\n---\r\n");
    writeText(world / "Facilities" / "train_rm" / "train_rm.sam",
              "Info.Id 1440\r\nUsageInfo.TrainingRoom 1\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    const int q = park.place(1101, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.layQueue(q, Park::pathRun({2, 2}, {2, 4})) == 1);
    CHECK(park.place(1401, 0, {"*"}, false, {5, 5}, 0) == -1);
    CHECK(park.place(1440, 0, {"*2"}, false, {5, 2}, 0) == -1);
    CHECK(park.scrapValue(0) == 500);

    // Staff: two engineers and gardeners able to be trained; janitors not.
    CHECK(park.hire(Park::StaffKind::kEngineer, 0) == Park::Hiring::kHired);
    CHECK(park.hire(Park::StaffKind::kGardener, 0) == Park::Hiring::kHired);
    CHECK(park.hire(Park::StaffKind::kGardener, 0) == Park::Hiring::kHired);
    CHECK(park.hire(Park::StaffKind::kJanitor, 0) == Park::Hiring::kHired);
    CHECK(park.staff().size() == 4);
    const int engineer = park.staff()[0].id;
    const int gardener = park.staff()[1].id;
    const int trainee = park.staff()[2].id;
    const int janitor = park.staff()[3].id;
    CHECK(park.sendForTraining(janitor) == Park::Training::kTooInexperienced);
    CHECK(park.sendForTraining(trainee) == Park::Training::kSent);
    CHECK(park.staffMember(trainee)->doing == Park::StaffDoing::kGoingToTrain);

    // Wages: set, clamped, changed by a percentage; patrols.
    park.setWage(janitor, 20);
    CHECK(park.staffMember(janitor)->wage == 20);
    park.adjustWages(Park::StaffKind::kJanitor, Park::Adjustment::kPercent, 50);
    CHECK(park.staffMember(janitor)->wage == 30);
    park.setPatrol(janitor, {3, 4}, {0, 4});
    const Park::StaffMember *j = park.staffMember(janitor);
    CHECK(j && j->patrolling && (j->patrolFrom == Park::Cell{0, 4}) && (j->patrolTo == Park::Cell{3, 4}));
    park.clearPatrol(janitor);
    CHECK(!park.staffMember(janitor)->patrolling);

    // The first rider wears the ride out: broken, none get on; the engineer
    // is sent the next day, and mends it.
    int waiting = 0;
    for (int t = 0; t < 600 && !waiting; ++t) {
        park.advance(100);
        waiting = park.waitingAt(q);
    }
    CHECK(waiting && park.board(q, waiting) && park.objects()[0].broken && park.objects()[0].users == 1);
    CHECK(park.letOff(q, waiting));
    bool sent = false;
    for (int t = 0; t < 600 && park.objects()[0].broken; ++t) {
        park.advance(100);
        sent = sent || park.objects()[0].engineer == engineer;
        CHECK(park.waitingAt(q) == 0 || !park.objects()[0].broken);
    }
    CHECK(sent && !park.objects()[0].broken && park.objects()[0].repair == 100 && !park.objects()[0].engineer);

    // Closed, none get on either.
    park.setOpen(0, false);
    for (int t = 0; t < 100; ++t) {
        park.advance(100);
        CHECK(park.waitingAt(q) == 0);
    }
    park.setOpen(0, true);

    // A month and more on: the plant dried below half and was watered; the
    // trainee is trained, 5000 the more experienced.
    for (int t = 0; t < 1500; ++t)
        park.advance(100);
    CHECK(park.objects()[1].water > 50);
    const Park::StaffMember *t = park.staffMember(trainee);
    CHECK(t && t->doing == Park::StaffDoing::kWorking && t->experience == 45000);

    // Fired, gone.
    park.fire(gardener);
    CHECK(!park.staffMember(gardener) && park.staff().size() == 3);

    // Underpaid a year, staff demand a raise; refused at too little, taken
    // at the whole.
    for (int t = 0; t < 4000 && !park.wageDemand(); ++t)
        park.advance(1000);
    const Park::WageDemand *demand = park.wageDemand();
    CHECK(demand != nullptr);
    if (demand) {
        const int who = demand->staff;
        const int raise = demand->raise;
        const int was = park.staffMember(who)->wage;
        CHECK(park.offer(0) == Park::Offer::kRefused && park.wageDemand());
        CHECK(park.offer(raise) == Park::Offer::kAccepted && park.staffMember(who)->wage == was + raise);
    }

    // Three janitors, worn out a year on: on strike, none hired the while.
    CHECK(park.hire(Park::StaffKind::kJanitor, 0) == Park::Hiring::kHired);
    CHECK(park.hire(Park::StaffKind::kJanitor, 0) == Park::Hiring::kHired);
    for (int t = 0; t < 1600 && !park.striking(Park::StaffKind::kJanitor); ++t)
        park.advance(1000);
    CHECK(park.striking(Park::StaffKind::kJanitor));
    CHECK(park.hire(Park::StaffKind::kJanitor, 0) == Park::Hiring::kStrike);

    // The park closed: nobody more comes.
    park.setParkOpen(false);
    const size_t about = park.visitorsAbout().size();
    for (int t = 0; t < 600; ++t)
        park.advance(100);
    CHECK(park.visitorsAbout().size() <= about);
}

// Research: a ride researched, its consultant needed, put on sale; an
// upgrade researched, then bought, needing an engineer trained, and made;
// experience gained by the month; an attraction renamed and deleted.
void testResearch(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 100000\r\nArrival.MinPeople 0\r\nArrival.FixedRate 0\r\n"
              "StaffPoolInfo.BeginningNumberOfMechanics 2\r\nStaffPoolInfo.BeginningNumberOfResearchers 2\r\n"
              "StaffPoolInfo.AvgExperienceOfMechanics 40000\r\nStaffPoolInfo.AvgExperienceOfResearchers 20000\r\n"
              "StaffPoolInfo.AvgWageDifference 0\r\nStaffAgencies[0].InitialWageModifier 10\r\n"
              "StaffAgencies[0].InitialExperienceModifier 0\r\nStaffAgencies[0].ExperienceGainModifier 10\r\n"
              "StaffAgencies[0].TrainingExperienceGainModifier 10\r\n"
              "AllStaffConstants.MinExperienceForTraining 30000\r\nAllStaffConstants.TrainingExtraExperience 5000\r\n"
              "ResearcherConstsPerGrade[1].ExperiencePerMonth 600\r\nResearch.StartingWorkLoad 85\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nResearch.Group 0\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nUpgrades[0].CostOfUpgrade 1000\r\nUpgrades[0].InitCapacity 8\r\n"
              "Upgrades[0].ScrapValueYear1 50\r\nUpgrades[1].CostOfUpgrade 300\r\nUpgrades[1].CostOfResearch 10\r\n"
              "Upgrades[1].DurationOfUpgrade 1\r\nUpgrades[1].InitCapacity 12\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\n");
    writeText(world / "rides" / "rocket" / "rocket.sam",
              "Info.Id 1102\r\nResearch.Group 1\r\nUpgrades[0].CostOfUpgrade 2000\r\n"
              "Upgrades[0].CostOfResearch 20\r\nRStaff[0].SType 3\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\n");
    writeText(world / "Facilities" / "lab" / "lab.sam",
              "Info.Id 1450\r\nUsageInfo.ThinkingSpace 1\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    writeText(world / "Facilities" / "train_rm" / "train_rm.sam",
              "Info.Id 1440\r\nUsageInfo.TrainingRoom 1\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    const int q = park.place(1101, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.layQueue(q, Park::pathRun({2, 2}, {2, 4})) == 1);
    CHECK(park.place(1440, 0, {"*2"}, false, {5, 2}, 0) == -1);
    CHECK(!park.attractions().find(1102)->available);

    // Upgrades want research first; the rocket and, after it, the robot's
    // upgrade are the projects to choose from.
    CHECK(park.upgrade(0) == Park::Upgrading::kNotResearched);
    std::vector<Park::Project> choices = park.projectChoices();
    CHECK(choices.size() == 2);
    CHECK(choices.size() == 2 && choices[0].kind == Park::ProjectKind::kAttraction && choices[0].id == 1102 &&
          choices[0].cost == 20);
    CHECK(choices.size() == 2 && choices[1].kind == Park::ProjectKind::kUpgrade && choices[1].id == 1101 &&
          choices[1].level == 1 && choices[1].cost == 10);

    // A lab, then a scientist: on the rocket, held up for want of an
    // engineer to consult till one's hired.
    CHECK(park.hire(Park::StaffKind::kScientist, 0) == Park::Hiring::kNeedsLab);
    CHECK(!park.hasResearchLab() && park.place(1450, 0, {"*2"}, false, {0, 2}, 0) == -1 && park.hasResearchLab());
    CHECK(park.hire(Park::StaffKind::kScientist, 0) == Park::Hiring::kHired);
    const int scientist = park.staff().back().id;
    const int learnt = park.staff().back().experience;
    CHECK(park.projectHeldUp(0) == 0);
    park.startProject(0, choices[0]);
    CHECK(park.projectHeldUp(0) == Park::kHeldUpByNoneAssigned);
    park.assign(0, scientist);
    CHECK(park.projectHeldUp(0) == Park::kHeldUpByNoConsultant);
    CHECK(park.hire(Park::StaffKind::kEngineer, 0) == Park::Hiring::kHired);
    const int engineer = park.staff().back().id;
    CHECK(park.projectHeldUp(0) == 0);
    const int cash = park.cash();
    for (int t = 0; t < 2000 && !park.projects()[0]->finished; ++t)
        park.advance(100);
    CHECK(park.projects()[0]->finished && park.researched(1102) && park.attractions().find(1102)->available);
    CHECK(park.cash() <= cash - 20 && park.staffMember(scientist)->doing != Park::StaffDoing::kResearching);

    // The upgrade, in the other slot.
    choices = park.projectChoices();
    CHECK(choices.size() == 1);
    park.startProject(1, choices[0]);
    park.assign(1, scientist);
    for (int t = 0; t < 2000 && !park.projects()[1]->finished; ++t)
        park.advance(100);
    CHECK(park.researched(1101, 1) && park.projectChoices().empty());

    // Bought only once an engineer's trained; made in its days, its
    // capacity the next level's.
    CHECK(park.upgrade(0) == Park::Upgrading::kNeedsEngineer);
    CHECK(park.sendForTraining(engineer) == Park::Training::kSent);
    for (int t = 0; t < 200 && park.staffMember(engineer)->ability != Park::kRideUpgrading; ++t)
        park.advance(1000);
    const int before = park.cash();
    CHECK(park.upgrade(0) == Park::Upgrading::kStarted && park.cash() == before - 300);
    CHECK(park.upgrade(0) == Park::Upgrading::kBusy);
    for (int t = 0; t < 100 && park.objects()[0].level == 0; ++t)
        park.advance(1000);
    CHECK(park.objects()[0].level == 1 && park.levelOf(0)->capacity == 12);
    CHECK(park.upgrade(0) == Park::Upgrading::kFullyUpgraded);

    // Months gone by: the scientist the more experienced by 600 a month.
    const int gained = park.staffMember(scientist)->experience - learnt;
    CHECK(gained >= 600 && gained % 600 == 0);

    // Renamed; deleted for its scrap value, its queue with it.
    park.rename(0, "Tester's Swing");
    CHECK(park.objects()[0].name == "Tester's Swing");
    const size_t objects = park.objects().size();
    const int scrap = park.scrapValue(0);
    const int had = park.cash();
    park.remove(0);
    CHECK(park.objects().size() == objects - 1 && park.cash() == had + scrap && park.queueOf(0) < 0);
    CHECK(park.ground({2, 2}) == Park::Ground::kOpen && park.hasResearchLab());
}

// Money: a loan taken, repaid a month with its interest, paid off; prices
// set by kind; the money running out, warned of, and the park bankrupt
// after six months in the red; a year's report, the share price as it was
// for a year that made nothing.
void testFinance(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 1000\r\nBankAccountInfo.InitialAdmissionFee 25\r\n"
              "Arrival.MinPeople 0\r\nArrival.FixedRate 0\r\n"
              "LoanInfo[0].LoanAmount 12000\r\nLoanInfo[0].APRInPercent 12\r\n"
              "LoanInfo[0].RepaymentPeriodInMonths 12\r\nLoanInfo[0].Lendername 2\r\n"
              "LoanInfo[1].LoanAmount 5000\r\nLoanInfo[1].APRInPercent 0\r\n"
              "LoanInfo[1].RepaymentPeriodInMonths 5\r\nLoanInfo[1].Lendername 1\r\n"
              "ShareInfo.CompanySharesIssued 30000\r\nShareInfo.InitialSharesOwned 1000\r\n"
              "ShareInfo.InitialSharePrice 100\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "Standard.sam", "LoanInfo[1].Lendername 7\r\n");
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nUpgrades[0].CostOfUpgrade 100\r\nUpgrades[0].RunningCost 2000\r\n"
              "Upgrades[0].ScrapValueYear1 50\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    auto toDay = [&](uint32_t day) {
        while (park.day() < day)
            park.advance(1000);
    };
    auto posted = [](const std::vector<Park::Message> &messages, uint32_t text) {
        return std::any_of(messages.begin(), messages.end(), [&](const Park::Message &m) { return m.text == text; });
    };

    // The offers: 12,000 over a year at 12% is 1,066 a month; the world's
    // lender over the general one's.
    std::vector<Park::Loan> offers = park.availableLoans();
    CHECK(offers.size() == 2);
    CHECK(offers.size() == 2 && offers[0].repayment == 1066 && offers[0].totalPayable() == 12792 &&
          offers[0].lender == 2);
    CHECK(offers.size() == 2 && offers[1].repayment == 1000 && offers[1].lender == 7);
    CHECK(park.shares().issued == 30000 && park.shares().owned == 1000 && park.shares().price == 100);

    // Taken once; on 1 February a month's interest is added and a repayment made.
    CHECK(park.takeLoan(0) && !park.takeLoan(0) && park.cash() == 13000 && park.owed() == 12000);
    CHECK(park.availableLoans().size() == 1);
    toDay(31);
    CHECK(park.loans().size() == 1 && park.loans()[0].balance == 11054 && park.loans()[0].monthsLeft == 11);
    CHECK(park.cash() == 11934 && park.lastMonth()[size_t(Park::Ledger::kLoans)] == 1066);
    CHECK(Park::expenditure(park.lastMonth()) == 1066 && Park::income(park.lastMonth()) == 0);
    CHECK(park.repayLoan(0) && park.cash() == 880 && park.loans().empty() && park.owed() == 0);

    // Prices by kind: the entry fee set, the rides' raised half again.
    const int q = park.place(1101, 100, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.thisYear()[size_t(Park::Ledger::kBuilding)] == 100 && park.parkValue() == 50);
    park.setFare(q, 10);
    park.adjustPrices(Park::PriceKind::kEntry, Park::Adjustment::kSetTo, 30);
    park.adjustPrices(Park::PriceKind::kRide, Park::Adjustment::kPercent, 50);
    CHECK(park.admissionFee() == 30 && park.queues()[size_t(q)].fare == 15);

    // The ride's running cost puts the park in the red on 1 March: warned
    // when the money ran low, then out, after three months and five; six,
    // and it's bankrupt.
    std::vector<Park::Message> messages;
    for (uint32_t day = park.day(); day < 400 && !park.bankrupt(); day += 1) {
        toDay(day + 1);
        for (Park::Message &m : park.takeMessages())
            messages.push_back(std::move(m));
    }
    CHECK(posted(messages, 106) && posted(messages, 34) && posted(messages, 36) && posted(messages, 37));
    CHECK(park.bankrupt() && park.monthsInRed() == 6 && posted(messages, 166));
    CHECK(Park::dateOf(park.day()).month == 7); // 1 August

    // A year of nothing: the report has its five lines and the share news,
    // which is that the price is as it was; the year's accounts begin again.
    Park quiet;
    CHECK(!quiet.load(files, "levels/science", 0));
    while (quiet.day() < 366)
        quiet.advance(1000);
    const std::vector<Park::Message> news = quiet.takeMessages();
    const auto report = std::find_if(news.begin(), news.end(), [](const Park::Message &m) { return m.title == 262; });
    CHECK(report != news.end() && report->text == 263 && report->lines.size() == 6);
    CHECK(report != news.end() && report->lines.size() == 6 && report->lines[5].text == 267);
    CHECK(quiet.shares().price == 100 && Park::income(quiet.thisYear()) == 0);
}

// The campaign: Operation Expand!'s visitors open the gates and allow
// research; Plughole's lake researched as a Park Expansion, a gardener
// trained in Water/Drainage for it, then drained; Manager's promotion
// opens Polar Zone, where the next objective is. A park set up later in
// the campaign has what the missions done there left it.
void testMissions(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "1000 2000 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 100000\r\nArrival.MinPeople 1\r\nArrival.TimeBetweenArrivals 1\r\n"
              "Arrival.FixedRate 2\r\nArrival.PointsPerVisitor 1\r\nPeepInfo.PercentageOfKids 100\r\n"
              "StaffPoolInfo.BeginningNumberOfGardeners 2\r\nStaffPoolInfo.BeginningNumberOfResearchers 2\r\n"
              "StaffPoolInfo.AvgExperienceOfGardeners 40000\r\nStaffPoolInfo.AvgWageDifference 0\r\n"
              "StaffAgencies[0].InitialWageModifier 10\r\nStaffAgencies[0].InitialExperienceModifier 0\r\n"
              "StaffAgencies[0].TrainingExperienceGainModifier 10\r\n"
              "AllStaffConstants.MinExperienceForTraining 30000\r\n" +
                  types);
    const std::filesystem::path missions = data / "missions";
    writeText(missions / "miss00.sam", "Info.Number 0\r\nInfo.NoResearch 1\r\nCompletion.EndingTheme 1\r\n"
                                       "Completion.ObstacleOpened 1700\r\nExitStatistic[0].Type 8\r\n"
                                       "ExitStatistic[0].TargetValue 1\r\nObstacle.Type 1700\r\n");
    writeText(missions / "miss01.sam",
              "Info.Number 1\r\nInfo.NoResearch 0\r\nTechLevel.NewMaxGroup 1\r\nTechLevel.Theme 1\r\n"
              "Completion.EndingTheme 1\r\nExitStatistic[0].Type 0\r\nObstacle.Type 1703\r\n"
              "Obstacle.ResearchEffort 20\r\nObstacle.GoldenTickets 0\r\nObstacleStaff[0].Ability 10\r\n"
              "ObstacleStaff[0].StaffType 5\r\nObstacleStaff[0].Number 1\r\n");
    writeText(missions / "miss02.sam", "Info.Number 2\r\nCompletion.EndingTheme 1\r\nCompletion.ThemeOpened 1\r\n"
                                       "Completion.ThemeNumber 2\r\nExitStatistic[0].Type 0\r\nPromotion.Number 2\r\n");
    writeText(missions / "miss03.sam", "Info.Number 3\r\nCompletion.EndingTheme 2\r\nExitStatistic[0].Type 8\r\n"
                                       "ExitStatistic[0].TargetValue 50\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nResearch.Group 0\r\n");
    for (auto [id, group] : {std::pair{1102, 1}, std::pair{1103, 2}})
        writeText(world / "rides" / ("r" + std::to_string(id)) / ("r" + std::to_string(id) + ".sam"),
                  "Info.Id " + std::to_string(id) + "\r\nResearch.Group " + std::to_string(group) +
                      "\r\nUpgrades[0].CostOfUpgrade 1000\r\nUpgrades[0].CostOfResearch 10\r\n"
                      "Info.Shape\r\n---\r\n2N\r\n---\r\n");
    writeText(world / "Facilities" / "lab" / "lab.sam",
              "Info.Id 1450\r\nUsageInfo.ThinkingSpace 1\r\nUpgrades[0].CostOfUpgrade 500\r\n"
              "Research.Group 0\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    writeText(world / "Facilities" / "train_rm" / "train_rm.sam",
              "Info.Id 1440\r\nUsageInfo.TrainingRoom 1\r\nUpgrades[0].CostOfUpgrade 500\r\n"
              "Research.Group 0\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    writeText(world / "obstacles" / "gatesobstacle" / "gatesobstacle.sam",
              "Info.Id 1700\r\nInfo.IsObstacle 1\r\nObstacle.OpenedAutomaticly 1\r\nUpgrades[0].CostOfUpgrade 150\r\n");
    writeText(world / "obstacles" / "lake" / "lake.sam",
              "Info.Id 1703\r\nInfo.IsObstacle 1\r\nObstacle.ReplaceWithID 1704\r\nUpgrades[0].CostOfUpgrade 150\r\n");
    writeText(world / "obstacles" / "dlake" / "dlake.sam", "Info.Id 1704\r\nInfo.IsObstacle 1\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.theme() == 1 && park.missions().size() == 4 && park.mission().number == 0);
    std::vector<Park::Message> messages;
    auto advance = [&](uint32_t ms) {
        park.advance(ms);
        for (Park::Message &m : park.takeMessages())
            messages.push_back(std::move(m));
    };
    auto letter = [&](uint32_t mission) {
        return std::any_of(messages.begin(), messages.end(),
                           [&](const Park::Message &m) { return m.objectiveLetter && m.text == mission; });
    };

    // Operation Expand!: no research, nothing on offer to research; the
    // obstacles stand.
    const Attractions::Attraction *lab = park.attractions().find(1450);
    CHECK(!park.researchAllowed() && park.projectChoices().empty() && lab && !lab->available);
    CHECK(park.attractions().find(1700) && park.attractions().find(1700)->obstacle &&
          !park.attractions().find(1700)->available);
    CHECK(park.place(1700, 0, {}, false, {6, 1}, 0) == -1 && park.place(1703, 0, {}, false, {6, 5}, 0) == -1);
    CHECK(park.proceedWithSolution() == Park::Solution::kNoObstacle);

    // The first visitor in, it's done: its letter, the gates gone, Plughole's begun.
    for (int t = 0; t < 600 && park.mission().number == 0; ++t)
        advance(100);
    CHECK(letter(0) && park.mission().number == 1 && park.progress() == 1);
    // The visitor counted as they came in, a child, what they paid kept.
    CHECK(park.totalVisitors() >= 1 && park.totalVisitors(0) == park.totalVisitors() && park.totalVisitors(1) == 0);
    CHECK(std::any_of(park.visitorsAbout().begin(), park.visitorsAbout().end(),
                      [](const Park::Visitor &v) { return v.inPark && v.arrived > 0; }));
    CHECK(park.objects().size() == 1 && park.objects()[0].id == 1703);
    CHECK(park.researchAllowed() && park.attractions().find(1450)->available &&
          !park.attractions().find(1440)->available);

    // Its choices: the lake first, then the group 1 ride, not group 2's.
    std::vector<Park::Project> choices = park.projectChoices();
    const auto chosen = [&](int id) {
        return std::find_if(choices.begin(), choices.end(), [&](const Park::Project &p) { return p.id == id; });
    };
    CHECK(!choices.empty() && choices[0].id == 1703);
    CHECK(chosen(1102) != choices.end() && chosen(1103) == choices.end());
    CHECK(chosen(1703) != choices.end() && chosen(1703)->kind == Park::ProjectKind::kExpansion &&
          chosen(1703)->cost == 20);

    // Researched: a training room on offer, gardeners to learn Water/Drainage (10).
    CHECK(park.proceedWithSolution() == Park::Solution::kNotResearched);
    CHECK(park.place(1450, 0, {"*2"}, false, {0, 2}, 0) == -1);
    CHECK(park.hire(Park::StaffKind::kScientist, 0) == Park::Hiring::kHired);
    park.startProject(0, *chosen(1703));
    park.assign(0, park.staff().back().id);
    for (int t = 0; t < 3000 && !park.obstacleResearched(); ++t)
        advance(100);
    CHECK(park.obstacleResearched() && park.attractions().find(1440)->available);
    CHECK(std::any_of(messages.begin(), messages.end(), [](const Park::Message &m) { return m.text == 255; }));
    CHECK(park.abilityToLearn(Park::StaffKind::kGardener) == 10 &&
          park.abilityToLearn(Park::StaffKind::kEngineer) == 0);
    CHECK(park.proceedWithSolution() == Park::Solution::kNeedsStaff);

    // A gardener trained, the lake's drained: the drained lake in its
    // place, its cost paid, Manager's begun.
    CHECK(park.place(1440, 0, {"*2"}, false, {5, 2}, 0) == -1);
    CHECK(park.hire(Park::StaffKind::kGardener, 0) == Park::Hiring::kHired);
    const int gardener = park.staff().back().id;
    CHECK(park.sendForTraining(gardener) == Park::Training::kSent);
    for (int t = 0; t < 200 && park.staffMember(gardener)->ability != 10; ++t)
        advance(1000);
    CHECK(park.obstacleStaffReady() && park.obstacleStaff()[0] == std::vector<int>{gardener});
    CHECK(park.obstacleCandidates() == std::vector<int>{gardener});
    // The scientist won't do; the gardener chosen goes to the lake, and
    // after their days at it, it's drained.
    const int scientist = park.staff().front().id;
    CHECK(park.proceedWithSolution({scientist}) == Park::Solution::kNeedsStaff);
    const int cash = park.cash();
    CHECK(park.proceedWithSolution({gardener}) == Park::Solution::kStarted && park.cash() == cash - 150);
    CHECK(park.clearing() && park.proceedWithSolution({gardener}) == Park::Solution::kUnderWay);
    const Park::StaffDoing going = park.staffMember(gardener)->doing;
    CHECK(going == Park::StaffDoing::kGoingToClear || going == Park::StaffDoing::kClearing);
    CHECK(park.objects()[0].id == 1703 && park.mission().number == 1);
    for (int t = 0; t < 40 && park.clearing(); ++t)
        advance(1000);
    CHECK(!park.clearing() && park.staffMember(gardener)->doing == Park::StaffDoing::kWorking);
    advance(0);
    CHECK(park.objects()[0].id == 1704 && letter(1) && park.mission().number == 2);

    // Manager's wants a promotion; given it, Polar Zone opens, and the
    // next objective's there: none here.
    advance(8000);
    CHECK(park.mission().number == 2);
    park.setPlayer(0, 2);
    advance(8000);
    CHECK(letter(2) && park.progress() == 3 && park.mission().number == -1);
    CHECK(park.themesOpened() == std::vector<int>{2});

    // The bulldozer takes up path, not bare land.
    CHECK(park.isPath({6, 4}) && park.removePath({6, 4}) && park.ground({6, 4}) == Park::Ground::kOpen);
    CHECK(!park.removePath({6, 4}) && !park.removePath({6, 5}));

    // Set up as Manager's: Plughole's group to research, the training room
    // on offer; no lake to research.
    Park later;
    CHECK(!later.load(files, "levels/science", 2));
    choices = later.projectChoices();
    CHECK(later.mission().number == 2 && chosen(1102) != choices.end() && chosen(1103) == choices.end() &&
          chosen(1703) == choices.end());
    CHECK(later.attractions().find(1440)->available);
}

// Challenges: this world's offered one each DaysBetweenOffers, by the
// director who sets them; a visitors challenge prepared for, then failed
// at once, the share price down, offered again; a hot dog challenge set
// off by a hot dog stand, won by its sales, its prizes paid; the Safety
// Review's promotion doing the mission that wanted it; a player failing
// past the firing line fired.
void testChallenges(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "1000 2000 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 100000\r\nArrival.MinPeople 1\r\nArrival.TimeBetweenArrivals 1\r\n"
              "Arrival.FixedRate 2\r\nArrival.PointsPerVisitor 1\r\nPeepInfo.PercentageOfKids 100\r\n"
              "Challenges.DaysBetweenOffers 5\r\nFiringLineForRank[1].Limit 2\r\n" +
                  types);
    writeText(data / "Challenges.sam",
              "Challenges[2].Type 2\r\nChallenges[2].TargetTime 40\r\nChallenges[2].TargetVal 2\r\n"
              "Challenges[2].CashPrize 500\r\nChallenges[2].GTPrize 2\r\nChallenges[2].SharePrize 100\r\n"
              "Challenges[2].SharePriceEffectWon 10\r\nChallenges[2].TriggerData 1\r\nChallenges[2].Sender 2\r\n"
              "Challenges[3].Type 37\r\nChallenges[3].TargetTime 10\r\nChallenges[3].TargetVal 1000\r\n"
              "Challenges[3].GracePeriodLimit 3\r\nChallenges[3].SharePriceEffectLost 10\r\n"
              "Challenges[14].Type 46\r\nChallenges[14].TargetTime 5\r\nChallenges[14].TargetVal 10\r\n"
              "Challenges[14].GracePeriodLimit 2\r\nChallenges[14].PromoGiven 2\r\nChallenges[14].Sender 3\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\nCompletion.EndingTheme 1\r\nPromotion.Number 2\r\n");
    writeText(data / "missions" / "miss01.sam", "Info.Number 1\r\nCompletion.EndingTheme 2\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "Standard.sam", "ChallengesInThisLevel[0].ChallengeType 3\r\n"
                                      "ChallengesInThisLevel[1].ChallengeType 2\r\n"
                                      "ChallengesInThisLevel[2].ChallengeType 14\r\n");
    writeText(data / "levels" / "water" / "Standard.sam", "ChallengesInThisLevel[0].ChallengeType 7\r\n");
    writeText(world / "shops" / "Shops.sam", "Info.HasQueue 0\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "shops" / "hotdog" / "hotdog.sam",
              "Info.Id 1207\r\nUsageInfo.ShopType 2\r\nUsageInfo.InitPricePerUse 5\r\nInfo.ExpectedPrice 5\r\n"
              "UsageInfo.HappinessEffect 30\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.challenges().size() == 3 && park.challenges()[0].challenge.number == 3 &&
          park.challenges()[2].challenge.promotion == 2 && park.challenges()[2].challenge.tickets == 1);
    std::vector<Park::Message> messages;
    auto day = [&]() {
        const uint32_t next = park.day() + 1;
        while (park.day() < next) {
            park.advance(100);
            if (park.queues().empty())
                continue;
            if (const int id = park.waitingAt(0); id && park.board(0, id))
                park.letOff(0, id);
        }
        for (Park::Message &m : park.takeMessages())
            messages.push_back(std::move(m));
    };
    auto status = [&](size_t i) { return park.challenges()[i].status; };
    auto until = [&](auto &&done, int days) {
        for (int d = 0; d < days && !done(); ++d)
            day();
        return done();
    };

    // The visitors challenge, set off at once, offered by the president.
    CHECK(until([&] { return status(0) == Park::ChallengeStatus::kAvailable; }, 3));
    CHECK(!messages.empty() && messages.back().title == 42 && messages.back().text == 43 &&
          messages.back().challenge == 3 && messages.back().sender == 173);
    // Prepared for three days, then failed as it runs: a thousand visitors aren't there.
    const int price = park.shares().price;
    CHECK(park.acceptChallenge(0) && status(0) == Park::ChallengeStatus::kPreparing);
    CHECK(!park.acceptChallenge(0));
    CHECK(until([&] { return status(0) != Park::ChallengeStatus::kPreparing; }, 4) &&
          status(0) == Park::ChallengeStatus::kActive);
    CHECK(until([&] { return status(0) != Park::ChallengeStatus::kActive; }, 2));
    CHECK(status(0) == Park::ChallengeStatus::kNotYet && park.challenges()[0].failures == 1 &&
          park.challengesFailed() == 1 && park.shares().price == price * 90 / 100 && park.failuresLeft() == 1);

    // A hot dog stand sets the hot dog challenge off; taken up, its two
    // sales win it: a director's letter, cash, two tickets, shares.
    CHECK(park.place(1207, 0, {"*2"}, false, {4, 3}, 0) == -1);
    CHECK(until([&] { return status(1) == Park::ChallengeStatus::kAvailable; }, 12));
    const int cash = park.cash();
    CHECK(park.acceptChallenge(1) && status(1) == Park::ChallengeStatus::kActive);
    CHECK(until([&] { return status(1) == Park::ChallengeStatus::kComplete; }, 40));
    CHECK(park.goldenTickets() == 2 && park.shares().owned == 1100 && park.cash() >= cash + 500);
    CHECK(std::any_of(messages.begin(), messages.end(),
                      [](const Park::Message &m) { return m.title == 46 && m.text == 55 && m.sender == 177; }));
    CHECK(park.challengesWon() == std::vector<int>{2} && park.challengesWonIn(1) == 1 && park.challengesWonIn(2) == 0);

    // The Safety Review, its rides never worn: won at its end, a
    // promotion to Manager, which does the mission waiting for it the
    // day after.
    CHECK(until([&] { return status(2) == Park::ChallengeStatus::kAvailable; }, 12));
    CHECK(park.acceptChallenge(2));
    CHECK(until([&] { return status(2) == Park::ChallengeStatus::kComplete; }, 10));
    CHECK(park.rank() == 2);
    CHECK(until([&] { return park.progress() == 1; }, 2));
    CHECK(std::any_of(messages.begin(), messages.end(),
                      [](const Park::Message &m) { return m.title == 169 && m.rank == 2; }));

    // One more failure than the firing line allows: fired.
    Park doomed;
    CHECK(!doomed.load(files, "levels/science", 0));
    doomed.setChallengeRecord({}, 1);
    CHECK(doomed.failuresLeft() == 1);
    for (int d = 0; d < 3 && doomed.challenges()[0].status != Park::ChallengeStatus::kAvailable; ++d)
        doomed.advance(Park::kDayMillis);
    CHECK(doomed.acceptChallenge(0));
    for (int d = 0; d < 6 && !doomed.fired(); ++d)
        doomed.advance(Park::kDayMillis);
    CHECK(doomed.fired() && doomed.failuresLeft() == 0);
}

// Special rides: a boating lake boarded up, wanting a guard in First Aid
// and a gardener in Landfill, which the swamp's research discovers, and a
// golden ticket. Activated, it opens, and its challenge, a special
// ride's, is offered and won for a promotion. The player's rides
// activated are set again on a new park.
void testSpecialRides(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "1000 2000 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 100000\r\nArrival.MinPeople 2\r\nArrival.TimeBetweenArrivals 1\r\n"
              "Arrival.FixedRate 4\r\nArrival.PointsPerVisitor 1\r\nPeepInfo.PercentageOfKids 100\r\n"
              "PeepInfo.PrankeryLikelihood 0\r\nChallenges.DaysBetweenOffers 1\r\n"
              "StaffPoolInfo.BeginningNumberOfGardeners 2\r\nStaffPoolInfo.BeginningNumberOfGuards 2\r\n"
              "StaffPoolInfo.AvgExperienceOfGardeners 40000\r\nStaffPoolInfo.AvgExperienceOfGuards 40000\r\n"
              "StaffPoolInfo.AvgWageDifference 0\r\nStaffAgencies[0].InitialWageModifier 10\r\n"
              "StaffAgencies[0].InitialExperienceModifier 0\r\nAllStaffConstants.MinExperienceForTraining 30000\r\n" +
                  types);
    writeText(data / "Challenges.sam", "Challenges[20].Type 48\r\nChallenges[20].TargetTime 3\r\n"
                                       "Challenges[20].TargetVal 1\r\nChallenges[20].TargetObj 1110\r\n"
                                       "Challenges[20].PromoGiven 3\r\nChallenges[20].Sender 4\r\n");
    // The swamp's mission, done in Polar Zone, needs Landfill (11).
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\nCompletion.EndingTheme 2\r\n"
                                                "Obstacle.Type 2700\r\nObstacle.ResearchEffort 10\r\n"
                                                "ObstacleStaff[0].Ability 11\r\nObstacleStaff[0].StaffType 5\r\n");
    writeText(data / "missions" / "miss01.sam", "Info.Number 1\r\nCompletion.EndingTheme 2\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "Standard.sam", "ChallengesInThisLevel[0].ChallengeType 20\r\n");
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nResearch.Group 0\r\n");
    writeText(world / "rides" / "vict_hs" / "vict_hs.sam",
              "Info.Id 1110\r\nInfo.IsSpecial 1\r\nUsageInfo.GoldenTicketCost 1\r\nSkillSet[9].Num 1\r\n"
              "SkillSet[11].Num 1\r\nUsageInfo.ExcitementLevel 40\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\n"
              "Info.Hoarding\r\n---\r\nF7\r\n---\r\n");
    writeText(world / "Facilities" / "train_rm" / "train_rm.sam",
              "Info.Id 1440\r\nUsageInfo.TrainingRoom 1\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));

    // Before the swamp's mission's done, Landfill isn't known.
    Park early;
    CHECK(!early.load(files, "levels/science", 0));
    const int q0 = early.place(1110, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q0 >= 0 && early.awaitsActivation(0) && !early.objects()[0].open);
    // Its hoarding, "F7" over its two cells from (2, 2): their top and
    // sides boarded, a diagonal at each top corner, an end at each foot.
    const std::vector<Park::FencePiece> hoarding = early.hoardingPieces(0);
    CHECK(hoarding.size() == 4);
    int diagonals = 0;
    int ends = 0;
    for (const Park::FencePiece &piece : hoarding) {
        diagonals += piece.model == Park::FenceModel::kDiagonal;
        ends += piece.model == Park::FenceModel::kEnd;
    }
    CHECK(diagonals == 2 && ends == 2);
    CHECK(early.abilityDiscovered(9) && !early.abilityDiscovered(11));
    CHECK(early.activate(0) == Park::Activation::kUndiscovered);

    Park park;
    CHECK(!park.load(files, "levels/science", 1));
    CHECK(park.abilityDiscovered(11) && Park::abilityKind(11) == Park::StaffKind::kGardener &&
          Park::abilityKind(9) == Park::StaffKind::kGuard);
    const int q = park.place(1110, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.layQueue(q, Park::pathRun({2, 2}, {2, 4})) == 1);
    CHECK(park.awaitsActivation(0) && park.challenges()[0].status == Park::ChallengeStatus::kNotYet);
    park.setOpen(0, true);
    CHECK(!park.objects()[0].open);
    CHECK(park.activate(0) == Park::Activation::kNeedsStaff);

    // A guard and a gardener trained: each learns the ride's ability of their kind.
    CHECK(park.place(1440, 0, {"*2"}, false, {5, 2}, 0) == -1);
    CHECK(park.hire(Park::StaffKind::kGuard, 0) == Park::Hiring::kHired);
    const int guard = park.staff().back().id;
    CHECK(park.hire(Park::StaffKind::kGardener, 0) == Park::Hiring::kHired);
    const int gardener = park.staff().back().id;
    CHECK(park.abilityToLearn(Park::StaffKind::kGuard) == 9 && park.abilityToLearn(Park::StaffKind::kGardener) == 11);
    // The guard's is chosen for them; the gardener's left to the park.
    const std::vector<int> guards = park.learnable(Park::StaffKind::kGuard);
    CHECK(std::find(guards.begin(), guards.end(), 9) != guards.end());
    const std::vector<int> gardeners = park.learnable(Park::StaffKind::kGardener);
    CHECK(std::find(gardeners.begin(), gardeners.end(), 11) != gardeners.end());
    CHECK(park.learnable(Park::StaffKind::kScientist).empty());
    CHECK(park.trainingCheck(guard) == Park::Training::kSent);
    CHECK(park.sendForTraining(guard, 9) == Park::Training::kSent && park.staffMember(guard)->learning == 9);
    CHECK(park.sendForTraining(gardener) == Park::Training::kSent);
    for (int t = 0; t < 200 && (park.staffMember(guard)->ability != 9 || park.staffMember(gardener)->ability != 11);
         ++t)
        park.advance(1000);
    CHECK(park.staffMember(guard)->ability == 9 && park.staffMember(gardener)->ability == 11);
    CHECK(park.activationStaff(0) == (std::vector<std::vector<int>>{{guard}, {gardener}}));

    // Its golden ticket, then it's open; its challenge's set off.
    CHECK(park.activate(0) == Park::Activation::kNeedsTickets);
    park.setPlayer(1, 2);
    CHECK(park.activate(0) == Park::Activation::kActivated && park.goldenTickets() == 0 && park.objects()[0].open);
    CHECK(park.ridesActivated() == std::vector<int>{1110});
    for (int t = 0; t < 10 && park.challenges()[0].status != Park::ChallengeStatus::kAvailable; ++t)
        park.advance(Park::kDayMillis);
    CHECK(park.acceptChallenge(0));
    for (int t = 0; t < 10 && park.challenges()[0].status != Park::ChallengeStatus::kComplete; ++t)
        park.advance(Park::kDayMillis);
    CHECK(park.challenges()[0].status == Park::ChallengeStatus::kComplete && park.rank() == 3);

    // A new park of the player's has it open from the start.
    Park again;
    CHECK(!again.load(files, "levels/science", 1));
    CHECK(again.place(1110, 0, {"2N"}, true, {2, 2}, 0) >= 0 && again.awaitsActivation(0));
    again.setRidesActivated({1110});
    CHECK(!again.awaitsActivation(0) && again.objects()[0].open && again.hoardingPieces(0).empty());
}

void testAwards(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(
        data / "levels" / "Standard.sam",
        "BankAccountInfo.InitialCash 100000\r\nArrival.MinPeople 0\r\nStaffPoolInfo.BeginningNumberOfMechanics 2\r\n"
        "StaffPoolInfo.AvgExperienceOfMechanics 40000\r\nStaffPoolInfo.AvgWageDifference 0\r\n"
        "StaffAgencies[0].InitialWageModifier 10\r\nStaffAgencies[0].InitialExperienceModifier 0\r\n"
        "AllStaffConstants.MinExperienceForTraining 30000\r\nParkAward.Hygiene 90\r\n"
        "ParkAward.HygieneMinParkSize 5\r\nParkAward.Decoration 50\r\nParkAward.Security 25\r\n"
        "ParkAward.Training 50\r\nParkAward.TrainingMinStaff 2\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\nExitStatistic[0].Type 6\r\n"
                                                "ExitStatistic[0].TargetValue 3\r\n");
    writeText(data / "missions" / "miss01.sam", "Info.Number 1\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "features" / "statue" / "statue.sam", "Info.Id 1500\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    writeText(world / "Facilities" / "camera" / "camera.sam",
              "Info.Id 1450\r\nUsageInfo.ProvidesSecurity 1\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    writeText(world / "Facilities" / "train_rm" / "train_rm.sam",
              "Info.Id 1440\r\nUsageInfo.TrainingRoom 1\r\nInfo.Shape\r\n---\r\n*2\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    const auto held = [&](Park::Award a) { return park.awards()[size_t(a)].held; };

    // An empty park holds none; the rides' awards aren't measured.
    CHECK(park.awardsHeld() == 0 && !park.awards()[size_t(Park::Award::kHighestCoaster)].measured);
    CHECK(park.awards()[size_t(Park::Award::kHighestCoaster)].needed == 110);

    // A statue and a camera: half the covered cells a feature's, the park
    // watched; too few cells covered to be the cleanest till the Training
    // Room's built, which leaves the statue too few.
    CHECK(park.place(1500, 0, {"*2"}, false, {1, 1}, 0) == -1);
    CHECK(park.place(1450, 0, {"*2"}, false, {3, 1}, 0) == -1);
    CHECK(held(Park::Award::kMostDecoration) && held(Park::Award::kBestSecurity));
    CHECK(park.awards()[size_t(Park::Award::kMostDecoration)].value == 50);
    CHECK(!held(Park::Award::kCleanestPark));
    CHECK(park.place(1440, 0, {"*2"}, false, {5, 1}, 0) == -1);
    CHECK(held(Park::Award::kCleanestPark) && !held(Park::Award::kMostDecoration));
    CHECK(park.awardsHeld() == 2 && park.statisticValue(6) == 2);

    // Half of two engineers trained.
    CHECK(park.hire(Park::StaffKind::kEngineer, 0) == Park::Hiring::kHired);
    const int engineer = park.staff().back().id;
    CHECK(park.hire(Park::StaffKind::kEngineer, 0) == Park::Hiring::kHired);
    CHECK(!held(Park::Award::kBestStaffTraining));
    CHECK(park.sendForTraining(engineer) == Park::Training::kSent);
    for (int t = 0; t < 200 && park.staffMember(engineer)->ability == 0; ++t)
        park.advance(1000);
    CHECK(held(Park::Award::kBestStaffTraining) && park.awardsHeld() == 3);
    CHECK(park.statisticValue(6) == 3);
}

// Fences laid in pieces: a square's corners cut across and its sides'
// straights facing in; a run of three edges between two ends, an end
// piece to close the odd edge.
void testFencePieces() {
    using Edge = Park::Edge;
    std::vector<Edge> square;
    for (int i = 0; i < 4; ++i) {
        square.push_back({{i, 0}, {i + 1, 0}});
        square.push_back({{i, 4}, {i + 1, 4}});
        square.push_back({{0, i}, {0, i + 1}});
        square.push_back({{4, i}, {4, i + 1}});
    }
    auto inside = [](const Park::Cell &c) { return c.x >= 0 && c.x < 4 && c.y >= 0 && c.y < 4; };
    const std::vector<Park::FencePiece> pieces = Park::fencePieces(square, inside);
    CHECK(pieces.size() == 8);
    for (const Park::FencePiece &p : pieces) {
        if (p.at.x == 0 && p.at.y == 0)
            CHECK(p.model == Park::FenceModel::kDiagonal && p.angle == 0);
        if (p.at.x == 4 && p.at.y == 4)
            CHECK(p.model == Park::FenceModel::kDiagonal && p.angle == 180);
        if (p.at.x == 2 && p.at.y == 0)
            CHECK(p.model == Park::FenceModel::kStraight && p.angle == 90);
        if (p.at.x == 0 && p.at.y == 2)
            CHECK(p.model == Park::FenceModel::kStraight && p.angle == 0);
        if (p.at.x == 4 && p.at.y == 2)
            CHECK(p.model == Park::FenceModel::kStraight && p.angle == 180);
        CHECK(p.look >= 1 && p.look <= 4);
    }
    const std::vector<Park::FencePiece> run =
        Park::fencePieces({{{0, 0}, {1, 0}}, {{1, 0}, {2, 0}}, {{2, 0}, {3, 0}}}, inside);
    CHECK(run.size() == 3);
    for (const Park::FencePiece &p : run)
        CHECK(p.model == Park::FenceModel::kEnd && p.look == 1);
}

void testLand(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam", "BankAccountInfo.InitialCash 100000\r\nArrival.MinPeople 0\r\n");
    // The first mission opens the whole map, from by the entrance; the
    // second opens nothing.
    writeText(data / "missions" / "miss00.sam",
              "Info.Number 0\r\nCompletion.EndingTheme 1\r\nExitStatistic[0].Type 8\r\n"
              "ExitStatistic[0].TargetValue 0\r\nCompletion.LoadNewHoardings 1\r\n"
              "BoundaryClicks[0].X -7\r\nBoundaryClicks[0].Y -1\r\nBoundaryClicks[1].X 1\r\n"
              "BoundaryClicks[1].Y -1\r\nBoundaryClicks[2].X 1\r\nBoundaryClicks[2].Y 5\r\n"
              "BoundaryClicks[3].X -7\r\nBoundaryClicks[3].Y 5\r\nBoundaryClicks[4].X 512\r\n"
              "BoundaryClicks[4].Y 512\r\n");
    writeText(data / "missions" / "miss01.sam", "Info.Number 1\r\nCompletion.EndingTheme 1\r\n"
                                                "Completion.LoadNewHoardings 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "Standard.sam", "FixedItemInfo.EntranceAPosX 2\r\nFixedItemInfo.EntranceAPosY 0\r\n"
                                      "FixedItemInfo.EntranceBPosX 3\r\nFixedItemInfo.EntranceBPosY 0\r\n");
    writeText(world / "shops" / "Shops.sam", "Info.HasQueue 0\r\nInfo.IsChoosable 1\r\n");
    writeText(world / "shops" / "balloon" / "balloon.sam", "Info.Id 1209\r\nInfo.Shape\r\n---\r\n*\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    // The park's to start with: x 0 to 3.
    tpw::testing::Bytes hoardings(size_t(128) * 128, 1);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < kColumns; ++y)
            hoardings[size_t(x) * 128 + size_t(y)] = 0;
    test::writeFile(world / "hoardings.map", hoardings);
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));

    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.missions()[0].boundary.size() == 4 && park.missions()[0].newLand);
    CHECK(park.fence().empty() && park.fencePieces().empty());
    CHECK(park.owns({3, 5}) && !park.owns({4, 0}) && park.ground({5, 2}) == Park::Ground::kOutside);
    CHECK(park.place(1209, 0, {"*"}, false, {5, 2}, 0) == Park::kNotPlaced);
    CHECK(park.place(1209, 0, {"*"}, false, {1, 2}, 0) == -1);
    // Its objective met the first day, the land beyond is the park's.
    for (int t = 0; t < 3 && park.progress() == 0; ++t)
        park.advance(Park::kDayMillis);
    CHECK(park.progress() == 1 && park.owns({7, 5}) && park.ground({5, 2}) == Park::Ground::kOpen);
    // Its fence stands in the world's own's place, from by the entrance.
    CHECK(park.fence().size() == 4 && park.fence()[0].x == 0 && park.fence()[0].y == 0 && !park.fencePieces().empty());
    CHECK(park.place(1209, 0, {"*"}, false, {5, 2}, 0) == -1);

    // A park set up after it has the land from the start.
    Park later;
    CHECK(!later.load(files, "levels/science", 1));
    CHECK(later.owns({7, 0}) && later.owns({0, 0}));
}

// A go-kart track laid from beside its station: the first run only along
// its side; north, east, then west straight across its own track, south
// and east, and back north into its first block, which closes it. Its
// pieces, its way round, its cost and its longest straight.
void testTracks(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 10000\r\nArrival.MinPeople 0\r\nCosts.KartTrackCell 40\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\nCompletion.EndingTheme 1\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nResearch.Group 0\r\n");
    writeText(world / "rides" / "gokarts" / "gokarts.sam",
              "Info.Id 1150\r\nInfo.Shape\r\n---\r\n****\r\n****\r\n****\r\n*2N*\r\n---\r\n"
              "Bumper.WhichTrackType 1\r\nBumper.NorthXAdjust -2\r\nBumper.NorthYAdjust 1\r\n"
              "UsageInfo.ExcitementLevel 50\r\n"
              "SupplementalMeshes[0].FileName \"gk_blue.md2\"\r\n");
    writeText(world / "upgrades" / "dr_jump" / "dr_jump.sam",
              "Info.Id 1505\r\nAddOn.UpgradesId 1150\r\nAddOn.UpgradeType 1\r\nUpgrades[0].CostOfUpgrade 800\r\n");
    constexpr int kSide = 16;
    tpw::testing::Bytes tiles(size_t(kSide) * kSide, 0);
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kSide, kSide, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));

    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    const Attractions::Attraction *karts = park.attractions().find(1150);
    CHECK(karts && karts->trackType == 1 && karts->vehicles == std::vector<std::string>{"gk_blue.md2"});
    CHECK(park.place(1150, 0, karts->shape, true, {8, 8}, 0) >= 0);
    CHECK(park.hasTrack(0) && !park.trackComplete(0));
    const Park::Cell start = park.trackStart(0);
    CHECK(start.x == 6 && start.y == 9);
    const int cash = park.cash();
    CHECK(park.layTrack(0, {2, 9}) == Park::TrackLaying::kNotInLine);
    CHECK(park.layTrack(0, {6, 3}) == Park::TrackLaying::kLaid && park.objects()[0].track.size() == 4);
    CHECK(park.ground({6, 9}) == Park::Ground::kTrack && park.ground({7, 4}) == Park::Ground::kTrack);
    CHECK(park.layTrack(0, {6, 9}) == Park::TrackLaying::kNotInLine);
    CHECK(park.layTrack(0, {11, 4}) == Park::TrackLaying::kLaid);
    CHECK(park.layTrack(0, {10, 5}) == Park::TrackLaying::kLaid);
    // Across its own straight at (6, 5), but not to stop on it.
    CHECK(park.layTrack(0, {6, 5}) == Park::TrackLaying::kBlocked && park.objects()[0].track.size() == 8);
    CHECK(park.undoTrack(0) && park.objects()[0].track.size() == 7 && park.ground({8, 5}) == Park::Ground::kOpen);
    CHECK(park.layTrack(0, {4, 5}) == Park::TrackLaying::kLaid && park.objects()[0].track.size() == 10);
    CHECK(park.layTrack(0, {4, 11}) == Park::TrackLaying::kLaid);
    CHECK(park.layTrack(0, {6, 11}) == Park::TrackLaying::kLaid);
    CHECK(park.layTrack(0, {6, 9}) == Park::TrackLaying::kClosed && park.trackComplete(0));
    const std::vector<Park::Cell> &track = park.objects()[0].track;
    CHECK(track.size() == 14);
    // 13 blocks laid, the crossing not paid for again; the one taken up not paid back.
    CHECK(park.cash() == cash - 14 * 40);

    int starts = 0;
    int crossings = 0;
    int bends = 0;
    for (const Park::TrackPiece &p : park.trackPieces(0)) {
        starts += p.model == Park::TrackModel::kStart;
        crossings += p.model == Park::TrackModel::kCrossing;
        bends += p.model == Park::TrackModel::kBend;
        if (p.at.x == 6 && p.at.y == 3)
            CHECK(p.model == Park::TrackModel::kBend && p.angle == 0);
    }
    CHECK(starts == 1 && crossings == 1 && bends == 6);
    CHECK(park.trackCircuit(0).size() == 14);
    CHECK(park.awards()[size_t(Park::Award::kFastestGokart)].value == 4);

    // A jump over the west side's straight; not across the crossing.
    CHECK(park.addOnsFor(0).size() == 1 && park.addOnsFor(0)[0]->id == 1505);
    const int before = park.cash();
    const int exciting = park.excitementOf(0);
    CHECK(!park.buyAddOn(0, 1505, {6, 7}) && park.buyAddOn(0, 1505, {4, 7}) && park.cash() == before - 800);
    CHECK(!park.buyAddOn(0, 1505, {4, 9}));
    const std::vector<Park::AddOnPiece> addOns = park.addOnPieces(0);
    CHECK(addOns.size() == 1 && addOns[0].middle.x == 5 && addOns[0].middle.y == 9 && addOns[0].angle == 0);
    CHECK(park.trackPieces(0).size() == 11);
    CHECK(exciting == 50 && park.excitementOf(0) == 60);

    // Another without its track has a letter saying so the next day, once.
    CHECK(park.place(1150, 0, karts->shape, true, {12, 12}, 0) >= 0);
    park.takeMessages();
    int letters = 0;
    for (int d = 0; d < 3; ++d) {
        park.advance(Park::kDayMillis);
        for (const Park::Message &m : park.takeMessages())
            letters += m.title == 256 && m.text == 257;
    }
    CHECK(letters == 1);

    // Taken down, its track goes with it.
    park.remove(0, false);
    CHECK(park.ground({6, 9}) == Park::Ground::kOpen && park.ground({6, 5}) == Park::Ground::kOpen);

    // Bridges: track over a path, which stays, not stopping on it; path
    // laid under a straight; a ramp up and one down; taken up, the path
    // leaves the track.
    Park bridges;
    CHECK(!bridges.load(files, "levels/science", 0));
    CHECK(bridges.place(1150, 0, karts->shape, true, {8, 8}, 0) >= 0);
    CHECK(bridges.buildPath(Park::pathRun({0, 5}, {5, 5})) == 6);
    bridges.buildPath(Park::pathRun({6, 5}, {7, 5}));
    CHECK(bridges.layTrack(0, {6, 5}) == Park::TrackLaying::kBlocked && bridges.objects()[0].track.size() == 2);
    CHECK(bridges.layTrack(0, {6, 3}) == Park::TrackLaying::kLaid && bridges.objects()[0].track.size() == 4);
    CHECK(bridges.isPath({6, 5}) && bridges.ground({6, 6}) == Park::Ground::kTrack);
    CHECK(bridges.buildPath(Park::pathRun({4, 7}, {7, 7})) == 4 && bridges.isPath({7, 7}));
    std::vector<Park::TrackModel> models;
    for (const Park::TrackPiece &p : bridges.trackPieces(0))
        models.push_back(p.model);
    CHECK((models == std::vector<Park::TrackModel>{Park::TrackModel::kStart, Park::TrackModel::kRampUp,
                                                   Park::TrackModel::kRampDown, Park::TrackModel::kEnd}));
    CHECK(bridges.removePath({6, 7}) && bridges.ground({6, 7}) == Park::Ground::kTrack);
    // Not under the track's first block, nor its end.
    CHECK(bridges.buildPath({{6, 10}, {6, 3}}) == 0);
}

// A ride's sliders: its speed, capacity and duration from its level, set
// within its least and most; its excitement by its speed; past a red line.
// A photo kiosk, of the upgrades, not on sale in the Buy window, placed
// only near a ride, which it serves. A park without visitors has no mood.
void testRideExtras(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam", "BankAccountInfo.InitialCash 10000\r\nArrival.MinPeople 0\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\nCompletion.EndingTheme 1\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam",
              "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nResearch.Group 0\r\nUsageInfo.MinDuration 1\r\n"
              "UsageInfo.MaxDuration 10\r\nUsageInfo.MinSpeed 1\r\nUsageInfo.MaxSpeed 100\r\n"
              "Upgrades[0].InitDuration 3\r\nUpgrades[0].RedLineDuration 7\r\nUpgrades[0].InitSpeed 60\r\n"
              "Upgrades[0].RedLineSpeed 60\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\nUsageInfo.ExcitementLevel 70\r\n"
              "UsageInfo.MinCapacity 1\r\nUsageInfo.MaxCapacity 16\r\nUpgrades[0].InitCapacity 8\r\n"
              "Upgrades[0].RedLineCapacity 8\r\nUpgrades[0].WearRate 5\r\nUpgrades[0].CostOfUpgrade 100\r\n");
    writeText(world / "upgrades" / "Upgrades.sam", "Info.IsChoosable 0\r\nResearch.Group 0\r\n");
    writeText(world / "upgrades" / "fotobth" / "fotobth.sam",
              "Info.Id 1500\r\nInfo.Shape\r\n---\r\n**\r\n*2\r\n---\r\nInfo.HasQueue 0\r\n"
              "Info.IsPhotoBooth 1\r\nUsageInfo.InitPricePerUse 40\r\nUpgrades[0].CostOfUpgrade 400\r\n");
    writeText(world / "upgrades" / "dr_jump" / "dr_jump.sam", "Info.Id 1501\r\nUpgrades[0].CostOfUpgrade 400\r\n");
    constexpr int kSide = 32;
    tpw::testing::Bytes tiles(size_t(kSide) * kSide, 0);
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kSide, kSide, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));

    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    CHECK(park.mood() == -1);
    CHECK(park.place(1101, 0, {"2N"}, true, {2, 2}, 0) >= 0);
    using Setting = Park::RideSetting;
    const auto speed = park.rideSetting(0, Setting::kSpeed);
    CHECK(speed && speed->value == 60 && speed->least == 1 && speed->most == 100 && speed->redLine == 60);
    const auto capacity = park.rideSetting(0, Setting::kCapacity);
    CHECK(capacity && capacity->value == 8 && capacity->most == 16 && capacity->redLine == 8);
    CHECK(park.rideSetting(0, Setting::kDuration)->value == 3 && park.excitementOf(0) == 70 && !park.pastRedLine(0));
    park.setRideSetting(0, Setting::kSpeed, 30);
    CHECK(park.excitementOf(0) == 35);
    park.setRideSetting(0, Setting::kSpeed, 500);
    CHECK(park.rideSetting(0, Setting::kSpeed)->value == 100 && park.excitementOf(0) == 100 && park.pastRedLine(0));
    park.setRideSetting(0, Setting::kSpeed, 60);
    park.setRideSetting(0, Setting::kCapacity, 12);
    CHECK(park.pastRedLine(0));

    // The kiosk: far off, refused; by the ride, its.
    const Attractions::Attraction *kiosk = park.attractions().find(1500);
    CHECK(kiosk && kiosk->photo && !kiosk->available && kiosk->kind == Attractions::Kind::kShop);
    CHECK(!park.attractions().find(1501));
    CHECK(park.place(1500, 400, kiosk->shape, false, {25, 25}, 0) == Park::kNotPlaced);
    CHECK(park.place(1500, 400, kiosk->shape, false, {6, 2}, 0) != Park::kNotPlaced);
    CHECK(park.photoRideOf(1) == 0 && park.photoRideOf(0) == -1);

    // Price letters at the month's end: the ride, used enough and mostly
    // found too dear, "Price Complaints"; the kiosk, found cheap, "Pricing
    // Opportunity"; one used too little, nothing.
    for (int i = 0; i < 12; ++i)
        park.notePrice(0, i < 8 ? Park::PriceOpinion::kMuchTooHigh : Park::PriceOpinion::kAboutRight);
    for (int i = 0; i < 12; ++i)
        park.notePrice(1, Park::PriceOpinion::kCheap);
    park.takeMessages();
    for (int d = 0; d < 32; ++d)
        park.advance(Park::kDayMillis);
    std::vector<std::pair<uint32_t, uint32_t>> letters;
    for (const Park::Message &m : park.takeMessages())
        if (m.title == 248 || m.title == 249)
            letters.emplace_back(m.title, m.text);
    CHECK((letters == std::vector<std::pair<uint32_t, uint32_t>>{{248, 134}, {249, 141}}));
}

void testSaving(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    std::string types;
    for (int i = 0; i < 24; ++i)
        types += "PeepTypes[" + std::to_string(i) +
                 "].PreferredExcitement.StartingCash.BoredomThreshold.Generation 60 "
                 "100 200 " +
                 std::to_string(i / 8) + "\r\n";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 1000\r\nCosts.QueueCell 10\r\nArrival.MinPeople 3\r\n"
              "Arrival.TimeBetweenArrivals 1\r\nArrival.FixedRate 3\r\nArrival.PointsPerVisitor 1\r\n"
              "PeepInfo.PercentageOfKids 100\r\nStaffPoolInfo.BeginningNumberOfHandymen 2\r\n"
              "StaffPoolInfo.AvgExperienceOfHandymen 20000\r\nStaffPoolInfo.AvgWageDifference 0\r\n"
              "StaffAgencies[0].InitialWageModifier 10\r\nStaffAgencies[0].InitialExperienceModifier 0\r\n" +
                  types);
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    const std::filesystem::path world = data / "levels" / "science";
    writeText(world / "rides" / "Rides.sam", "Info.HasQueue 1\r\nInfo.IsChoosable 1\r\nInfo.AttractionValue 3\r\n");
    writeText(world / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nUsageInfo.InitPricePerUse 10\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\n");
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(world / "terrain" / "base.map", tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));

    // A park played a while: a ride and its queue, a janitor, visitors.
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    const int q = park.place(1101, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.layQueue(q, Park::pathRun({2, 2}, {2, 4})) == 1);
    CHECK(park.hire(Park::StaffKind::kJanitor, 0) == Park::Hiring::kHired);
    park.rename(0, "Big Bot");
    for (int t = 0; t < 300; ++t)
        park.advance(100);
    CHECK(park.visitors() > 0 && !park.objects().empty());
    const std::vector<uint8_t> saved = park.saveState();

    // Put back over a new park of the world, it's the same, and plays on
    // the same.
    Park again;
    CHECK(!again.load(files, "levels/science", 0));
    CHECK(!again.restoreState(saved));
    CHECK(again.saveState() == saved);
    CHECK(again.objects().size() == 1 && again.objects()[0].name == "Big Bot" && again.staff().size() == 1);
    CHECK(again.cash() == park.cash() && again.day() == park.day() && again.visitors() == park.visitors());
    for (int t = 0; t < 300; ++t) {
        park.advance(100);
        again.advance(100);
    }
    CHECK(again.saveState() == park.saveState());

    // Through a file: listed by its player, read back whole.
    const std::filesystem::path folder = tpw::savesFolder(tmp / "saves");
    const std::filesystem::path file = tpw::newSaveFile(folder);
    CHECK(file.filename() == "save1.rsv");
    CHECK(!tpw::writeSave(file, {"Robot park", "Tester", "levels/science", 0, park.day(), saved, {1, 2}}));
    CHECK(tpw::newSaveFile(folder).filename() == "save2.rsv");
    const std::vector<tpw::SaveSlot> slots = tpw::listSaves(folder, "Tester");
    CHECK(slots.size() == 1 && slots[0].name == "Robot park" && tpw::listSaves(folder, "Someone").empty());
    tpw::SavedGame read;
    CHECK(!tpw::readSave(file, read) && read.park == saved && read.world == "levels/science" &&
          (read.screen == std::vector<uint8_t>{1, 2}));
    std::filesystem::resize_file(file, std::filesystem::file_size(file) - 1);
    CHECK(tpw::readSave(file, read));
    CHECK(!tpw::deleteSave(file) && tpw::listSaves(folder, "Tester").empty() && tpw::deleteSave(file));

    // The options: the game's speed by Config.sam's default config, the
    // volumes by sound.sam's; kept and read back.
    writeText(data / "Config.sam", "Defaults.DefaultConfig 1\r\nConfig[1].DefaultGameSpeed 70\r\n");
    writeText(data / "sound.sam", "DefaultVolume.SFX 80\r\nDefaultVolume.MUSIC 60\r\n");
    SearchSet withConfig;
    CHECK(!mountGame(withConfig, tmp / "game", warnings));
    tpw::GameOptions options = tpw::defaultOptions(withConfig);
    CHECK(options.gameSpeed == 70 && (options.volume == std::array<int, 3>{80, 100, 60}) && options.advisor);
    options.gameSpeed = 40;
    options.advisor = false;
    CHECK(!tpw::writeOptions(tmp / "player", options));
    const tpw::GameOptions read2 = tpw::readOptions(tmp / "player", tpw::GameOptions{});
    CHECK(read2.gameSpeed == 40 && !read2.advisor && read2.volume[2] == 60);
    CHECK(tpw::readOptions(tmp / "nobody", options).gameSpeed == 40);

    // Cut short, or another version, it's refused.
    Park broken;
    CHECK(!broken.load(files, "levels/science", 0));
    CHECK(broken.restoreState(std::vector<uint8_t>(saved.begin(), saved.end() - 3)));
    std::vector<uint8_t> other = saved;
    other[0] = 99;
    CHECK(broken.restoreState(other));
}

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.park", {
                                {"new-park", [](const std::filesystem::path &tmp) { testNewPark(tmp); }},
                                {"calendar", [](const std::filesystem::path &) { testCalendar(); }},
                                {"paths", [](const std::filesystem::path &tmp) { testPaths(tmp); }},
                                {"path-tiles", [](const std::filesystem::path &) { testPathTiles(); }},
                                {"queue-pieces", [](const std::filesystem::path &) { testQueuePieces(); }},
                                {"attractions", [](const std::filesystem::path &tmp) { testAttractions(tmp); }},
                                {"placing", [](const std::filesystem::path &) { testPlacing(); }},
                                {"riders", [](const std::filesystem::path &tmp) { testRiders(tmp); }},
                                {"conditions", [](const std::filesystem::path &tmp) { testConditions(tmp); }},
                                {"staff", [](const std::filesystem::path &tmp) { testStaff(tmp); }},
                                {"upkeep", [](const std::filesystem::path &tmp) { testUpkeep(tmp); }},
                                {"research", [](const std::filesystem::path &tmp) { testResearch(tmp); }},
                                {"finance", [](const std::filesystem::path &tmp) { testFinance(tmp); }},
                                {"missions", [](const std::filesystem::path &tmp) { testMissions(tmp); }},
                                {"challenges", [](const std::filesystem::path &tmp) { testChallenges(tmp); }},
                                {"special-rides", [](const std::filesystem::path &tmp) { testSpecialRides(tmp); }},
                                {"awards", [](const std::filesystem::path &tmp) { testAwards(tmp); }},
                                {"land", [](const std::filesystem::path &tmp) { testLand(tmp); }},
                                {"fence-pieces", [](const std::filesystem::path &) { testFencePieces(); }},
                                {"tracks", [](const std::filesystem::path &tmp) { testTracks(tmp); }},
                                {"ride-extras", [](const std::filesystem::path &tmp) { testRideExtras(tmp); }},
                                {"saving", [](const std::filesystem::path &tmp) { testSaving(tmp); }},
                            });
