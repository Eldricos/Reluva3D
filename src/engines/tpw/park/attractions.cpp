// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Which are on sale from the start is set against the recording's Buy
// window: docs/engines/tpw/games/simcoaster/systems/building.md.

#include "engines/tpw/park/attractions.h"

#include "engines/tpw/formats/sam.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace reluva::tpw {

namespace {

// The kinds' folders of a world, as their names are (any case).
// Obstacles are features no one can buy.
const std::array<std::pair<const char *, Attractions::Kind>, 6> kKindFolders{{
    {"rides", Attractions::Kind::kRide},
    {"sideshow", Attractions::Kind::kSideshow},
    {"shops", Attractions::Kind::kShop},
    {"facilities", Attractions::Kind::kFacility},
    {"features", Attractions::Kind::kFeature},
    {"obstacles", Attractions::Kind::kFeature},
}};
constexpr const char *kOnline = "online_"; // the online game's settings, beside the kinds'

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::vector<std::string> split(const std::string &path) {
    std::vector<std::string> parts;
    size_t at = 0;
    for (size_t slash; (slash = path.find('/', at)) != std::string::npos; at = slash + 1)
        parts.push_back(path.substr(at, slash - at));
    parts.push_back(path.substr(at));
    return parts;
}

Error readSam(const SearchSet &files, const std::string &name, SamFile &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    return out.parse(data);
}

} // namespace

Error Attractions::load(const SearchSet &files, const std::string &folder) {
    _all.clear();
    const std::string prefix = lower(folder) + "/";
    // The world's files by their parts: <kind>/<kind>.sam, and
    // <kind>/<object>/<object>.sam with its model beside it.
    std::map<std::string, std::string> kindSettings; // by kind folder, lower case
    std::map<std::string, std::string> models;       // by object folder, lower case
    std::vector<std::string> objects;
    for (const std::string &name : files.list()) {
        const std::string low = lower(name);
        if (low.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::vector<std::string> parts = split(low.substr(prefix.size()));
        if (parts.size() == 2 && parts[1].size() > 4 && parts[1].compare(parts[1].size() - 4, 4, ".sam") == 0 &&
            parts[1].compare(0, std::char_traits<char>::length(kOnline), kOnline) != 0)
            kindSettings[parts[0]] = name;
        if (parts.size() != 3)
            continue;
        const std::string stem = parts[2].size() > 4 ? parts[2].substr(0, parts[2].size() - 4) : "";
        const std::string ending = parts[2].size() > 4 ? parts[2].substr(parts[2].size() - 4) : "";
        if (stem != parts[1])
            continue;
        if (ending == ".sam")
            objects.push_back(name);
        else if (ending == ".md2")
            models[parts[0] + "/" + parts[1]] = name;
    }
    for (const auto &[kindName, kind] : kKindFolders) {
        SamFile defaults;
        const auto k = kindSettings.find(kindName);
        if (k != kindSettings.end() && readSam(files, k->second, defaults))
            continue;
        for (const std::string &name : objects) {
            const std::vector<std::string> parts = split(lower(name).substr(prefix.size()));
            if (parts[0] != kindName)
                continue;
            SamFile own;
            if (readSam(files, name, own))
                continue;
            auto number = [&](const char *key, double fallback) {
                return own.number(key, defaults.number(key, fallback));
            };
            Attraction a;
            a.id = int(own.number("Info.Id", -1));
            if (a.id < 0)
                continue;
            a.kind = kind;
            a.folder = name.substr(0, name.rfind('/'));
            const auto model = models.find(parts[0] + "/" + parts[1]);
            if (model != models.end())
                a.model = model->second;
            a.name = own.text("Info.Name");
            a.price = int(number("Upgrades[0].CostOfUpgrade", 0));
            a.hasQueue = number("Info.HasQueue", 0) != 0;
            a.obstacle = number("Info.IsObstacle", 0) != 0;
            a.available = !a.obstacle && number("Research.Group", -1) == 0 && a.price > 0;
            // An obstacle's shape marks land its own way, which isn't worked
            // out: it stands on its first cell.
            if (const SamFile::Entry *shape = own.find("Info.Shape"); shape && !a.obstacle)
                a.shape = shape->grid;
            a.replaceWith = int(number("Obstacle.ReplaceWithID", 0));
            a.openedAutomatically = number("Obstacle.OpenedAutomaticly", 0) != 0;
            a.excitement = int(number("UsageInfo.ExcitementLevel", -1));
            a.fare = int(number("UsageInfo.InitPricePerUse", 0));
            a.capacity = int(number("Upgrades[0].InitCapacity", -1));
            a.workingLife = int(number("Info.WorkingLife", -1));
            a.attractionValue = int(number("Info.AttractionValue", 0));
            a.choosable = number("Info.IsChoosable", 0) != 0;
            a.thirstEffect = int(number("UsageInfo.ThirstEffect", 0));
            a.hungerEffect = int(number("UsageInfo.HungerEffect", 0));
            a.toiletEffect = int(number("UsageInfo.ToiletEffect", 0));
            a.happinessEffect = int(number("UsageInfo.HappinessEffect", 0));
            a.relief = number("UsageInfo.ProvidesRelief", 0) != 0;
            a.handlesSprite = number("UsageInfo.RideHandlesSprite", 0) != 0;
            a.litterEffect = int(number("UsageInfo.LitterEffect", 0));
            a.vomitEffect = int(number("UsageInfo.VomitEffect", 0));
            a.appearance = int(number("UsageInfo.AppearanceEffect", 0));
            a.gender = int(number("Info.GenderSpecific", 0));
            a.holdsLitter = number("UsageInfo.HoldsLitter", 0) != 0;
            a.indoors = number("UsageInfo.ISIndoors", 0) != 0;
            a.security = number("UsageInfo.ProvidesSecurity", 0) != 0;
            a.lab = number("UsageInfo.ThinkingSpace", 0) != 0;
            a.trainingRoom = number("UsageInfo.TrainingRoom", 0) != 0;
            a.requiresWatering = number("UsageInfo.RequiresWatering", 0) != 0;
            for (size_t l = 0; l < a.levels.size(); ++l) {
                const std::string at = "Upgrades[" + std::to_string(l) + "].";
                auto value = [&](const char *name, double fallback) {
                    return int(number((at + name).c_str(), fallback));
                };
                Attraction::Level &level = a.levels[l];
                level.capacity = value("InitCapacity", -1);
                level.duration = value("InitDuration", 0);
                level.wearRate = value("WearRate", 0);
                level.runningCost = value("RunningCost", 0);
                level.cost = value("CostOfUpgrade", 0);
                level.research = value("CostOfResearch", 0);
                level.days = value("DurationOfUpgrade", 0);
            }
            a.wearRate = a.levels[0].wearRate;
            a.runningCost = a.levels[0].runningCost;
            a.costOfGoods = int(number("UsageInfo.InitCostOfGoods", 0));
            a.researchGroup = int(number("Research.Group", 0));
            a.consultant = int(number("RStaff[0].SType", 0));
            for (size_t y = 0; y < a.scrapPercent.size(); ++y)
                a.scrapPercent[y] =
                    int(number(("Upgrades[0].ScrapValueYear" + std::to_string(y + 1)).c_str(), a.scrapPercent[y]));
            a.generationPercent = {int(number("GenerationModifier.KidScorePercentage", 100)),
                                   int(number("GenerationModifier.ParentScorePercentage", 100)),
                                   int(number("GenerationModifier.GrandparentScorePercentage", 100))};
            a.expectedPrice = int(number("Info.ExpectedPrice", a.fare));
            a.weatherPercent = {100, int(number("WeatherModifier.HeatWavePercentage", 100)),
                                int(number("WeatherModifier.RainPercentage", 100)),
                                int(number("WeatherModifier.StormPercentage", 100)),
                                int(number("WeatherModifier.SnowPercentage", 100))};
            _all.push_back(std::move(a));
        }
    }
    return Error::success();
}

const Attractions::Attraction *Attractions::find(int id) const {
    for (const Attraction &a : _all)
        if (a.id == id)
            return &a;
    return nullptr;
}

void Attractions::setAvailable(int id, bool available) {
    for (Attraction &a : _all)
        if (a.id == id)
            a.available = available && a.price > 0;
}

void Attractions::markAvailable(int id) {
    for (Attraction &a : _all)
        if (a.id == id)
            a.available = a.price > 0;
}

std::vector<const Attractions::Attraction *> Attractions::onSale(Kind kind) const {
    std::vector<const Attraction *> out;
    for (const Attraction &a : _all)
        if (a.kind == kind && a.available)
            out.push_back(&a);
    return out;
}

} // namespace reluva::tpw
