// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/age/formats/race.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * What the player has done: each race's best place at each level and the
 * Crash Course lessons passed. Kept as text in the game's save folder
 * (Reluva's own file, not the game's .plr). See
 * docs/engines/age/games/mm2/systems/record-and-unlocking.md.
 */
class PlayerRecord {
public:
    static constexpr const char *kFileName = "record.txt";

    /**
     * From `folder` (nothing yet is an empty record): kFileName, or a
     * driver's own file.
     */
    Error load(const std::filesystem::path &folder, const std::string &file = kFileName);
    /** Writes the record to the file load() read. Fails without a save folder, or if it can't write. */
    Error save() const;
    /** The record from its file's text, in place of what it held; lines it can't read are skipped. */
    Error parse(const std::string &text);
    /** The record as its file holds it. */
    std::string text() const;

    /**
     * A race finished in `place` (1 first; a Blitz finished in time is 1);
     * the best is kept.
     * `time`, seconds, when it finished (the best is kept too).
     */
    void raced(const std::string &city, RaceKind kind, int index, bool professional, int place, float time = -1);
    /** The best time, -1 if never finished. */
    float bestTime(const std::string &city, RaceKind kind, int index, bool professional) const;
    /** The last race run (Quick Race starts it again); lastRace() is false if there's none. */
    ///@{
    void setLastRace(const std::string &city, RaceKind kind, int index);
    bool lastRace(std::string &city, RaceKind &kind, int &index) const;
    ///@}
    /** The best place, 0 if never finished. */
    int bestPlace(const std::string &city, RaceKind kind, int index, bool professional) const;
    /**
     * How many of a city's races of a kind were finished at `place` or
     * better, at that level (professional results count for amateur too).
     */
    int placedIn(const std::string &city, RaceKind kind, bool professional, int place) const;

    /** A city's Crash Course lesson passed (lessons count from 0), and whether one has been. */
    ///@{
    void passed(const std::string &city, int lesson);
    bool hasPassed(const std::string &city, int lesson) const;
    ///@}

private:
    std::filesystem::path _path;
    std::map<std::string, int> _places;   // "sf blitz 3 p" -> best place
    std::map<std::string, bool> _lessons; // "sf 4"
    std::map<std::string, float> _times;  // as _places, the best time
    std::string _last;                    // "sf blitz 3"
};

/** A driver: a name, a level, and the file of their record. */
struct DriverEntry {
    std::string name;
    bool professional = false;
    std::string file; // its record's, in the save folder
};
/**
 * The drivers (the main menu's SELECT DRIVER): each a name, a level and
 * a record of their own. Kept in drivers.txt beside the records: a line
 * a driver, its record's file, a or p, its name. The first, from before
 * there were drivers, keeps record.txt.
 */
class DriverRoster {
public:
    static constexpr const char *kFileName = "drivers.txt";
    /**
     * Reads drivers.txt from the save folder; without it (or any driver in
     * it), one driver, "Player", keeping record.txt.
     */
    Error load(const std::filesystem::path &folder);
    /** Writes drivers.txt. Fails without a save folder, or if it can't write. */
    Error save() const;
    /** Every driver, in order. */
    const std::vector<DriverEntry> &drivers() const { return _drivers; }
    /** The driver playing: its index, choosing another (kept within the list), and the driver. */
    ///@{
    int current() const { return _current; }
    void setCurrent(int index);
    const DriverEntry &driver() const { return _drivers[size_t(_current)]; }
    ///@}
    /** A name already taken (letters' case aside). */
    bool has(const std::string &name) const;
    /**
     * A new driver, made current; its record starts empty, in the first
     * record-`<n>`.txt free. Fails on an empty name or one taken.
     */
    Error add(const std::string &name, bool professional);
    /** Gone, with its record's file. Fails for the only driver, or no such one. */
    Error remove(int index);
    /** The save folder load() was given. */
    const std::filesystem::path &folder() const { return _folder; }

private:
    std::filesystem::path _folder;
    std::vector<DriverEntry> _drivers;
    int _current = 0;
};

/**
 * What unlocks a car or one of its paint jobs, as its picture says
 * (`jpg/<car>_lck<n>.jpg`, and _p for a professional's).
 */
struct UnlockRule {
    std::string car;
    int paintJob = -1; // -1: the car itself
    std::string city;
    RaceKind kind = RaceKind::kCheckpoint; // CrashCourse: pass `lesson`
    int count = 0;                         // races to place in; 0 all of them
    int lesson = -1;
};
/** Every car and paint job that has to be unlocked, and how. */
const std::vector<UnlockRule> &unlockRules();

/** How many races of a kind a city has. */
using RaceCount = std::function<int(const std::string &city, RaceKind kind)>;

/**
 * Whether the car (paintJob -1) or a paint job is open: amateurs by
 * placing 1st-3rd in the races (or passing the lesson), professionals by
 * finishing first. One with no rule is open from the start.
 * @param raceCount How many races a city has of a kind, for a rule that
 *        needs all of them.
 */
bool unlocked(const PlayerRecord &record, const std::string &car, int paintJob, bool professional,
              const RaceCount &raceCount);

/**
 * `race/<city>/<city>_rewards.csv`: what a city's races open and the
 * results screen's words for it ("Good Job! You've unlocked the Audi
 * TT!"): the race kind (blitz, circuit, race for Checkpoint, crash), half
 * or all of them (a lesson's number for crash), the car and its paint job
 * (0: the car itself).
 */
struct Reward {
    std::string kind;
    std::string which;
    std::string car;
    int paintJob = 0;
    std::string message;
};
/** A rewards file's rows, after its heading; rows of fewer than five fields are left out. */
std::vector<Reward> parseRewards(const std::string &text);

/** What it takes, in words (as the pictures put it), for a menu. */
std::string unlockText(const UnlockRule &rule, bool professional);

/**
 * The races open, by the .cinfo's UnlockGroup and MustPlace: the first
 * group of `group` races, then each next group once every race before it
 * is placed at `mustPlace` or better (Reluva's reading). Crash Course's
 * lessons are open in any order but the Final (12), open once the three
 * Midterms (3, 7 and 11) are passed; Cruise's are always open.
 */
bool raceOpen(const PlayerRecord &record, const std::string &city, RaceKind kind, int index, bool professional,
              int group, int mustPlace);

} // namespace reluva::age
