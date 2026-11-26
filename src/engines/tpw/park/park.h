// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/attribute_map.h"
#include "engines/tpw/formats/sam.h"
#include "engines/tpw/park/attractions.h"
#include "graphics/gfx3d.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <utility>
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
    /** How far a security camera sees, in cells, deterring pranks and counting for Best Security (guessed). */
    static constexpr int kCameraCells = 6;
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

    /** The staff's kinds, in the Hire Staff window's tabs' order. */
    enum class StaffKind : uint8_t { kEngineer, kJanitor, kGardener, kGuard, kEntertainer, kScientist };
    static constexpr size_t kStaffKinds = 6;

    /**
     * A mission (`missions/missNN.sam`): the campaign's objectives, in
     * order. See docs/engines/tpw/games/simcoaster/systems/missions.md.
     */
    struct Mission {
        int number = -1;
        // Where it's played (Completion.EndingTheme): 0 Arabian Nights, 1
        // Land of Invention, 2 Polar Zone; −1 any.
        int theme = -1;
        /** A statistic to reach (ExitStatistic[n]): its Type and TargetValue. */
        struct Objective {
            int statistic = 0; // 2 profit, 6 park awards, 7 percent of the shares, 8 visitors in the park at once
            int target = 0;
        };
        std::vector<Objective> objectives;
        int statistic = 0; // the first objective's, or 0
        int target = 0;
        bool noResearch = false; // Info.NoResearch
        int techGroup = -1;      // TechLevel.NewMaxGroup: the research groups it allows; −1 none set
        int techTheme = -1;      // in that world
        /** Staff an obstacle needs (ObstacleStaff[n]): so many of a kind, in the game's numbering, able so. */
        struct StaffNeed {
            int ability = 0; // in ABILITIES.str
            int staffType = 0;
            int number = 1;
        };
        int obstacle = 0;       // Obstacle.Type: the object it clears
        int obstacleEffort = 0; // Obstacle.ResearchEffort: its Park Expansion project's points
        std::vector<StaffNeed> staffNeeded;
        int goldenTickets = 0;  // Obstacle.GoldenTickets: needed to finish it
        int opens = 0;          // Completion.ObstacleOpened: an object that opens as it's done
        int themeOpened = -1;   // Completion.ThemeNumber, with ThemeOpened
        bool endOfGame = false; // Completion.EndOfGame
        int promotion = 0;      // Promotion.Number: the rank it needs, in PROMOTIONLEVELS.str
        int challenges = 0;     // RequiredChallenges.Number, won in RequiredChallenges.Theme
        int challengesTheme = -1;
        // Completion.LoadNewHoardings, BoundaryClicks: the park's fence as
        // it's done, its corners from by the entrance (inferred).
        bool newLand = false;
        std::vector<Cell> boundary;
        /** Whether its obstacle wants a solution: research, or trained staff. Others open of themselves. */
        bool needsSolution() const { return obstacle && (obstacleEffort > 0 || !staffNeeded.empty()); }
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
    /** The mission played here: the current objective, if it's in this world; else one numbered −1. */
    const Mission &mission() const { return _mission; }
    /** Every mission, by number. */
    const std::vector<Mission> &missions() const { return _missions; }
    /** The world's theme, as missions number them: 0 Arabian Nights, 1 Land of Invention, 2 Polar Zone. */
    int theme() const { return _theme; }
    /** The first mission not done: the player's objective, wherever it's played. */
    int progress() const { return _progress; }
    /** The themes the missions done here have opened. */
    const std::vector<int> &themesOpened() const { return _themesOpened; }
    /** Whether the last mission is done: the game's won. */
    bool won() const { return _won; }

    /** The player's golden tickets and rank, as their file keeps them. */
    void setPlayer(int goldenTickets, int rank) {
        _goldenTickets = goldenTickets;
        _rank = rank;
    }
    int goldenTickets() const { return _goldenTickets; }
    int rank() const { return _rank; }
    /** What a statistic stands at (Mission::Objective::statistic). */
    int statisticValue(int statistic) const;
    /** Whether the park may research: not in a mission with Info.NoResearch. */
    bool researchAllowed() const;
    /** Whether the mission's obstacle has been researched, and its staff are trained. */
    bool obstacleResearched() const { return _obstacleResearched; }
    /**
     * The staff trained for each of the obstacle's needs, as many as it
     * needs of them, each once, the first trained first (the game has the
     * player pick them: guessed).
     */
    std::vector<std::vector<int>> obstacleStaff() const;
    bool obstacleStaffReady() const;
    /** What "Proceed with solution" (UITEXT 592) on the obstacle came to. */
    enum class Solution { kCleared, kNoObstacle, kNotResearched, kNeedsStaff, kNeedsTickets, kNoMoney };
    Solution proceedWithSolution();
    /** The objects removed since last asked, and their queues (−1 for none), in order. */
    std::vector<std::pair<size_t, int>> takeRemovals() { return std::exchange(_removals, {}); }
    /**
     * The ability a member of staff learns in training: what the
     * obstacle wants of their kind, else what a special ride waiting to
     * be activated does, else 0.
     */
    int abilityToLearn(StaffKind kind) const;
    /** The kind of staff who learn an ability (ABILITIES.str's); nullopt for none. */
    static std::optional<StaffKind> abilityKind(int ability);
    /**
     * Whether an ability's been discovered: Ride Upgrading, Firefighting
     * and First Aid from the start; the others with the research of an
     * obstacle that needs them (guessed).
     */
    bool abilityDiscovered(int ability) const;

    /** Whether an object's a special ride not yet activated. */
    bool awaitsActivation(size_t object) const;
    /** The staff trained for each of a special ride's abilities, as obstacleStaff() does an obstacle's. */
    std::vector<std::vector<int>> activationStaff(size_t object) const;
    /** What activating a special ride ("Activate Ride", UITEXT 984) came to. */
    enum class Activation { kActivated, kNotSpecial, kUndiscovered, kNeedsStaff, kNeedsTickets };
    Activation activate(size_t object);
    /** The special rides activated, by their objects' numbers: the player's file keeps them. */
    std::vector<int> ridesActivated() const;
    void setRidesActivated(const std::vector<int> &ids);
    /** A theme's name's number in THEMENAMES.str: 0 Land of Invention, 1 Polar Zone, 3 Arabian Nights. */
    static int themeName(int theme);
    /** A staff kind by the missions' numbering (ObstacleStaff[n].StaffType). */
    static std::optional<StaffKind> missionStaffKind(int staffType);

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
        // Prices against what visitors think fair: below CheapPriceMultiplier
        // of it cheap, to AveragePriceMultiplier about right, from
        // ExpensivePriceMultiplier much too high; and their opinions of each,
        // OpinionForTooLow, AboutRight, TooHigh, MuchTooHigh, 0 to 100.
        float cheapPrice = 0.75f;
        float averagePrice = 1.25f;
        float expensivePrice = 2.0f;
        std::array<int, 4> priceOpinion{100, 50, 20, 0};
        int minimumEntryFee = 20; // MinimumEntryFee: below it everybody pays up happily
        int excitementToCost = 6; // ExcitementToCostDivisor: the park's excitement to a fair entry fee
    };
    const VisitorRules &visitorRules() const { return _rules; }

    /** Someone the staff agencies offer: their name's number in their kind's names table. */
    struct Candidate {
        int name = 0;
        int experience = 0; // 0 to kMostExperience
        int wage = 0;       // a month
        int agency = 0;     // StaffAgencies[n]
    };
    /** The most experience staff have (the general settings' comment). */
    static constexpr int kMostExperience = 100000;
    /** What a member of staff is about. */
    enum class StaffDoing : uint8_t {
        kWorking,     // about the paths, at their work
        kGoingToRest, // to the staff room
        kResting,     // in it, unseen
        kGoingToTrain,
        kTraining, // in the training room, unseen
        kStriking, // at the strike area, holding a sign
        kGoingToResearch,
        kResearching, // a scientist in the research lab, unseen
    };
    /** How content a member of staff starts (guessed). */
    static constexpr float kStaffStartingMood = 80;
    /** Those to be hired of a kind. */
    const std::vector<Candidate> &candidates(StaffKind kind) const { return _pool[size_t(kind)]; }
    /** A member of staff, walking the paths and doing their work. */
    struct StaffMember : Candidate {
        int id = 0;
        StaffKind kind = StaffKind::kJanitor;
        float x = 0;
        float z = 0;
        float headingX = 0;
        float headingZ = 0;
        float walked = 0;
        Cell cell;
        Cell from;
        std::vector<Cell> route; // the path cells on to their work, the next last
        bool working = false;    // at work where they stand: sweeping, mending, watering
        uint64_t workUntil = 0;
        StaffDoing doing = StaffDoing::kWorking;
        // 0 to 100: their energy, happiness and motivation.
        float energy = 100;
        float happiness = kStaffStartingMood;
        float motivation = kStaffStartingMood;
        uint32_t hiredDay = 0;
        int target = -1;         // the object they make for: a room, a ride to mend, a plant to water; −1 for none
        float trained = 0;       // days of training done
        bool patrolling = false; // kept to their patrol area, its corners' cells
        Cell patrolFrom;
        Cell patrolTo;
        int ability = 0; // trained in: ABILITIES.str's, 1 Ride Upgrading; 0 none
        int chasing = 0; // a guard's: the visitor chased; 0 none
        uint64_t chaseUntil = 0;
    };
    const std::vector<StaffMember> &staff() const { return _staff; }
    /** What hiring came to. */
    enum class Hiring { kHired, kNoSuchCandidate, kNeedsLab, kStrike };
    /**
     * Hires a candidate: they come in at the entrance's path and start
     * work, paid their wage each month. Scientists want a research room.
     */
    Hiring hire(StaffKind kind, size_t candidate);
    /** What the staff are paid a month, all told. */
    int wages() const;
    /** A member of staff by their number; nullptr if they're not employed. */
    const StaffMember *staffMember(int id) const;
    /** Lets a member of staff go. */
    void fire(int id);
    /** Sets a member of staff's monthly wage, between AllStaffConstants' MinWage and MaxWage. */
    void setWage(int id, int wage);
    /** How the Staff Finance Controls change a kind's wages (UITEXT 262 to 264). */
    enum class Adjustment { kSetTo, kChangeBy, kPercent };
    /** Changes all of a kind's wages so. */
    void adjustWages(StaffKind kind, Adjustment how, int amount);
    /** What sending for training came to (UITEXT 232, 233). */
    enum class Training { kSent, kTooInexperienced, kNoRoom, kNotNow };
    /**
     * Sends a member of staff to the training room, for
     * AllStaffConstants.TrainingExtraExperience more experience; they
     * need MinExperienceForTraining first.
     */
    Training sendForTraining(int id);
    /** How far through their training, 0 to 1. */
    float trainingProgress(const StaffMember &m) const;
    /** Keeps a member of staff to the cells between two corners, or lets them go anywhere. */
    void setPatrol(int id, const Cell &from, const Cell &to);
    void clearPatrol(int id);
    /** Whether a kind's on strike. */
    bool striking(StaffKind kind) const { return _strikes[size_t(kind)]; }
    /**
     * A member of staff demanding a raise (UITEXT 253), the first of those
     * waiting to be answered; nullptr for none.
     */
    struct WageDemand {
        int staff = 0;
        int raise = 0;
    };
    const WageDemand *wageDemand() const { return _demands.empty() ? nullptr : &_demands.front(); }
    /** How an offer was taken (UITEXT 255 to 257). */
    enum class Offer { kAccepted, kStays, kRefused };
    /**
     * Answers the first demand with a raise: the whole of it, or within
     * PercentageForBadWageDecision of it, they stay; short of that they
     * still want the whole, and the demand stands.
     */
    Offer offer(int raise);
    /** Lets the one demanding go. */
    void refuseDemand();

    /** Sends the nearest engineer free to mend an object, or calls them off. */
    void callEngineer(size_t object);
    void cancelEngineer(size_t object);
    /** Sends the nearest gardener free to water a plant. */
    void callGardener(size_t object);
    /** Opens or closes an attraction to visitors. */
    void setOpen(size_t object, bool open);
    /** What an object would fetch scrapped: its price by its Upgrades[0].ScrapValueYear percentages. */
    int scrapValue(size_t object) const;
    /** A level's settings of an object: its current one's by default. */
    const Attractions::Attraction::Level *levelOf(size_t object, int level = -1) const;
    /** ABILITIES.str's Ride Upgrading, which engineers learn in training (guessed). */
    static constexpr int kRideUpgrading = 1;
    /** Why research is held up: UITEXT's numbers. */
    static constexpr uint32_t kHeldUpByStrike = 300;
    static constexpr uint32_t kHeldUpByNoneAssigned = 301;
    static constexpr uint32_t kHeldUpByNoScientists = 302;
    static constexpr uint32_t kHeldUpByConsultantStrike = 303;
    static constexpr uint32_t kHeldUpByNoConsultant = 304;
    /** What an upgrade came to (UITEXT 11, 125). */
    enum class Upgrading { kStarted, kFullyUpgraded, kNotResearched, kNeedsEngineer, kNoMoney, kBusy };
    /**
     * Buys an object's next level: researched first where it costs
     * research, an engineer trained in Ride Upgrading in the park, its
     * CostOfUpgrade paid; closed for its DurationOfUpgrade the while.
     */
    Upgrading upgrade(size_t object);
    /** Stops an upgrade being made, its cost given back. */
    void cancelUpgrade(size_t object);
    /** Deletes an object, for its scrap value: its cells and queue cleared, its riders let off. */
    void remove(size_t object, bool sell = true);
    /** Names an object; empty gives it back its own. */
    void rename(size_t object, std::string name);

    /** A research project of the lab's two (UITEXT 285 on). */
    enum class ProjectKind : uint8_t { kAttraction, kUpgrade, kExpansion }; // kExpansion: the mission's obstacle
    struct Project {
        ProjectKind kind = ProjectKind::kAttraction;
        int id = 0;    // the attraction researched
        int level = 0; // an upgrade's, 1 or 2
        int cost = 0;  // its CostOfResearch: points, and dollars spent on them
        float done = 0;
        std::vector<int> scientists; // assigned
        bool finished = false;
    };
    static constexpr size_t kProjectSlots = 2;
    /** The lab's projects; empty slots without. */
    const std::array<std::optional<Project>, kProjectSlots> &projects() const { return _projects; }
    /** What could be researched: attractions not on sale, and upgrades not researched. */
    std::vector<Project> projectChoices() const;
    /** Sets a project going in a slot. */
    void startProject(size_t slot, const Project &project);
    void cancelProject(size_t slot);
    /** Assigns a scientist to a slot's project, or takes them off it. */
    void assign(size_t slot, int scientist);
    void unassign(size_t slot, int scientist);
    /** The lab's work rate, percent: Research.StartingWorkLoad at first. */
    int workRate() const { return _workRate; }
    void setWorkRate(int rate) { _workRate = std::clamp(rate, 0, 100); }
    /** Points a day a slot's project makes now; and, if it can't go on, why (UITEXT 300 to 304), else 0. */
    float researchPerDay(size_t slot) const;
    uint32_t projectHeldUp(size_t slot) const;
    /** Whether the park has a Research Lab, which research and scientists need. */
    bool hasResearchLab() const;
    /** Whether an attraction, or an upgrade of it, has been researched. */
    bool researched(int id, int level = 0) const;
    /** The staff kind a consultant of the game's numbering is (RStaff[0].SType); nullopt for none. */
    static std::optional<StaffKind> consultantKind(int stype);

    /** Opens or closes the park's gates: closed, none come in. */
    void setParkOpen(bool open) { _parkOpen = open; }
    bool parkOpen() const { return _parkOpen; }

    /** Those to be hired of a kind. */

    /** What a visitor thinks of a price against what they think fair. */
    enum class PriceOpinion : uint8_t { kCheap, kAboutRight, kTooHigh, kMuchTooHigh };
    PriceOpinion priceOpinion(int price, int fair) const;
    /**
     * What visitors think a fair entry fee: the park's attractions'
     * excitement over ExcitementToCostDivisor, not below MinimumEntryFee
     * (guessed).
     */
    int fairEntryFee() const;
    /** Sets what visitors pay to come in. */
    void setAdmissionFee(int fee) { _admissionFee = std::max(0, fee); }
    /** Sets what a go of a queue's attraction costs. */
    void setFare(int queue, int fare);

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
        // What their window and the visitors' statistics show: the rides
        // they've been on, what they've bought, the sideshows they've
        // played and won, what they've spent, and when they came in (the
        // park's time).
        int ridden = 0;
        int purchases = 0;
        int sideshowsPlayed = 0;
        int sideshowsWon = 0;
        int spent = 0;
        uint64_t arrived = 0;
    };
    /** The settings' people are numbered from 0 to 9 (Visitor::person). */
    static constexpr size_t kPeople = 10;
    /** How happy a visitor is as they come in (guessed). */
    static constexpr float kStartingHappiness = 50;
    const std::vector<Visitor> &visitorsAbout() const { return _people; }
    /** How a visitor is, as the thought they'd have of it (Thought: very happy to angry). */
    Thought mood(const Visitor &v) const { return howTheyAre(v); }

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
    /** The ground of a cell; kOutside off the map, beyond the park's fence, or for a world without one. */
    Ground ground(const Cell &cell) const;
    /**
     * Whether a cell is the park's, inside its fence: the world's
     * `hoardings.map`, grown by the missions done here. See
     * docs/engines/tpw/games/simcoaster/systems/land.md.
     */
    bool owns(const Cell &cell) const;
    bool isPath(const Cell &cell) const { return ground(cell) == Ground::kPath; }
    /** Whether path is laid on a cell, the park's or beyond its fence, as the prebuilt park has it there. */
    bool pathLaid(const Cell &cell) const;
    /** An object standing in the park, by its number (its settings' `Info.Id`). */
    struct PlacedObject {
        int id = 0;
        Cell at;             // its first cell, the corner its model is laid from
        int angle = 0;       // degrees
        float repair = 100;  // its state of repair, percent: worn by use, broken at 0
        bool broken = false; // till an engineer mends it
        bool open = true;    // closed by the player, none get on
        float water = 100;   // a plant's: dry at 0
        int engineer = 0;    // the staff member called to mend it; 0 for none
        int gardener = 0;    // to water it
        int users = 0;       // this month's, and last month's
        int lastUsers = 0;
        int takings = 0; // likewise, its fares
        int lastTakings = 0;
        int costs = 0; // likewise: its running cost and upgrades
        int lastCosts = 0;
        int goods = 0; // likewise: the cost of what it sold
        int lastGoods = 0;
        int profit = 0; // to date: takings less costs and goods, its months closed
        uint32_t placedDay = 0;
        int level = 0;         // its upgrades bought, 0 to 2
        float upgrading = 0;   // days left of an upgrade being made; 0 for none
        std::string name;      // the player's name for it; empty for its own
        bool prebuilt = false; // the prebuilt park's, there as it opened
        bool active = true;    // a special ride: activated, and so open
    };
    const std::vector<PlacedObject> &objects() const { return _objects; }
    /**
     * The world's fixed items, as the prebuilt park lists them unplaced:
     * the gates, the ticket booths' building, the bunting and the like,
     * by their numbers, each laid out from the world's corner
     * (Attraction::fixedInWorld).
     */
    const std::vector<int> &fixedObjects() const { return _fixedObjects; }

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
    /** Takes up a cell of path, as the bulldozer does: the land bare again, nothing paid back (guessed). */
    bool removePath(const Cell &cell);

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

    /**
     * The park's accounts' lines: its takings by what took them, then what
     * it spends on. See docs/engines/tpw/games/simcoaster/systems/finance.md.
     */
    enum class Ledger : uint8_t {
        kGate,
        kRides,
        kShops,
        kSideshows,
        kFacilities,
        kBuilding, // attractions, upgrades, path and queue; scrap sold off against it
        kRunning,  // running costs and the cost of goods
        kWages,
        kResearch,
        kLoans, // repayments
    };
    static constexpr size_t kLedgerLines = 10;
    static constexpr size_t kIncomeLines = 5; // the lines before kBuilding
    using Accounts = std::array<int, kLedgerLines>;
    /** This year's accounts so far, and last month's. */
    const Accounts &thisYear() const { return _thisYear; }
    const Accounts &lastMonth() const { return _lastMonth; }
    static int income(const Accounts &a);
    static int expenditure(const Accounts &a);
    /** What the park's attractions are worth: their scrap values (guessed). */
    int parkValue() const;
    /** How many visitors have come in; of a generation (0 children, 1 adults, 2 seniors). */
    int totalVisitors() const { return _totalVisitors; }
    int totalVisitors(int generation) const {
        return generation >= 0 && generation < 3 ? _generationVisitors[size_t(generation)] : 0;
    }

    /** A loan on offer (LoanInfo[n]), or taken. */
    struct Loan {
        int offer = 0;  // its LoanInfo number
        int lender = 0; // in LOANNAMES.str
        int amount = 0;
        float rate = 0; // APRInPercent
        int months = 0;
        int repayment = 0;  // monthly
        int monthsLeft = 0; // taken: months still to pay
        int balance = 0;    // taken: what's owed, less interest not yet due
        int totalPayable() const { return repayment * months; }
    };
    /** The loans on offer, those not taken. */
    std::vector<Loan> availableLoans() const;
    /** The loans taken and not paid off. */
    const std::vector<Loan> &loans() const { return _loans; }
    /** What the loans taken still owe. */
    int owed() const;
    /** Takes an offer: its amount paid in, repaid monthly from the next month. */
    bool takeLoan(int offer);
    /** Pays off a loan taken, by its place in loans(); false if the money isn't there (UITEXT 419). */
    bool repayLoan(size_t loan);

    /** The company's shares: those issued, the player's, and their price. */
    struct Shares {
        int issued = 30000;
        int owned = 1000;
        int price = 100;
    };
    const Shares &shares() const { return _shares; }
    /** Sets them: the player's, as their file keeps them. */
    void setShares(const Shares &shares) { _shares = shares; }

    /** How each kind of price is set in Attraction Finance Controls (UITEXT 184 to 189). */
    enum class PriceKind : uint8_t { kRide, kShop, kSideshow, kBathroom, kPhoto, kEntry };
    /** Changes every price of a kind so, as adjustWages does the wages. */
    void adjustPrices(PriceKind kind, Adjustment how, int amount);

    /**
     * A message to the player, in TAG_SYSTEM.str: its title and text, their
     * parameters, and lines that follow it (the annual report's).
     */
    struct Message {
        /** A line, and the change since last year after it (TAG_SYSTEM 271 to 273; 0 for none). */
        struct Line {
            uint32_t text = 0;
            std::map<int, std::string> values;
            uint32_t suffix = 0;
        };
        uint32_t title = 0;
        uint32_t text = 0;
        std::map<int, std::string> values;
        std::vector<Line> lines;
        bool objectiveLetter = false; // its text is OBJECTIVE_COMPLETE.str's, by the mission done
        int challenge = 0;            // a challenge whose title (CHALLENGE_TITLES.str) follows it
        uint32_t sender = 0;          // its sender's name in TAG_SYSTEM; 0 the president
        int rank = 0;                 // a rank to name in its {4} (PROMOTIONLEVELS.str); 0 none
    };
    /** The messages since last asked, oldest first. */
    std::vector<Message> takeMessages() { return std::exchange(_messages, {}); }
    /**
     * A challenge (`Challenges.sam`'s Challenges[n]): what it asks, how
     * long for, and what it wins. See
     * docs/engines/tpw/games/simcoaster/systems/challenges.md.
     */
    struct Challenge {
        int number = 0;        // n, its title's in CHALLENGE_TITLES.str and its letters' in CHALLENGE_SPECIFIC.str
        int type = 0;          // what it asks: 2 hot dogs sold, 37 visitors kept in the park, and so on
        int time = 0;          // TargetTime: days it runs
        int target = 0;        // TargetVal
        int targetObject = 0;  // TargetObj: the attraction it's about
        int target2 = 0;       // TargetVal2: a special ride's second measure
        int preparation = 0;   // GracePeriodLimit: days to prepare before it runs
        int cash = 0;          // CashPrize
        int tickets = 1;       // GTPrize: golden tickets won
        int shares = 0;        // SharePrize: shares won
        int promotion = 0;     // PromoGiven: the rank it promotes to; 0 none
        int missionNeeded = 0; // MissionNeededToActivate: the mission the player must be at
        int trigger = 0;       // TriggerData, as its type reads it
        int daysDelay = 0;     // DaysDelay: days from its trigger to its offer
        int sender = 1;        // Sender: 1 the president, 2 to 6 the directors
        int priceWon = 0;      // SharePriceEffectWon, percent
        int priceLost = 0;     // SharePriceEffectLost
    };
    enum class ChallengeStatus : uint8_t { kNotYet, kAvailable, kPreparing, kActive, kComplete };
    /** One of this world's challenges as it stands. */
    struct ChallengeState {
        Challenge challenge;
        ChallengeStatus status = ChallengeStatus::kNotYet;
        int failures = 0;
        int daysLeft = 0;   // preparing or active
        int baseline = 0;   // its count as it began, for those that count
        int baseline2 = 0;  // a second count: the pranks, or the visitors come in
        int triggered = -1; // the day its trigger was met; −1 not yet
        bool warned = false;
    };
    /** This world's challenges (its ChallengesInThisLevel), in their order. */
    const std::vector<ChallengeState> &challenges() const { return _challenges; }
    /** Takes up an available challenge, by its place in challenges(); false if it isn't, or another's under way. */
    bool acceptChallenge(size_t index);
    /** Where a challenge's measure stands: what's sold, used, kept; the lowest of what's watched. */
    int challengeValue(size_t index) const;
    /** A special ride's challenge's second measure: its ride's state of repair, or satisfaction. */
    int challengeValue2(size_t index) const;
    /** The challenges the player may still fail before they're fired (FiringLineForRank[rank].Limit). */
    int failuresLeft() const;
    /** The player's record, as their file keeps it: the challenges won, by number, and those failed. */
    void setChallengeRecord(std::vector<int> won, int failures);
    const std::vector<int> &challengesWon() const { return _challengesWon; }
    /** The challenges won of a world's (its theme's, as missions number them). */
    int challengesWonIn(int theme) const;
    int challengesFailed() const { return _challengesFailed; }
    /** Whether the player's failed too many challenges and been fired (TAG_SYSTEM 167). */
    bool fired() const { return _fired; }
    /** The park awards (UITEXT 453 to 460), in their order. */
    enum class Award : uint8_t {
        kHighestCoaster,
        kFastestCoaster,
        kCleanestPark,
        kMostDecoration,
        kBestSecurity,
        kBestStaffTraining,
        kFastestGokart,
        kLongestWaterRide,
    };
    static constexpr size_t kAwardCount = 8;
    /** How the park stands for an award: whether it's measured yet, its measure, what's needed, and if it's held. */
    struct AwardStanding {
        bool measured = false; // the coasters', go-kart tracks' and water rides' aren't: there are none yet
        int value = 0;         // percent
        int needed = 0;
        bool held = false;
    };
    /** Each award as the park stands now. See docs/engines/tpw/games/simcoaster/systems/park-awards.md. */
    std::array<AwardStanding, kAwardCount> awards() const;
    int awardsHeld() const;
    /** Months the park's ended in the red, in a row. */
    int monthsInRed() const { return _monthsInRed; }
    /** Whether the park has been in the red six months, and is closed down (TAG_SYSTEM 166). */
    bool bankrupt() const { return _bankrupt; }
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
    float priceFeeling(PriceOpinion opinion) const;
    void loadStaffRules(const SamFile &general);
    void fillPool(bool start);
    void moveStaff(StaffMember &m, float left);
    void staffWay(StaffMember &m);
    void staffDay();
    void staffMonth();
    void objectsDay();
    void wear(size_t object);
    Cell pathBeside(size_t object) const;
    int nearestFree(StaffKind kind, const Cell &to) const;
    int findObject(bool (*what)(const Attractions::Attraction &)) const;
    void sendTo(StaffMember &m, int object, StaffDoing doing);
    void finishWork(StaffMember &m);
    void researchDay();
    void loadFinance(const SamFile &general, const SamFile *world);
    Error loadMissions(const SearchSet &files, int mission);
    void startMission(int number);
    void missionDay();
    void completeMission();
    void researchedObstacle();
    void loadChallenges(const SearchSet &files, const SamFile &general, const SamFile *world);
    void loadAwards(const SamFile &general);
    void loadLand(const SearchSet &files);
    /** Adds the land inside a mission's fence (Mission::boundary). */
    void growLand(const std::vector<Cell> &clicks);
    void challengeDay();
    bool challengeTriggered(const Challenge &c) const;
    bool challengeKept(const Challenge &c, int value) const;
    int challengeOutcome(size_t index, bool timeUp) const;
    void startCounting(size_t index);
    bool amongTopThoughts(std::initializer_list<Thought> thoughts) const;
    int satisfaction() const;
    void endChallenge(ChallengeState &state, bool won);
    void postChallenge(const Challenge &c, int event);
    int techCap() const;
    void clearObstacle(int id);
    void setResearchables();
    void book(Ledger line, int amount);
    void financeDay();
    void financeMonth();
    void annualReport();
    void post(uint32_t title, uint32_t text, std::map<int, std::string> values = {});
    void chase(Visitor &v);
    bool wantsToStrike(StaffKind kind) const;
    bool guarded(float x, float z) const;
    void think(Visitor &v, Thought thought) const;
    Thought howTheyAre(const Visitor &v) const;
    void dropLitter(Visitor &v);
    int litterNear(float x, float z, float cells) const;
    bool objectNear(const Visitor &v, bool (*what)(const Attractions::Attraction &), int cells) const;
    std::vector<gfx::Vec3> queueRoute(const Queue &queue) const;

    std::string _folder;
    FixedItems _fixed;
    Mission _mission;
    std::vector<Mission> _missions;
    int _theme = -1;
    int _progress = 0;
    bool _obstacleResearched = false;
    std::vector<int> _themesOpened;
    std::vector<std::pair<size_t, int>> _removals;
    bool _won = false;
    int _goldenTickets = 0;
    int _rank = 1;
    int _cash = 0;
    Accounts _thisMonth{};
    Accounts _lastMonth{};
    Accounts _thisYear{};
    // Last year's, for the annual report: revenue, profit, park value, visitors, satisfaction.
    std::array<int, 5> _lastReport{};
    bool _reported = false; // there's a last year to compare with
    int _totalVisitors = 0;
    std::array<int, 3> _generationVisitors{};
    int _yearVisitors = 0;
    std::vector<Loan> _offers;
    std::vector<Loan> _loans;
    Shares _shares;
    int _similarPercent = 3;    // ShareInfo.AnnRptSimilarPercentage
    int _bigChangePercent = 45; // AnnRptBigChangePercentage
    int _monthsInRed = 0;
    bool _wasInRed = false;
    bool _beenInRed = false;
    bool _bankrupt = false;
    bool _lowWarned = false;
    bool _repayAdvised = false;
    std::vector<Message> _messages;
    std::vector<ChallengeState> _challenges;
    /** ParkAward's settings. */
    struct AwardRules {
        int coasterHeight = 110;
        int coasterSpeed = 100;
        int hygiene = 90;           // percent of the paths' cells unlittered
        int hygieneMinCells = 1000; // HygieneMinParkSize: cells covered by objects
        int security = 25;          // percent patrolled plus percent watched
        int decoration = 15;        // percent of the covered cells features'
        int training = 50;          // percent of the staff with an ability
        int trainingMinStaff = 10;
        int gokartSpeed = 5;
        int waterLength = 50;
    };
    AwardRules _awardRules;
    std::vector<int> _challengesWon;
    std::map<int, int> _challengeThemes; // each challenge's world, by number
    int _challengesFailed = 0;
    bool _fired = false;
    int _daysBetweenOffers = 20;                        // Challenges.DaysBetweenOffers
    int _warningPercent = 20;                           // ShortTimeLeftWarningAt
    std::array<int, 6> _firingLine{255, 5, 5, 5, 5, 5}; // FiringLineForRank[rank].Limit
    int _lastOffer = -1000;                             // the day a challenge was last offered
    // What the challenges count: each shop type's sales and takings less
    // goods, each attraction's users, pranks, heatwaves.
    std::array<int, 16> _sold{};
    std::array<int, 16> _shopProfit{};
    std::map<int, int> _usesById;
    int _kidPranks = 0;
    int _adultPranks = 0;
    int _heatwaves = 0;
    int _admissionFee = 0;
    int _visitors = 0;
    uint64_t _millis = 0;
    bool _paused = false;
    Arrivals _arrivals;
    int _pathCost = 0;
    int _mapWidth = 0; // the ground's cells, x across, y down, as the settings number them
    int _mapHeight = 0;
    std::vector<Ground> _ground;
    std::vector<uint8_t> _owned; // by cell, as _ground; 1 where it's the park's
    bool _prebuilt = false;
    std::vector<int> _fixedObjects;
    std::vector<PlacedObject> _objects;
    std::vector<Queue> _queues;
    int _queueCost = 0;
    std::vector<VisitorType> _visitorTypes;
    VisitorRules _rules;
    std::array<std::array<int, 4>, 4> _seasons{}; // each season's chance of heatwave, rain, storm and snow, percent
    Weather _weather = Weather::kFine;
    uint32_t _lastDay = 0;
    std::vector<Litter> _litter;
    /** By kind: StaffPoolInfo's, and their work's by the kind's settings. */
    struct StaffRules {
        int averageExperience = 20000;
        int beginning = 5;
        int most = 6;
        float minRange = 3; // MinDetectionRange, or MinActiveRange for entertainers: cells
        float maxRange = 5;
        float minWork = 40; // MinWorkDuration and MaxWorkDuration: tenths of a second (guessed)
        float maxWork = 5;
        int maxWage = 80; // <Kind>.MaxWage: what the most experienced expect
    };
    std::array<StaffRules, kStaffKinds> _staffRules{};
    struct Agency {
        int level = 0;
        int wage = 10;
        int experience = 10;
        int recuperation = 10; // RecuperationModifier and HappinessRecuperationModifier, tenths
        int happinessRecuperation = 10;
        int training = 10;       // TrainingExperienceGainModifier, tenths
        int experienceGain = 10; // ExperienceGainModifier, tenths
    };
    // AllStaffConstants', Strike's and RideBreakDown's.
    struct StaffConstants {
        int minWage = 1;
        int maxWage = 500;
        int badWagePercent = 10;
        int minExperienceForTraining = 30000;
        int happyWage = 2;
        int unhappyWage = 5;
        int lowestWageHappiness = 20;
        int slackingEnergy = 50;
        int slackingCells = 10;
        int trainingExperience = 5000;
        float minRecuperation = 0.2f;
        float maxRecuperation = 0.75f;
        float minHappinessRecuperation = 1;
        float maxHappinessRecuperation = 3;
        int strikeMonths = 12;
        int strikeStaff = 3;
        int strikeEnergy = 20;
        int strikeHappiness = 30;
        int strikeMotivation = 30;
        float breakdownImminent = 10;
        Cell strikeFrom; // the world's FixedItemInfo.StrikeArea
        Cell strikeSize{1, 1};
    };
    StaffConstants _constants;
    // By kind and grade: ExperiencePerMonth (<Kind>ConstsPerGrade[n]).
    std::array<std::array<int, 5>, kStaffKinds> _experiencePerMonth{};
    int _staffRoomCapacity = 10;
    std::array<std::optional<Project>, kProjectSlots> _projects;
    std::vector<std::pair<int, int>> _researched; // (id, level)
    int _workRate = 85;
    float _researchSpeed = 0.005f; // Research.SpeedScaleDown
    float _researchCost = 1;       // Research.CostScaleUp
    float _minResearchAbility = 2;
    float _maxResearchAbility = 6;
    std::array<bool, kStaffKinds> _strikes{};
    std::vector<WageDemand> _demands;
    bool _parkOpen = true;
    std::vector<Agency> _agencies;
    int _minExperience = 1000;
    int _experienceSpread = 15000;
    float _fastest = 1.5f;                              // MaxSpeedDifference: how much faster the most experienced walk
    uint32_t _poolDays = 90;                            // TimeBetweenStaffUpdates
    std::array<int, 5> _entertaining{5, 8, 12, 16, 20}; // EntertainerConstsPerGrade[n].HappinessEffectOnCell
    std::array<std::vector<Candidate>, kStaffKinds> _pool;
    std::vector<StaffMember> _staff;
    uint32_t _candidatesMade = 0;
    int _nextStaffId = 1;
    std::vector<StinkBomb> _stinkBombs;
    std::vector<gfx::Vec3> _wayIn;
    std::vector<Visitor> _people;
    Attractions _attractions;
    uint64_t _nextArrivalMs = 0;
    int _nextId = 1;
};

} // namespace reluva::tpw
