// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/park/attractions.h"
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
     * mission's; its ground by the world's attribute map, with the path,
     * queues and objects of the game's prebuilt park (`PreBuilt/prebuilt.TPWS`) where
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
        int generation = 0;  // 0 children, 1 parents, 2 grandparents
        float speed = 1;     // SpeedMultiplier: of the children's pace
        int excitement = 50; // PreferredExcitement, 0 to 100
        int cash = 0;        // StartingCash
        int boredom = 50;    // BoredomThreshold: days with nothing to do before they go home (guessed)
    };
    const std::vector<VisitorType> &visitorTypes() const { return _visitorTypes; }

    /** How visitors behave, by the general settings' `PeepInfo` (see visitors.md). */
    struct VisitorRules {
        int cashSpreadPercent = 0;                        // StartingCashVarPc: each one's cash this much either way
        std::array<int, 3> generationPercent{70, 20, 10}; // PercentageOfKids, OfParents, OfGrandparents
        // A ride's happiness by how near its excitement is to what they like:
        // within PerfectExcitementMatch, PerfectRide; and so on.
        std::array<int, 3> rideMatch{5, 15, 40};
        std::array<int, 3> rideHappiness{25, 15, 5};
        // How much each counts in choosing where to go (DecisionVar...Weight).
        int distanceWeight = 1;
        int queueWeight = 1;
        int excitementWeight = 1;
        int thirstWeight = 2;
        int hungerWeight = 2;
        int toiletWeight = 2;
        int exitDays = 400;  // ExitLevel: how long they stay, in days (guessed)
        int exitSpread = 60; // ExitLevelVar, either way
        int needy = 80;      // ThoughtBubbleHungry and the like: a need this high makes them unhappy
        int happy = 80;      // ThoughtBubbleHappy
        int unhappy = 20;    // ThoughtBubbleUnhappy
        int sick = 80;       // ThoughtBubbleSick: a sickness this high, they think so
        int smallChange = 5; // Small, Medium and BigHappinessChange
        int mediumChange = 15;
        int bigChange = 25;
        int vomitDivisor = 10;     // RideVomitDivisor: a ride's excitement over this adds to sickness
        int vomitCapacity = 100;   // VomitCapacity: past this they're sick
        int prankersPercent = 10;  // PrankeryLikelihood: of a hundred visitors, the pranksters
        int stinkBombPercent = 25; // StinkbombLikelihood: of their pranks, stink bombs
        int prankUnhappy = 15;     // PrankHappinessThreshold: a prankster this unhappy plays them
        int prankPercent = 10;     // PrankLikelihood: a chance each day
        int rumourCells = 2;       // ViciousRumoursArea: how far whining is heard
        int weatherDays = 4;       // WeatherModifierFrequency: how often the weather tells on them
        int umbrellaShelter = 1;   // UmbrellaModifier: the unhappiness an umbrella saves
        // By generation and Park::Weather: the weather's effects on their
        // happiness, thirst and hunger (PeepHappinessWeatherEffects and
        // the like).
        std::array<std::array<float, 5>, 3> weatherHappiness{};
        std::array<std::array<float, 5>, 3> weatherThirst{};
        std::array<std::array<float, 5>, 3> weatherHunger{};
        std::array<int, 5> weatherArrivals{100, 100, 100, 100, 100}; // Arrival's percentage modifiers
    };
    const VisitorRules &visitorRules() const { return _rules; }

    /** The weather: the world's seasons' chances (Seasons[n].PercentageChanceFor...). */
    enum class Weather : uint8_t { kFine, kHeatwave, kRain, kStorm, kSnow };
    Weather weather() const { return _weather; }
    /** Sets the weather until its next change: for tests and the tools. */
    void setWeather(Weather weather) { _weather = weather; }

    /**
     * What a visitor thinks, shown in a bubble over them: THOUGHTS.str's
     * strings by these numbers.
     */
    enum class Thought : uint8_t {
        kNone,
        kHungry,
        kThirsty,
        kHungryAndThirsty,
        kToilet,
        kLiked,
        kDisliked,
        kMessy,
        kVeryHappy,
        kHappy,
        kOkay,
        kUnhappy,
        kBored,
        kAngry,
        kSick,
        kScared,
        kLineTooLong,
        kConfused,
        kWhining,
        kLikeWeather,
        kDislikeWeather,
        kNaughty,
    };
    /** How long a thought shows. */
    static constexpr uint64_t kThoughtMillis = 4000;

    /** Something on the ground: litter dropped, or where a visitor was sick. */
    struct Litter {
        float x = 0;
        float z = 0;
        int frame = 0; // in the litter's sprite sheet
        bool vomit = false;
    };
    const std::vector<Litter> &litter() const { return _litter; }
    /** A prankster's stink bomb, and when it went off and clears (the park's time). */
    struct StinkBomb {
        float x = 0;
        float z = 0;
        uint64_t since = 0;
        uint64_t until = 0;
    };
    const std::vector<StinkBomb> &stinkBombs() const { return _stinkBombs; }
    /** The most litter the park keeps: the oldest goes past it. */
    static constexpr size_t kMostLitter = 1000;

    /** What a visitor is doing. */
    enum class Doing : uint8_t {
        kComing,    // on their way in
        kWalking,   // about the paths
        kQueueing,  // in a ride's queue, or at its entrance to get on
        kRiding,    // on the ride, or in the shop
        kLeaving,   // from a ride's exit to the path
        kGoingHome, // out along the way in, then gone
    };
    /** A visitor coming in, or in the park. */
    struct Visitor {
        int id = 0;
        int generation = 0;
        float speed = 1;  // of kWalkPerSecond
        float walked = 0; // units walked since they came, which steps their walk
        float x = 0;      // where they are, in the world's units
        float z = 0;
        float headingX = 0; // the way they walk, a unit along the ground; 0, 0 standing
        float headingZ = 0;
        size_t leg = 0; // coming: the waypoint they walk to next
        bool paid = false;
        bool inPark = false;
        Doing doing = Doing::kComing;
        Cell cell;            // walking: the path cell walked to; leaving: the one they make for
        Cell from;            // walking: the one they came from
        int queue = -1;       // queueing, riding: the ride's queue
        float along = 0;      // queueing: units along the queue, from the path
        int lastQueue = -1;   // the queue of the ride last ridden, not joined again straight away
        int rides = 0;        // how many rides they've had
        uint32_t choices = 0; // how many ways they've chosen, which picks the next
        int type = 0;         // in visitorTypes()
        int cash = 0;
        // 0 to 100: how happy they are, and how hungry, thirsty and in
        // need of the toilet.
        float happiness = kStartingHappiness;
        float hunger = 0;
        float thirst = 0;
        float toilet = 0;
        float bored = 0;         // days since they last did something
        float daysLeft = 0;      // before they go home
        int target = -1;         // the queue they're making for; −1 for none
        std::vector<Cell> route; // walking to it: the path cells on to its end, the next last
        bool gone = false;       // home: taken away at the end of the step
        /**
         * Who they are, by the settings' numbering of people
         * (PersonSpriteUmbrellaModifiers): 0 to 3 children, 4 and 5
         * parents, 8 and 9 grandparents; the screen draws them by it.
         */
        int person = 0;
        bool female = false;
        float sickness = 0; // past VomitCapacity, they're sick
        int carrying = 0;   // litter from what they used: a shop's LitterEffect
        int appearance = 0; // what they've bought to carry: 1 a balloon, 2 a costume, 3 an umbrella
        bool prankster = false;
        Thought thought = Thought::kNone;
        uint64_t thoughtUntil = 0; // the park's time it shows till
        uint64_t nextThought = 0;  // when they next think of how they are
    };
    /** The settings' people are numbered from 0 to 9 (Visitor::person). */
    static constexpr size_t kPeople = 10;
    /** How happy a visitor is as they come in (guessed). */
    static constexpr float kStartingHappiness = 50;
    const std::vector<Visitor> &visitorsAbout() const { return _people; }

    /** What a park cell is, by the world's attribute map, and what's been built on it. */
    enum class Ground : uint8_t {
        kOutside, // not the park's to build on: its edges, its river, the road
        kOpen,    // the park's land, bare
        kPath,
        kQueue, // a ride's queue
        kTaken, // an object's: a ride, a feature, an obstacle
    };
    /** The ground's size in cells, x across and y down; 0 for a world without its attribute map. */
    int groundWidth() const { return _mapWidth; }
    int groundHeight() const { return _mapHeight; }
    /** The ground of a cell; kOutside off the map, or for a world without one. */
    Ground ground(const Cell &cell) const;
    bool isPath(const Cell &cell) const { return ground(cell) == Ground::kPath; }
    /** An object standing in the park, by its number (its settings' `Info.Id`). */
    struct PlacedObject {
        int id = 0;
        Cell at;       // its first cell, the corner its model is laid from
        int angle = 0; // degrees
    };
    const std::vector<PlacedObject> &objects() const { return _objects; }

    /**
     * A queue: its cells from the one by its attraction's entrance out,
     * and where it meets the path, if it does.
     */
    struct Queue {
        Cell entrance;
        std::vector<Cell> cells;
        bool atPath = false;
        Cell pathEnd;
        int object = -1; // its attraction, in objects(); −1 where it isn't known
        /**
         * An attraction without a queue (a shop, a sideshow, a toilet):
         * visitors walk from the path beside its entrance straight to it.
         * Not drawn, nor laid.
         */
        bool kiosk = false;
        int fare = 0;         // what a ride costs: its UsageInfo.InitPricePerUse
        bool hasExit = false; // where its riders come off
        Cell exit;
        std::vector<int> line; // the visitors in it, the first at its entrance first
    };
    const std::vector<Queue> &queues() const { return _queues; }

    /** The models a queue is drawn with, a cell each (`queue/` of the world's). */
    enum class QueueModel {
        kStraight,      // questra
        kBend,          // quebnd1: its side towards the path east, unturned
        kBendOther,     // quebnd2: south
        kJoin,          // queend: a straight, its white posts north, by the path
        kJoinBendSouth, // quebin1: a bend, its posts at its south side, by the path
        kJoinBendEast,  // quebin2: a bend, its posts at its east side
        kDeadEnd,       // quedead: open south alone
    };
    /** A queue's cell: its model, turned so many degrees (east towards south). */
    struct QueuePiece {
        Cell at;
        QueueModel model = QueueModel::kStraight;
        int angle = 0;
    };
    /**
     * The pieces a queue is drawn with, by its shape: straights and bends
     * along it, the piece by the path with its posts towards it, a dead end
     * where it stops short. See docs/engines/tpw/games/simcoaster/systems/paths.md#queues.
     */
    static std::vector<QueuePiece> queuePieces(const Queue &queue);
    /** Every queue's pieces. */
    std::vector<QueuePiece> queuePieces() const;

    /**
     * The cells a shape (`Info.Shape`) takes with its first cell at `at`,
     * turned `angle` degrees (east towards south): its rows from the last,
     * at `at`'s y, each further one y + 1; its columns x + 1 on; turned
     * about the first cell.
     */
    struct Footprint {
        std::vector<Cell> cells; // all it takes, its entrance and exit among them
        bool hasEntrance = false;
        Cell entrance;
        bool hasExit = false;
        Cell exit;
    };
    static Footprint footprint(const std::vector<std::string> &shape, const Cell &at, int angle);
    static constexpr int kNotPlaced = -2;
    /** Whether every cell of a footprint is bare land. */
    bool canPlace(const Footprint &footprint) const;
    /**
     * Places an attraction, if it fits and the money's there: its price
     * paid, its cells taken, and, if it has a queue, one begun at its
     * entrance.
     * @return The new queue's index, or −1 if it has none; kNotPlaced if
     *     it wasn't placed.
     */
    int place(int id, int price, const std::vector<std::string> &shape, bool hasQueue, const Cell &at, int angle);

    /** What a cell of queue costs (the general settings' `Costs.QueueCell`). */
    int queueCost() const { return _queueCost; }
    /**
     * The queue a length of queue can be laid on from a cell: one whose
     * entrance it is, while it has no cells, or whose last cell it is,
     * while it doesn't meet the path.
     * @return The queue's index, or −1.
     */
    int queueFrom(const Cell &cell) const;
    /**
     * The queue `index` with a length laid on from its end, through
     * `run` (pathRun()'s cells, its first the end): over bare land, until
     * it meets path, which it joins, or something it can't cross.
     */
    Queue queueWith(int index, const std::vector<Cell> &run) const;
    /**
     * Lays it, at queueCost() a cell while the money lasts.
     * @return How many cells were laid.
     */
    int layQueue(int index, const std::vector<Cell> &run);

    /** Whether the prebuilt park's path, queues and objects were laid. */
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

    /** The world's attractions, which the park's objects are. */
    const Attractions &attractions() const { return _attractions; }
    /** The queue of the attraction objects()[object]; −1 for none. */
    int queueOf(size_t object) const;
    /** A visitor by their number; nullptr if they're not about. */
    const Visitor *visitor(int id) const;
    /**
     * The visitor first in a queue, at its ride's entrance waiting to get
     * on; 0 for none.
     */
    int waitingAt(int queue) const;
    /** A visitor waiting at a queue's entrance gets on its ride, paying its fare; false if they weren't. */
    bool board(int queue, int id);
    /** A rider gets off, by the ride's exit, and makes for the path; false if they weren't on it. */
    bool letOff(int queue, int id);
    /** Room in a queue: this many visitors to a cell (guessed). */
    static constexpr int kVisitorsPerQueueCell = 3;

    /**
     * The park's points for drawing visitors: FixedRate, NewParkBonus and
     * each of its attractions' AttractionValue (guessed).
     */
    int points() const;
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
    // Which waypoint of the way in is the booth's, where a visitor pays, and
    // the entrance's, past which they're in the park.
    static constexpr size_t kBoothLeg = 3;
    static constexpr size_t kEntranceLeg = 4;
    void layPrebuilt(const SearchSet &files, const AttributeMap &map);
    Queue followQueue(const Cell &entrance) const;
    void step(uint32_t ms);
    void linkQueue(size_t object, const Footprint &footprint);
    void moveVisitor(Visitor &v, float left);
    void chooseWay(Visitor &v);
    void chooseTarget(Visitor &v);
    void useUp(Visitor &v, float days);
    void goHome(Visitor &v);
    void findKioskPaths();
    std::vector<Cell> routeTo(const Cell &from, const Cell &to) const;
    void addVisitor();
    void newDay();
    void rollWeather();
    void think(Visitor &v, Thought thought) const;
    Thought howTheyAre(const Visitor &v) const;
    void dropLitter(Visitor &v);
    int litterNear(float x, float z, float cells) const;
    bool objectNear(const Visitor &v, bool (*what)(const Attractions::Attraction &), int cells) const;
    std::vector<gfx::Vec3> queueRoute(const Queue &queue) const;

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
    std::vector<PlacedObject> _objects;
    std::vector<Queue> _queues;
    int _queueCost = 0;
    std::vector<VisitorType> _visitorTypes;
    VisitorRules _rules;
    std::array<std::array<int, 4>, 4> _seasons{}; // each season's chance of heatwave, rain, storm and snow, percent
    Weather _weather = Weather::kFine;
    uint32_t _lastDay = 0;
    std::vector<Litter> _litter;
    std::vector<StinkBomb> _stinkBombs;
    std::vector<gfx::Vec3> _wayIn;
    std::vector<Visitor> _people;
    Attractions _attractions;
    uint64_t _nextArrivalMs = 0;
    int _nextId = 1;
};

} // namespace reluva::tpw
