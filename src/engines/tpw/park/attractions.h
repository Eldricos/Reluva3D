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
        bool relief = false;           // UsageInfo.ProvidesRelief: a toilet
        bool handlesSprite = false;    // UsageInfo.RideHandlesSprite: its user stays seen, as its script has them
        int litterEffect = 0;          // UsageInfo.LitterEffect: the litter its users carry away
        int vomitEffect = 0;           // UsageInfo.VomitEffect: what it adds to their sickness
        int appearance = 0;            // UsageInfo.AppearanceEffect: 1 a balloon, 2 a costume, 3 an umbrella
        int gender = 0;                // Info.GenderSpecific: 1 boys only, 2 girls only
        bool holdsLitter = false;      // UsageInfo.HoldsLitter: a bin
        bool indoors = false;          // UsageInfo.ISIndoors: shelter from the rain
        bool security = false;         // UsageInfo.ProvidesSecurity: a camera
        bool lab = false;              // UsageInfo.ThinkingSpace: a research room, which scientists need
        bool trainingRoom = false;     // UsageInfo.TrainingRoom
        bool requiresWatering = false; // UsageInfo.RequiresWatering: a plant
        int wearRate = 0;              // Upgrades[0].WearRate, out of 10: a ride's wear
        int runningCost = 0;           // Upgrades[0].RunningCost, a month
        // Upgrades[0].ScrapValueYear1 to 4: what it fetches scrapped, percent of its price.
        std::array<int, 4> scrapPercent{50, 30, 20, 10};
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

private:
    std::vector<Attraction> _all;
};

} // namespace reluva::tpw
