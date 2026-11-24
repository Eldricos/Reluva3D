// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What a new park starts with is in its settings and its mission's; how
// they're laid over the world is worked out from the world's attribute
// map: docs/engines/tpw/games/simcoaster/systems/park.md.

#include "engines/tpw/park/park.h"

#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/sam.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace reluva::tpw {

namespace {

constexpr const char *kGeneralSettings = "levels/Standard.sam";
constexpr int kMostBoundaryClicks = 256;
constexpr int kMostTicketBooths = 8;
// How far the entrance's path runs in, in cells: its tiles, in the
// attribute map.
constexpr int kPathIn = 4;
// Which waypoint of the way in is the booth's, where a visitor pays, and
// the entrance's, past which they're in the park.
constexpr int kVisitorTypes = 24;
// The attribute map's tiles for the park's bare land and its path
// (docs/engines/tpw/games/simcoaster/formats/attribute-map.md#the-tiles).
constexpr uint8_t kOpenTile = 0;
constexpr uint8_t kPathTile = 8;
constexpr size_t kBoothLeg = 3;
constexpr size_t kEntranceLeg = 4;

Error readSettings(const SearchSet &files, const std::string &name, SamFile &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    return out.parse(data);
}

Park::Cell cellOf(const SamFile &sam, const std::string &x, const std::string &y) {
    return {int(sam.number(x)), int(sam.number(y))};
}

bool leapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

} // namespace

Error Park::load(const SearchSet &files, const std::string &folder, int mission) {
    *this = {};
    _folder = folder;
    SamFile general;
    if (Error err = readSettings(files, kGeneralSettings, general))
        return err;
    _cash = int(general.number("BankAccountInfo.InitialCash"));
    _admissionFee = int(general.number("BankAccountInfo.InitialAdmissionFee"));
    _arrivals.minPeople = int(general.number("Arrival.MinPeople", 1));
    _arrivals.everyMs = uint32_t(std::max(1.0, general.number("Arrival.TimeBetweenArrivals", 25)) * 1000);
    _arrivals.fixedRate = int(general.number("Arrival.FixedRate"));
    _arrivals.newParkBonus = int(general.number("Arrival.NewParkBonus"));
    _arrivals.pointsPerVisitor = std::max(1, int(general.number("Arrival.PointsPerVisitor", 1)));
    _nextArrivalMs = _arrivals.everyMs;
    _pathCost = int(general.number("Costs.PathCell"));
    for (int i = 0; i < kVisitorTypes; ++i) {
        const std::string base = "PeepTypes[" + std::to_string(i) + "].";
        VisitorType type;
        // One line gives four values, the generation last.
        const SamFile::Entry *line =
            general.find(base + "PreferredExcitement.StartingCash.BoredomThreshold.Generation");
        if (line && line->values.size() >= 4)
            type.generation = std::clamp(int(line->values[3].number), 0, 2);
        else
            type.generation = i / (kVisitorTypes / 3);
        type.speed = float(general.number(base + "SpeedMultiplier", 1));
        _visitorTypes.push_back(type);
    }

    // The world's fixed items; a world without its settings has none.
    SamFile world;
    if (!readSettings(files, folder + "/Standard.sam", world)) {
        const int booths = std::min(kMostTicketBooths, int(world.number("FixedItemInfo.NumTicketBooths")));
        for (int i = 0; i < booths; ++i) {
            const std::string base = "TicketBooth[" + std::to_string(i) + "].";
            _fixed.ticketBooths.push_back(cellOf(world, base + "PosX", base + "PosY"));
            _fixed.ticketBoothAngles.push_back(int(world.number(base + "Angle")));
        }
        const std::string f = "FixedItemInfo.";
        _fixed.entrance = {cellOf(world, f + "EntranceAPosX", f + "EntranceAPosY"),
                           cellOf(world, f + "EntranceBPosX", f + "EntranceBPosY")};
        _fixed.busStops = {cellOf(world, f + "BusStopAPosX", f + "BusStopAPosY"),
                           cellOf(world, f + "BusStopBPosX", f + "BusStopBPosY"),
                           cellOf(world, f + "BusStopCPosX", f + "BusStopCPosY")};
        _fixed.crossingBusSide = {cellOf(world, f + "CrossingBSSideAPosX", f + "CrossingBSSideAPosY"),
                                  cellOf(world, f + "CrossingBSSideBPosX", f + "CrossingBSSideBPosY")};
        _fixed.crossingParkSide = {cellOf(world, f + "CrossingParkSideAPosX", f + "CrossingParkSideAPosY"),
                                   cellOf(world, f + "CrossingParkSideBPosX", f + "CrossingParkSideBPosY")};
    }

    // The ground: cell (x, y) is the attribute map's tile in row x, column
    // y. A world without its map has no land to build on.
    std::vector<uint8_t> mapData;
    AttributeMap map;
    if (!files.read(folder + "/terrain/base.map", mapData) && !map.parse(mapData)) {
        _mapWidth = map.height();
        _mapHeight = map.width();
        _ground.assign(size_t(_mapWidth) * size_t(_mapHeight), Ground::kOutside);
        for (int x = 0; x < _mapWidth; ++x)
            for (int y = 0; y < _mapHeight; ++y) {
                const uint8_t tile = map.at(y, x);
                _ground[size_t(y) * size_t(_mapWidth) + size_t(x)] = tile == kOpenTile   ? Ground::kOpen
                                                                     : tile == kPathTile ? Ground::kPath
                                                                                         : Ground::kOutside;
            }
    }

    // The way in: off the bus at its first stop, over the crossing from
    // the bus's side to the park's, past the first booth, through the
    // entrance and up the path inside (the attribute map's path tiles run
    // four cells in).
    const Cell in = _fixed.entrance[0];
    for (const Cell &c : {_fixed.busStops[0], _fixed.crossingBusSide[0], _fixed.crossingParkSide[0],
                          _fixed.ticketBooths.empty() ? in : _fixed.ticketBooths[0], in, Cell{in.x, in.y + kPathIn}})
        _wayIn.push_back(cellCentre(c));

    char name[32];
    std::snprintf(name, sizeof name, "missions/miss%02d.sam", mission);
    SamFile m;
    if (Error err = readSettings(files, name, m))
        return err;
    _mission.number = int(m.number("Info.Number", mission));
    _mission.statistic = int(m.number("ExitStatistic[0].Type"));
    _mission.target = int(m.number("ExitStatistic[0].TargetValue"));
    _mission.obstacle = int(m.number("Obstacle.Type"));
    for (int i = 0; i < kMostBoundaryClicks; ++i) {
        const std::string base = "BoundaryClicks[" + std::to_string(i) + "].";
        if (!m.find(base + "X"))
            break;
        _mission.boundary.push_back(cellOf(m, base + "X", base + "Y"));
    }
    return Error::success();
}

int Park::visitorsWanted() const {
    const int points = _arrivals.fixedRate + _arrivals.newParkBonus;
    return std::max(_arrivals.minPeople, points / _arrivals.pointsPerVisitor);
}

void Park::advance(uint32_t ms) {
    if (_paused)
        return;
    // In short steps, so that a visitor arriving partway through one
    // walks only from then.
    while (ms > kStepMillis) {
        step(kStepMillis);
        ms -= kStepMillis;
    }
    step(ms);
}

Park::Ground Park::ground(const Cell &cell) const {
    if (cell.x < 0 || cell.y < 0 || cell.x >= _mapWidth || cell.y >= _mapHeight)
        return Ground::kOutside;
    return _ground[size_t(cell.y) * size_t(_mapWidth) + size_t(cell.x)];
}

std::vector<Park::Cell> Park::pathRun(const Cell &from, const Cell &to) {
    std::vector<Cell> out;
    const bool alongX = std::abs(to.x - from.x) >= std::abs(to.y - from.y);
    const int length = alongX ? std::abs(to.x - from.x) : std::abs(to.y - from.y);
    const int step = (alongX ? to.x - from.x : to.y - from.y) < 0 ? -1 : 1;
    for (int i = 0; i <= length; ++i)
        out.push_back(alongX ? Cell{from.x + i * step, from.y} : Cell{from.x, from.y + i * step});
    return out;
}

int Park::buildPath(const std::vector<Cell> &cells) {
    int laid = 0;
    for (const Cell &c : cells) {
        if (ground(c) != Ground::kOpen || _cash < _pathCost)
            continue;
        _ground[size_t(c.y) * size_t(_mapWidth) + size_t(c.x)] = Ground::kPath;
        _cash -= _pathCost;
        ++laid;
    }
    return laid;
}

void Park::step(uint32_t ms) {
    _millis += ms;
    while (_millis >= _nextArrivalMs) {
        _nextArrivalMs += _arrivals.everyMs;
        if (int(_people.size()) < visitorsWanted() && !_wayIn.empty()) {
            Visitor v;
            v.id = _nextId++;
            // A type and look spread over the ids, the same each run.
            const uint32_t roll = uint32_t(v.id) * 2654435761u >> 16;
            if (!_visitorTypes.empty()) {
                const VisitorType &type = _visitorTypes[roll % _visitorTypes.size()];
                v.generation = type.generation;
                v.speed = type.speed;
            }
            v.look = roll / uint32_t(kVisitorTypes);
            v.x = _wayIn[0][0];
            v.z = _wayIn[0][2];
            v.leg = 1;
            _people.push_back(v);
        }
    }
    for (Visitor &v : _people) {
        float left = kWalkPerSecond * v.speed * float(ms) / 1000;
        if (v.leg >= _wayIn.size())
            v.headingX = v.headingZ = 0;
        while (left > 0 && v.leg < _wayIn.size()) {
            const float dx = _wayIn[v.leg][0] - v.x;
            const float dz = _wayIn[v.leg][2] - v.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d > 0) {
                v.headingX = dx / d;
                v.headingZ = dz / d;
            }
            if (d > left) {
                v.x += dx / d * left;
                v.z += dz / d * left;
                v.walked += left;
                break;
            }
            v.x = _wayIn[v.leg][0];
            v.z = _wayIn[v.leg][2];
            v.walked += d;
            left -= d;
            if (v.leg == kBoothLeg && !v.paid) {
                v.paid = true;
                _cash += _admissionFee;
            }
            if (v.leg == kEntranceLeg && !v.inPark) {
                v.inPark = true;
                ++_visitors;
            }
            ++v.leg;
        }
    }
}

Park::Date Park::dateOf(uint32_t days) {
    static constexpr std::array<int, 12> kMonthDays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    Date d{1, 0, 2000};
    for (;;) {
        const uint32_t yearDays = leapYear(d.year) ? 366 : 365;
        if (days < yearDays)
            break;
        days -= yearDays;
        ++d.year;
    }
    for (;;) {
        const uint32_t monthDays = uint32_t(kMonthDays[size_t(d.month)] + (d.month == 1 && leapYear(d.year)));
        if (days < monthDays)
            break;
        days -= monthDays;
        ++d.month;
    }
    d.day = int(days) + 1;
    return d;
}

gfx::Vec3 Park::cellCentre(const Cell &cell) {
    return {(float(cell.x) + 0.5f) * kCellUnits, 0, (float(cell.y) + 0.5f) * kCellUnits};
}

} // namespace reluva::tpw
