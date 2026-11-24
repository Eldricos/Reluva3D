// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park being played: a new one set up from settings and a mission
// built in the test; its calendar and its cells.

#include "common/archive.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/park/attractions.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/park/path_tiles.h"

#include "tests/engines/tpw/fixtures.h"
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

// Visitors on a line of path: into a ride's queue as they pass its end,
// up it to the entrance, on when the ride takes them, paying its fare,
// off by its exit and back to the path.
void testRiders(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data";
    writeText(data / "levels" / "Standard.sam",
              "BankAccountInfo.InitialCash 1000\r\nCosts.QueueCell 10\r\nArrival.MinPeople 1\r\n"
              "Arrival.TimeBetweenArrivals 1\r\nArrival.FixedRate 6\r\nArrival.PointsPerVisitor 1\r\n");
    writeText(data / "missions" / "miss00.sam", "Info.Number 0\r\n");
    writeText(data / "levels" / "science" / "rides" / "Rides.sam", "Info.HasQueue 1\r\n");
    writeText(data / "levels" / "science" / "rides" / "robot" / "robot.sam",
              "Info.Id 1101\r\nUsageInfo.InitPricePerUse 10\r\nInfo.Shape\r\n---\r\n2N\r\n---\r\n");
    // Without the world's settings its way in ends at (0, 4): path from
    // there along y = 4.
    constexpr int kRows = 8;    // x
    constexpr int kColumns = 6; // y
    tpw::testing::Bytes tiles(size_t(kRows) * kColumns, 0);
    for (int x = 0; x < 7; ++x)
        tiles[size_t(x) * kColumns + 4] = 8;
    test::writeFile(data / "levels" / "science" / "terrain" / "base.map",
                    tpw::testing::makeAttributeMap(kColumns, kRows, tiles));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!mountGame(files, tmp / "game", warnings));
    Park park;
    CHECK(!park.load(files, "levels/science", 0));
    const int q = park.place(1101, 0, {"2N"}, true, {2, 2}, 0);
    CHECK(q >= 0 && park.queueOf(0) == q);
    if (q < 0)
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
    const int cash = park.cash();
    CHECK(park.board(q, waiting) && park.cash() == cash + 10);
    v = park.visitor(waiting);
    CHECK(v && v->doing == Park::Doing::kRiding && v->rides == 1);
    CHECK(!park.board(q, waiting) && !park.letOff(q, waiting + 100));
    park.advance(1000);
    CHECK(park.visitor(waiting)->doing == Park::Doing::kRiding);
    CHECK(park.letOff(q, waiting));
    v = park.visitor(waiting);
    CHECK(v && v->doing == Park::Doing::kLeaving && v->x == 35 && v->z == 25 && (v->cell == Park::Cell{3, 4}));
    park.advance(5000);
    v = park.visitor(waiting);
    CHECK(v && v->doing == Park::Doing::kWalking && park.isPath(v->cell));
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
                            });
