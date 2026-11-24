// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/attribute_map.h"
#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * A park being played: what it is given at the start (its world's fixed
 * items, its mission, its money) and what it keeps as it runs (the date,
 * the money, the visitors). See
 * docs/engines/tpw/games/simcoaster/systems/park.md.
 */
class Park {
public:
    /** A park cell is 10 units square; cell (x, y) covers x 10x to 10x + 10 and z 10y to 10y + 10. */
    static constexpr float kCellUnits = 10;
    /** How long a day lasts at the game's own pace, in milliseconds (inferred from the recording). */
    static constexpr uint32_t kDayMillis = 4000;

    struct Cell {
        int x = 0;
        int y = 0;
        bool operator==(const Cell &o) const { return x == o.x && y == o.y; }
    };

    /** What every park of a world has from the start, by its settings' FixedItemInfo. */
    struct FixedItems {
        std::vector<Cell> ticketBooths;
        std::vector<int> ticketBoothAngles; // 0 to 1023 a turn, it seems (512 for all the game's)
        std::array<Cell, 2> entrance{};     // the two cells guests come in by
        std::array<Cell, 3> busStops{};
        std::array<Cell, 2> crossingBusSide{};
        std::array<Cell, 2> crossingParkSide{};
    };

    /** The mission the park is played for (`missions/missNN.sam`). */
    struct Mission {
        int number = -1;
        int statistic = 0; // what the objective counts: 8 the visitors in the park at once
        int target = 0;
        int obstacle = 0;           // the obstacle it clears, by its object number
        std::vector<Cell> boundary; // the park's land, as clicks from the entrance (inferred)
    };

    /**
     * Sets up a new park in the world of `folder` (`levels/science`) for
     * mission `mission`: the general and the world's settings, the
     * mission's; its ground by the world's attribute map, with the path and
     * queues of the game's prebuilt park (`PreBuilt/prebuilt.TPWS`) where
     * that is of this world. Fails if the general settings or the mission
     * can't be read.
     */
    Error load(const SearchSet &files, const std::string &folder, int mission);

    const std::string &folder() const { return _folder; }
    const FixedItems &fixedItems() const { return _fixed; }
    const Mission &mission() const { return _mission; }

    /** How visitors come, by the general settings' `Arrival` (guessed: see park.md). */
    struct Arrivals {
        int minPeople = 1;
        uint32_t everyMs = 25000; // TimeBetweenArrivals, seconds
        int fixedRate = 0;        // points a park has however bare
        int newParkBonus = 0;     // and while new
        int pointsPerVisitor = 1;
    };
    const Arrivals &arrivals() const { return _arrivals; }

    /**
     * A kind of visitor, by the general settings' `PeepTypes`: 24 of them,
     * 8 of each generation.
     */
    struct VisitorType {
        int generation = 0; // 0 children, 1 parents, 2 grandparents
        float speed = 1;    // SpeedMultiplier: of the children's pace
    };
    const std::vector<VisitorType> &visitorTypes() const { return _visitorTypes; }

    /** A visitor coming in, or in the park. */
    struct Visitor {
        int id = 0;
        uint32_t look = 0; // which of their generation's looks they have: the screen picks by it
        int generation = 0;
        float speed = 1;  // of kWalkPerSecond
        float walked = 0; // units walked since they came, which steps their walk
        float x = 0;      // where they are, in the world's units
        float z = 0;
        float headingX = 0; // the way they walk, a unit along the ground; 0, 0 standing
        float headingZ = 0;
        size_t leg = 0; // the waypoint they walk to next
        bool paid = false;
        bool inPark = false;
    };
    const std::vector<Visitor> &visitorsAbout() const { return _people; }

    /** What a park cell is, by the world's attribute map, and what's been built on it. */
    enum class Ground : uint8_t {
        kOutside, // not the park's to build on: its edges, its river, the road
        kOpen,    // the park's land, bare
        kPath,
        kQueue, // a ride's queue (not drawn yet)
    };
    /** The ground's size in cells, x across and y down; 0 for a world without its attribute map. */
    int groundWidth() const { return _mapWidth; }
    int groundHeight() const { return _mapHeight; }
    /** The ground of a cell; kOutside off the map, or for a world without one. */
    Ground ground(const Cell &cell) const;
    bool isPath(const Cell &cell) const { return ground(cell) == Ground::kPath; }
    /** Whether the prebuilt park's path and queues were laid. */
    bool prebuilt() const { return _prebuilt; }
    /** What a cell of path costs (the general settings' `Costs.PathCell`). */
    int pathCost() const { return _pathCost; }
    /**
     * The straight run of cells from `from` towards `to`, along whichever
     * of x and y they're farther apart in, both ends included: what the
     * path tool lays in one length.
     */
    static std::vector<Cell> pathRun(const Cell &from, const Cell &to);
    /**
     * Lays path on the cells of `cells` that are bare land, in order, at
     * pathCost() each, while the money lasts.
     * @return How many cells were laid.
     */
    int buildPath(const std::vector<Cell> &cells);

    /** How many visitors the park draws now: its points, at so many a visitor. */
    int visitorsWanted() const;
    /** The way a visitor walks in, cell by cell: off the bus, over the crossing, past a booth, in. */
    const std::vector<gfx::Vec3> &wayIn() const { return _wayIn; }
    /** How fast children walk, in units a second (guessed); the others by their type's speed. */
    static constexpr float kWalkPerSecond = 12;

    /** Money in the bank. */
    int cash() const { return _cash; }
    /** What a visitor pays to come in. */
    int admissionFee() const { return _admissionFee; }
    /** How many visitors are in the park. */
    int visitors() const { return _visitors; }
    /** Whether the park's clock runs: it stops while a window is open over it. */
    void setPaused(bool paused) { _paused = paused; }
    bool paused() const { return _paused; }

    /** The park's time, in milliseconds since it opened. */
    uint64_t millis() const { return _millis; }
    /** Days since the park opened on 1 January 2000. */
    uint32_t day() const { return uint32_t(_millis / kDayMillis); }
    /** Moves the park on by so many milliseconds of play: its clock, visitors coming and walking. */
    void advance(uint32_t ms);

    /** A date in the game's calendar. */
    struct Date {
        int day;   // 1 to 31
        int month; // 0 to 11
        int year;  // 2000 on
    };
    /** The date `days` after 1 January 2000. */
    static Date dateOf(uint32_t days);

    /** The middle of a cell on the ground, in the world's units (y up). */
    static gfx::Vec3 cellCentre(const Cell &cell);

private:
    static constexpr uint32_t kStepMillis = 100;
    void layPrebuilt(const SearchSet &files, const AttributeMap &map);
    void step(uint32_t ms);

    std::string _folder;
    FixedItems _fixed;
    Mission _mission;
    int _cash = 0;
    int _admissionFee = 0;
    int _visitors = 0;
    uint64_t _millis = 0;
    bool _paused = false;
    Arrivals _arrivals;
    int _pathCost = 0;
    int _mapWidth = 0; // the ground's cells, x across, y down, as the settings number them
    int _mapHeight = 0;
    std::vector<Ground> _ground;
    bool _prebuilt = false;
    std::vector<VisitorType> _visitorTypes;
    std::vector<gfx::Vec3> _wayIn;
    std::vector<Visitor> _people;
    uint64_t _nextArrivalMs = 0;
    int _nextId = 1;
};

} // namespace reluva::tpw
