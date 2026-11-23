// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What a new park starts with is in its settings and its mission's; how
// they're laid over the world is worked out from the world's attribute
// map: docs/engines/tpw/games/simcoaster/systems/park.md.

#include "engines/tpw/park/park.h"

#include "engines/tpw/formats/sam.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace reluva::tpw {

namespace {

constexpr const char *kGeneralSettings = "levels/Standard.sam";
constexpr int kMostBoundaryClicks = 256;
constexpr int kMostTicketBooths = 8;

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
