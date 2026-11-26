// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What a new park starts with is in its settings and its mission's; how
// they're laid over the world is worked out from the world's attribute
// map: docs/engines/tpw/games/simcoaster/systems/park.md.

#include "engines/tpw/park/park.h"

#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/formats/saved_park.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace reluva::tpw {

namespace {

constexpr const char *kGeneralSettings = "levels/Standard.sam";
constexpr const char *kPrebuiltPark = "PreBuilt/prebuilt.TPWS";
constexpr int kMostTicketBooths = 8;
// How far the entrance's path runs in, in cells: its tiles, in the
// attribute map.
constexpr int kPathIn = 4;
constexpr int kVisitorTypes = 24;
// The weathers but fine, in Park::Weather's order, as the settings name them.
constexpr std::array<const char *, 4> kWeathers{"Heatwave", "Rain", "Storm", "Snow"};
// The attribute map's tiles for the park's bare land and its path
// (docs/engines/tpw/games/simcoaster/formats/attribute-map.md#the-tiles).
constexpr uint8_t kOpenTile = 0;
constexpr uint8_t kPathTile = 8;

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
    _queueCost = int(general.number("Costs.QueueCell"));
    for (int i = 0; i < kVisitorTypes; ++i) {
        const std::string base = "PeepTypes[" + std::to_string(i) + "].";
        VisitorType type;
        // One line gives four values, the generation last.
        const SamFile::Entry *line =
            general.find(base + "PreferredExcitement.StartingCash.BoredomThreshold.Generation");
        if (line && line->values.size() >= 4) {
            type.excitement = int(line->values[0].number);
            type.cash = int(line->values[1].number);
            type.boredom = int(line->values[2].number);
            type.generation = std::clamp(int(line->values[3].number), 0, 2);
        } else {
            type.generation = i / (kVisitorTypes / 3);
        }
        type.speed = float(general.number(base + "SpeedMultiplier", 1));
        _visitorTypes.push_back(type);
    }
    const std::string p = "PeepInfo.";
    auto rule = [&](const std::string &name, int fallback) { return int(general.number(p + name, fallback)); };
    _rules.cashSpreadPercent = rule("StartingCashVarPc", 0);
    _rules.generationPercent = {rule("PercentageOfKids", 70), rule("PercentageOfParents", 20),
                                rule("PercentageOfGrandparents", 10)};
    _rules.rideMatch = {rule("PerfectExcitementMatch", 5), rule("GoodExcitementMatch", 15),
                        rule("OKExcitementMatch", 40)};
    _rules.rideHappiness = {rule("PerfectRide", 25), rule("GoodRide", 15), rule("OKRide", 5)};
    _rules.distanceWeight = rule("DecisionVarDistWeight", 1);
    _rules.queueWeight = rule("DecisionVarQueueWeight", 1);
    _rules.excitementWeight = rule("DecisionVarExcitementWeight", 1);
    _rules.thirstWeight = rule("DecisionVarThirstWeight", 2);
    _rules.hungerWeight = rule("DecisionVarHungerWeight", 2);
    _rules.toiletWeight = rule("DecisionVarToiletWeight", 2);
    _rules.exitDays = rule("ExitLevel", 400);
    _rules.exitSpread = rule("ExitLevelVar", 60);
    _rules.needy = rule("ThoughtBubbleHungry", 80);
    _rules.happy = rule("ThoughtBubbleHappy", 80);
    _rules.unhappy = rule("ThoughtBubbleUnhappy", 20);
    _rules.sick = rule("ThoughtBubbleSick", 80);
    _rules.smallChange = rule("SmallHappinessChange", 5);
    _rules.mediumChange = rule("MediumHappinessChange", 15);
    _rules.bigChange = rule("BigHappinessChange", 25);
    _rules.vomitDivisor = std::max(1, rule("RideVomitDivisor", 10));
    _rules.vomitCapacity = rule("VomitCapacity", 100);
    _rules.prankersPercent = rule("PrankeryLikelihood", 10);
    _rules.stinkBombPercent = rule("StinkbombLikelihood", 25);
    _rules.prankUnhappy = rule("PrankHappinessThreshold", 15);
    _rules.prankPercent = rule("PrankLikelihood", 10);
    _rules.rumourCells = rule("ViciousRumoursArea", 2);
    _rules.weatherDays = std::max(1, rule("WeatherModifierFrequency", 4));
    _rules.umbrellaShelter = rule("UmbrellaModifier", 1);
    _rules.cheapPrice = float(general.number(p + "CheapPriceMultiplier", 0.75));
    _rules.averagePrice = float(general.number(p + "AveragePriceMultiplier", 1.25));
    _rules.expensivePrice = float(general.number(p + "ExpensivePriceMultiplier", 2));
    _rules.priceOpinion = {rule("OpinionForTooLow", 100), rule("OpinionForAboutRight", 50),
                           rule("OpinionForTooHigh", 20), rule("OpinionForMuchTooHigh", 0)};
    _rules.minimumEntryFee = rule("MinimumEntryFee", 20);
    _rules.excitementToCost = std::max(1, rule("ExcitementToCostDivisor", 6));
    loadStaffRules(general);
    for (size_t g = 0; g < 3; ++g)
        for (size_t w = 0; w < kWeathers.size(); ++w) {
            const std::string at = "[" + std::to_string(g) + "]." + kWeathers[w];
            _rules.weatherHappiness[g][w + 1] = float(general.number("PeepHappinessWeatherEffects" + at));
            _rules.weatherThirst[g][w + 1] = float(general.number("PeepThirstWeatherEffects" + at));
            _rules.weatherHunger[g][w + 1] = float(general.number("PeepHungerWeatherEffects" + at));
        }
    for (size_t w = 0; w < kWeathers.size(); ++w)
        _rules.weatherArrivals[w + 1] =
            int(general.number(std::string("Arrival.") + kWeathers[w] + "PercentageModifier", 100));

    // The world's fixed items; a world without its settings has none.
    SamFile world;
    const bool hasWorld = !readSettings(files, folder + "/Standard.sam", world);
    loadFinance(general, hasWorld ? &world : nullptr);
    if (hasWorld) {
        // Each season's weather: a quarter of the year each, from January.
        for (size_t season = 0; season < _seasons.size(); ++season)
            for (size_t w = 0; w < kWeathers.size(); ++w)
                _seasons[season][w] = int(world.number(
                    "Seasons[" + std::to_string(season) + "].PercentageChanceFor" + std::string(kWeathers[w]), 0));
        const int booths = std::min(kMostTicketBooths, int(world.number("FixedItemInfo.NumTicketBooths")));
        for (int i = 0; i < booths; ++i) {
            const std::string base = "TicketBooth[" + std::to_string(i) + "].";
            _fixed.ticketBooths.push_back(cellOf(world, base + "PosX", base + "PosY"));
            _fixed.ticketBoothAngles.push_back(int(world.number(base + "Angle")));
        }
        const std::string f = "FixedItemInfo.";
        _constants.strikeFrom = cellOf(world, f + "StrikeAreaStartX", f + "StrikeAreaStartY");
        _constants.strikeSize = cellOf(world, f + "StrikeAreaSizeX", f + "StrikeAreaSizeY");
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

    // Its attractions: the prebuilt park's want their footprints. A world
    // without them has nothing to build or ride.
    _attractions.load(files, folder);

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
        layPrebuilt(files, map);
    }

    // The way in: off the bus at its first stop, over the crossing from
    // the bus's side to the park's, past the first booth, through the
    // entrance and up the path inside (the attribute map's path tiles run
    // four cells in).
    const Cell in = _fixed.entrance[0];
    for (const Cell &c : {_fixed.busStops[0], _fixed.crossingBusSide[0], _fixed.crossingParkSide[0],
                          _fixed.ticketBooths.empty() ? in : _fixed.ticketBooths[0], in, Cell{in.x, in.y + kPathIn}})
        _wayIn.push_back(cellCentre(c));

    rollWeather();
    fillPool(true);

    if (Error err = loadMissions(files, mission))
        return err;
    // The screen sets out from the park as it's set up.
    _removals.clear();
    return Error::success();
}

int Park::points() const {
    int points = _arrivals.fixedRate + _arrivals.newParkBonus;
    // A plant gone dry adds nothing (guessed).
    for (const PlacedObject &o : _objects)
        if (const Attractions::Attraction *a = _attractions.find(o.id); a && !(a->requiresWatering && o.water <= 0))
            points += a->attractionValue;
    return points;
}

int Park::visitorsWanted() const {
    const int wanted = points() / _arrivals.pointsPerVisitor * _rules.weatherArrivals[size_t(_weather)] / 100;
    return std::max(_arrivals.minPeople, wanted);
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

// The prebuilt park's path, queues and objects, where it's this world's: its cells'
// tiles are the world's attribute map's, every one. The recording's new
// park has its path as its briefing closes, before anything is spent.
void Park::layPrebuilt(const SearchSet &files, const AttributeMap &map) {
    std::vector<uint8_t> data;
    SavedPark saved;
    if (files.read(kPrebuiltPark, data) || saved.parse(data) || _mapWidth > SavedPark::kSide ||
        _mapHeight > SavedPark::kSide)
        return;
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y)
            if (saved.cell(x, y).tile != map.at(y, x))
                return;
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y) {
            const SavedPark::Cell &c = saved.cell(x, y);
            Ground &g = _ground[size_t(y) * size_t(_mapWidth) + size_t(x)];
            if (c.built == SavedPark::Built::kPath) {
                g = Ground::kPath;
            } else if (c.built == SavedPark::Built::kQueue) {
                g = Ground::kQueue;
            } else if (g == Ground::kOpen && (c.fill == SavedPark::kFillObject || c.fill == SavedPark::kFillEntrance ||
                                              c.fill == SavedPark::kFillExit)) {
                g = Ground::kTaken;
            }
        }
    // Each attraction's queue, followed from its entrance.
    for (int x = 0; x < _mapWidth; ++x)
        for (int y = 0; y < _mapHeight; ++y)
            if (saved.cell(x, y).fill == SavedPark::kFillEntrance)
                _queues.push_back(followQueue({x, y}));
    for (const SavedPark::Object &o : saved.objects()) {
        if (!o.placed())
            continue;
        _objects.push_back({o.id, {int(std::floor(o.x)), int(std::floor(o.y))}, int(o.angle)});
        const PlacedObject &placed = _objects.back();
        if (const Attractions::Attraction *a = _attractions.find(o.id))
            linkQueue(_objects.size() - 1, footprint(a->shape, placed.at, placed.angle));
    }
    _prebuilt = true;
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
        book(Ledger::kBuilding, _pathCost);
        ++laid;
    }
    findKioskPaths();
    return laid;
}

void Park::step(uint32_t ms) {
    _millis += ms;
    while (_lastDay < day()) {
        ++_lastDay;
        newDay();
    }
    _stinkBombs.erase(std::remove_if(_stinkBombs.begin(), _stinkBombs.end(),
                                     [this](const StinkBomb &b) { return b.until <= _millis; }),
                      _stinkBombs.end());
    while (_millis >= _nextArrivalMs) {
        _nextArrivalMs += _arrivals.everyMs;
        if (_parkOpen && int(_people.size()) < visitorsWanted() && !_wayIn.empty())
            addVisitor();
    }
    for (Visitor &v : _people) {
        useUp(v, float(ms) / float(kDayMillis));
        moveVisitor(v, kWalkPerSecond * v.speed * float(ms) / 1000);
    }
    for (StaffMember &m : _staff)
        moveStaff(m, kWalkPerSecond * float(ms) / 1000);
    _people.erase(std::remove_if(_people.begin(), _people.end(), [](const Visitor &v) { return v.gone; }),
                  _people.end());
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
