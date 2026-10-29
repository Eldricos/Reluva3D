// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/race.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::age {

// What the player has done: each race's best place at each level and the
// Crash Course lessons passed. Kept as text in the game's save folder
// (Reluva's own file, not the game's .plr). See
// docs/engines/age-racing.md.
class PlayerRecord {
public:
    static constexpr const char* kFileName = "record.txt";

    // From `folder` (nothing yet is an empty record): kFileName, or a
    // driver's own file.
    Error load(const std::filesystem::path& folder, const std::string& file = kFileName);
    Error save() const;
    Error parse(const std::string& text);
    std::string text() const;

    // A race finished in `place` (1 first; a Blitz finished in time is 1);
    // the best is kept.
    // `time`, seconds, when it finished (the best is kept too).
    void raced(const std::string& city, RaceKind kind, int index, bool professional, int place, float time = -1);
    // The best time, -1 if never finished.
    float bestTime(const std::string& city, RaceKind kind, int index, bool professional) const;
    // The last race run (Quick Race starts it again).
    void setLastRace(const std::string& city, RaceKind kind, int index);
    bool lastRace(std::string& city, RaceKind& kind, int& index) const;
    // The best place, 0 if never finished.
    int bestPlace(const std::string& city, RaceKind kind, int index, bool professional) const;
    // How many of a city's races of a kind were finished at `place` or
    // better, at that level (professional results count for amateur too).
    int placedIn(const std::string& city, RaceKind kind, bool professional, int place) const;

    void passed(const std::string& city, int lesson);
    bool hasPassed(const std::string& city, int lesson) const;

private:
    std::filesystem::path path_;
    std::map<std::string, int> places_;   // "sf blitz 3 p" -> best place
    std::map<std::string, bool> lessons_; // "sf 4"
    std::map<std::string, float> times_;  // as places_, the best time
    std::string last_;                    // "sf blitz 3"
};

// The drivers (the main menu's SELECT DRIVER): each a name, a level and
// a record of their own. Kept in drivers.txt beside the records: a line
// a driver, its record's file, a or p, its name. The first, from before
// there were drivers, keeps record.txt.
struct DriverEntry {
    std::string name;
    bool professional = false;
    std::string file; // its record's, in the save folder
};
class DriverRoster {
public:
    static constexpr const char* kFileName = "drivers.txt";
    Error load(const std::filesystem::path& folder);
    Error save() const;
    const std::vector<DriverEntry>& drivers() const { return drivers_; }
    int current() const { return current_; }
    void setCurrent(int index);
    const DriverEntry& driver() const { return drivers_[size_t(current_)]; }
    // A name already taken (letters' case aside).
    bool has(const std::string& name) const;
    // A new driver, made current; its record starts empty.
    Error add(const std::string& name, bool professional);
    // Gone, with its record (not the last driver).
    Error remove(int index);
    const std::filesystem::path& folder() const { return folder_; }

private:
    std::filesystem::path folder_;
    std::vector<DriverEntry> drivers_;
    int current_ = 0;
};

// What unlocks a car or one of its paint jobs, as its picture says
// (jpg/<car>_lck<n>.jpg, and _p for a professional's).
struct UnlockRule {
    std::string car;
    int paintJob = -1; // -1: the car itself
    std::string city;
    RaceKind kind = RaceKind::Checkpoint; // CrashCourse: pass `lesson`
    int count = 0;                        // races to place in; 0 all of them
    int lesson = -1;
};
const std::vector<UnlockRule>& unlockRules();

// How many races of a kind a city has.
using RaceCount = std::function<int(const std::string& city, RaceKind kind)>;

// Whether the car (paintJob -1) or a paint job is open: amateurs by
// placing 1st-3rd in the races (or passing the lesson), professionals by
// finishing first.
bool unlocked(const PlayerRecord& record, const std::string& car, int paintJob, bool professional,
              const RaceCount& raceCount);

// What it takes, in words (as the pictures put it), for a menu.
std::string unlockText(const UnlockRule& rule, bool professional);

// The races open, by the .cinfo's UnlockGroup and MustPlace: the first
// group of `group` races, then each next group once every race before it
// is placed at `mustPlace` or better (Reluva's reading). Lessons open one
// after another, each once the one before is passed.
bool raceOpen(const PlayerRecord& record, const std::string& city, RaceKind kind, int index, bool professional,
              int group, int mustPlace);

} // namespace reluva::age
