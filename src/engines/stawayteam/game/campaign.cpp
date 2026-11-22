// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/campaign.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace reluva::stawayteam {

namespace {

// The first line of a save, to know one: its format may change.
const char *const kMagic = "Reluva Star Trek: Away Team save 1";

} // namespace

int missionScore(const MissionObjectives &mission, const std::vector<Objective> &objectives) {
    int score = mission.baseScore;
    for (const Objective &o : objectives)
        if (!o.required && o.status == ObjectiveStatus::kComplete)
            score += o.points;
    return score;
}

std::vector<std::string> foundByScore(const std::map<std::string, ItemDefinition> &items, const Campaign &campaign) {
    std::vector<std::string> found;
    for (const auto &[name, item] : items) {
        if (item.startsDiscovered || item.pointsToDiscover <= 0 || campaign.score < item.pointsToDiscover)
            continue;
        const auto known = [&](const std::vector<std::string> &list) {
            return std::find(list.begin(), list.end(), name) != list.end();
        };
        if (!known(campaign.discovered) && !known(campaign.toDescribe))
            found.push_back(name);
    }
    return found;
}

Error writeSaveGame(const std::filesystem::path &path, const SaveGame &save) {
    // A line a value, "key=value"; a list's values a line each.
    std::ostringstream out;
    out << kMagic << "\n";
    out << "name=" << save.name << "\n";
    out << "saved=" << save.savedAt << "\n";
    out << "mission name=" << save.missionName << "\n";
    out << "mission=" << save.campaign.mission << "\n";
    out << "score=" << save.campaign.score << "\n";
    out << "time=" << save.campaign.time << "\n";
    out << "difficulty=" << save.campaign.difficulty << "\n";
    for (const std::string &item : save.campaign.discovered)
        out << "discovered=" << item << "\n";
    for (const std::string &item : save.campaign.toDescribe)
        out << "to describe=" << item << "\n";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (!file)
        return Error::fail("can't write " + path.string());
    file << out.str();
    return file ? Error::success() : Error::fail("can't write " + path.string());
}

Error readSaveGame(const std::filesystem::path &path, SaveGame &out) {
    std::ifstream file(path, std::ios::binary);
    std::string line;
    if (!file || !std::getline(file, line) || line != kMagic)
        return Error::fail(path.string() + " isn't a save");
    out = SaveGame{};
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "name")
            out.name = value;
        else if (key == "saved")
            out.savedAt = std::strtoll(value.c_str(), nullptr, 10);
        else if (key == "mission name")
            out.missionName = value;
        else if (key == "mission")
            out.campaign.mission = std::atoi(value.c_str());
        else if (key == "score")
            out.campaign.score = std::atoi(value.c_str());
        else if (key == "time")
            out.campaign.time = float(std::atof(value.c_str()));
        else if (key == "difficulty")
            out.campaign.difficulty = std::atoi(value.c_str());
        else if (key == "discovered")
            out.campaign.discovered.push_back(value);
        else if (key == "to describe")
            out.campaign.toDescribe.push_back(value);
    }
    if (out.campaign.mission < 1)
        return Error::fail(path.string() + " has no mission");
    return Error::success();
}

std::vector<std::pair<std::filesystem::path, SaveGame>> listSaveGames(const std::filesystem::path &folder) {
    std::vector<std::pair<std::filesystem::path, SaveGame>> saves;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(folder, ec)) {
        if (entry.path().extension() != ".sav")
            continue;
        SaveGame save;
        if (!readSaveGame(entry.path(), save))
            saves.emplace_back(entry.path(), std::move(save));
    }
    std::stable_sort(saves.begin(), saves.end(),
                     [](const auto &a, const auto &b) { return a.second.savedAt > b.second.savedAt; });
    return saves;
}

} // namespace reluva::stawayteam
