// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The things a world has to build, from each one's settings in its
 * folder (`levels/science/rides/robot/robot.sam`), over its kind's
 * (`rides/Rides.sam`). See docs/engines/tpw/games/simcoaster/systems/building.md.
 */
class Attractions {
public:
    /** The Buy window's tabs, in its order. */
    enum class Kind { kRide, kSideshow, kShop, kFacility, kFeature };
    static constexpr size_t kKinds = 5;

    struct Attraction {
        int id = 0; // Info.Id
        Kind kind = Kind::kRide;
        std::string folder;     // levels/science/rides/robot
        std::string model;      // its folder's model, robot.MD2
        std::string name;       // Info.Name, as the settings have it: "Giant Robot Swing"
        int price = 0;          // Upgrades[0].CostOfUpgrade
        int fare = 0;           // what a visitor pays for a go: UsageInfo.InitPricePerUse
        bool hasQueue = false;  // Info.HasQueue
        bool available = false; // on sale from the start: Research.Group 0, and a price
        /**
         * Info.Shape: its rows, the last at the first cell, each cell `.`
         * free, `2` its entrance, `N` its exit, anything else its own.
         */
        std::vector<std::string> shape;
        /**
         * Info.Hoarding: the boarding round a special ride till it's
         * activated, as its shape's rows: `F` `7` `L` `J` its corners, `^`
         * `_` `[` `]` its top, bottom, left and right edges, `.` none.
         */
        std::vector<std::string> hoarding;
        int excitement = -1;     // UsageInfo.ExcitementLevel, 0 to 100; −1 without
        int capacity = -1;       // Upgrades[0].InitCapacity
        int workingLife = -1;    // Info.WorkingLife, 0 to 100
        int attractionValue = 0; // Info.AttractionValue: the visitors it draws to the park
        bool choosable = false;  // Info.IsChoosable: visitors choose to use it
        // What using it does to a visitor (UsageInfo's): their thirst and
        // hunger taken away, their need of the toilet and happiness added.
        int thirstEffect = 0;
        int hungerEffect = 0;
        int toiletEffect = 0;
        int happinessEffect = 0;
        bool relief = false;        // UsageInfo.ProvidesRelief: a toilet
        bool handlesSprite = false; // UsageInfo.RideHandlesSprite: its user stays seen, as its script has them
        int litterEffect = 0;       // UsageInfo.LitterEffect: the litter its users carry away
        int vomitEffect = 0;        // UsageInfo.VomitEffect: what it adds to their sickness
        int appearance = 0;         // UsageInfo.AppearanceEffect: 1 a balloon, 2 a costume, 3 an umbrella
        int gender = 0;             // Info.GenderSpecific: 1 boys only, 2 girls only
        int rideType = -1;          // Info.RideTypeStringIndex: a sideshow's game, 19 to 23

        bool holdsLitter = false;      // UsageInfo.HoldsLitter: a bin
        bool indoors = false;          // UsageInfo.ISIndoors: shelter from the rain
        bool security = false;         // UsageInfo.ProvidesSecurity: a camera
        bool lab = false;              // UsageInfo.ThinkingSpace: a research room, which scientists need
        bool trainingRoom = false;     // UsageInfo.TrainingRoom
        bool requiresWatering = false; // UsageInfo.RequiresWatering: a plant
        // An obstacle (Info.IsObstacle, the world's obstacles folder): what
        // clearing it leaves, Obstacle.ReplaceWithID's object or nothing
        // (DeleteAfterUse); and whether it opens of itself as its mission's
        // done (OpenedAutomaticly).
        bool obstacle = false;
        // UsageInfo.ShopType: what a shop sells, by the shops' settings: 2
        // hot dogs (and chips), 4 drinks (and candy floss), 6 balloons, 5
        // costumes; −1 for none.
        int shopType = -1;
        // A special ride: closed till it's activated with its golden
        // tickets (UsageInfo.GoldenTicketCost) and staff of its abilities
        // (SkillSet[ability].Num: so many, an ability of ABILITIES.str).
        bool special = false;
        int ticketCost = 0;
        // Info.DontApplyOffset: a fixed item, its model laid out from the
        // world's corner rather than its cell ("a fixed item whose animation
        // should be played relative to world (0,0)"), or from the cell
        // Info.EngineMapOffsetOverrideX and Y give (the gates' (46, 17)).
        bool fixedInWorld = false;
        int fixedX = 0;
        int fixedY = 0;
        std::vector<std::pair<int, int>> skills; // ability, how many
        int replaceWith = 0;
        bool openedAutomatically = false;
        int wearRate = 0;    // Upgrades[0].WearRate, out of 10: a ride's wear
        int runningCost = 0; // Upgrades[0].RunningCost, a month
        // Upgrades[0].ScrapValueYear1 to 4: what it fetches scrapped, percent of its price.
        std::array<int, 4> scrapPercent{50, 30, 20, 10};
        /** A level of it, bought as an upgrade: Upgrades[n]'s settings. */
        struct Level {
            int capacity = -1; // InitCapacity
            int duration = 0;  // InitDuration
            int wearRate = 0;
            int runningCost = 0;
            int cost = 0;     // CostOfUpgrade: 0, none to buy
            int research = 0; // CostOfResearch: what researching it costs; 0, none needed
            int days = 0;     // DurationOfUpgrade
        };
        std::array<Level, 3> levels{};
        int costOfGoods = 0;   // UsageInfo.InitCostOfGoods: what a sale costs a shop
        int researchGroup = 0; // Research.Group: 0 on sale from the start, 1 on by research
        /**
         * RStaff[0].SType: the kind of staff to consult on researching it,
         * by the game's numbering (1 a gardener, 3 an engineer, 5 a guard,
         * by the recording's list); 0 none.
         */
        int consultant = 0;
        // How much more or less it's wanted in each weather, by Park::Weather:
        // its WeatherModifier percentages.
        std::array<int, 5> weatherPercent{100, 100, 100, 100, 100};
        // How much each generation wants it, percent: GenerationModifier's Kid,
        // Parent and GrandparentScorePercentage.
        std::array<int, 3> generationPercent{100, 100, 100};
        int expectedPrice = 0; // Info.ExpectedPrice: what visitors think a go is worth; its fare without one
    };

    /**
     * Reads the attractions of the world in `folder` (`levels/science`):
     * each of its rides', sideshows', shops', features' and facilities'
     * folders with its settings.
     */
    Error load(const SearchSet &files, const std::string &folder);

    const std::vector<Attraction> &all() const { return _all; }
    /** One by its number; nullptr if the world hasn't it. */
    const Attraction *find(int id) const;
    /** Those of a kind on sale, in the order read. */
    std::vector<const Attraction *> onSale(Kind kind) const;
    /** Puts one on sale, researched. */
    void markAvailable(int id);
    /** Puts one on sale, if it has a price, or takes it off. */
    void setAvailable(int id, bool available);

private:
    std::vector<Attraction> _all;
};

} // namespace reluva::tpw
