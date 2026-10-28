// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/record.h"

#include <fstream>
#include <sstream>

namespace reluva::age {

namespace {

const char* kindWord(RaceKind k) {
    switch (k) {
    case RaceKind::Blitz: return "blitz";
    case RaceKind::Circuit: return "circuit";
    case RaceKind::Checkpoint: return "checkpoint";
    case RaceKind::CrashCourse: return "crash";
    }
    return "?";
}

std::string raceKey(const std::string& city, RaceKind kind, int index, bool professional) {
    return city + " " + kindWord(kind) + " " + std::to_string(index) + (professional ? " p" : " a");
}

std::string cityName(const std::string& city) { return city == "london" ? "London" : "San Francisco"; }

const char* kindPlural(RaceKind k) {
    switch (k) {
    case RaceKind::Blitz: return "Blitz Races";
    case RaceKind::Circuit: return "Circuit Races";
    case RaceKind::Checkpoint: return "Checkpoint Races";
    case RaceKind::CrashCourse: return "Crash Course";
    }
    return "";
}

} // namespace

Error PlayerRecord::load(const std::filesystem::path& folder) {
    places_.clear();
    lessons_.clear();
    path_ = folder.empty() ? std::filesystem::path() : folder / kFileName;
    if (path_.empty())
        return Error::success();
    std::ifstream in(path_, std::ios::binary);
    if (!in)
        return Error::success(); // none yet
    std::stringstream text;
    text << in.rdbuf();
    return parse(text.str());
}

Error PlayerRecord::save() const {
    if (path_.empty())
        return Error::fail("No save folder");
    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    std::ofstream out(path_, std::ios::binary);
    if (!out)
        return Error::fail("Can't write " + path_.string());
    out << text();
    return out ? Error::success() : Error::fail("Can't write " + path_.string());
}

Error PlayerRecord::parse(const std::string& text) {
    // Lines: "race <city> <kind> <index> <a|p> <place>" and
    // "lesson <city> <index>"; '#' starts a comment.
    places_.clear();
    lessons_.clear();
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream words(line);
        std::string what;
        if (!(words >> what) || what[0] == '#')
            continue;
        if (what == "race") {
            std::string city, kind, level;
            int index = 0, place = 0;
            if (words >> city >> kind >> index >> level >> place && place > 0) {
                int& best = places_[city + " " + kind + " " + std::to_string(index) + " " + level];
                if (best == 0 || place < best)
                    best = place;
            }
        } else if (what == "lesson") {
            std::string city;
            int index = 0;
            if (words >> city >> index)
                lessons_[city + " " + std::to_string(index)] = true;
        }
    }
    return Error::success();
}

std::string PlayerRecord::text() const {
    std::string out = "# Reluva's record of a Midtown Madness 2 player\n";
    for (const auto& [key, place] : places_)
        out += "race " + key + " " + std::to_string(place) + "\n";
    for (const auto& [key, done] : lessons_)
        if (done)
            out += "lesson " + key + "\n";
    return out;
}

void PlayerRecord::raced(const std::string& city, RaceKind kind, int index, bool professional, int place) {
    if (place <= 0)
        return;
    int& best = places_[raceKey(city, kind, index, professional)];
    if (best == 0 || place < best)
        best = place;
}

int PlayerRecord::bestPlace(const std::string& city, RaceKind kind, int index, bool professional) const {
    auto it = places_.find(raceKey(city, kind, index, professional));
    return it == places_.end() ? 0 : it->second;
}

int PlayerRecord::placedIn(const std::string& city, RaceKind kind, bool professional, int place) const {
    int n = 0;
    for (int i = 0; i < 64; ++i) {
        int best = bestPlace(city, kind, i, true);
        if (!professional) {
            const int amateur = bestPlace(city, kind, i, false);
            if (amateur > 0 && (best == 0 || amateur < best))
                best = amateur;
        }
        n += best > 0 && best <= place;
    }
    return n;
}

void PlayerRecord::passed(const std::string& city, int lesson) { lessons_[city + " " + std::to_string(lesson)] = true; }

bool PlayerRecord::hasPassed(const std::string& city, int lesson) const {
    return lessons_.count(city + " " + std::to_string(lesson)) > 0;
}

const std::vector<UnlockRule>& unlockRules() {
    // From the pictures: "To unlock this vehicle, place 1st, 2nd, or 3rd
    // in six San Francisco Checkpoint Races" (vpauditt_lck), "... finish
    // first in ..." (_lck_p), "... pass London Crash Course Midterm Exam
    // Two" (vpcab_lck1)... The midterms are lessons 3, 7 and 11, the
    // final (completing the course) 12.
    using K = RaceKind;
    static const std::vector<UnlockRule> rules{
        {"vp4x4", -1, "sf", K::CrashCourse, 0, 12},
        {"vpauditt", -1, "sf", K::Checkpoint, 6, -1},
        {"vpauditt", 4, "sf", K::Checkpoint, 0, -1},
        {"vpbullet", 4, "sf", K::CrashCourse, 0, 11},
        {"vpcab", 1, "london", K::CrashCourse, 0, 7},
        {"vpcoop2k", -1, "london", K::Blitz, 5, -1},
        {"vpcoop2k", 4, "london", K::Blitz, 0, -1},
        {"vpcoop", 4, "london", K::CrashCourse, 0, 11},
        {"vpdb7", -1, "london", K::CrashCourse, 0, 12},
        {"vpddbus", 3, "sf", K::CrashCourse, 0, 7},
        {"vpdune", -1, "sf", K::Circuit, 5, -1},
        {"vpdune", 4, "sf", K::Circuit, 0, -1},
        {"vppanozgt", -1, "london", K::Checkpoint, 6, -1},
        {"vppanozgt", 4, "london", K::Checkpoint, 0, -1},
        {"vpsemi", -1, "sf", K::Blitz, 5, -1},
        {"vpsemi", 4, "sf", K::Blitz, 0, -1},
        {"vpvwcup", -1, "london", K::Circuit, 5, -1},
        {"vpvwcup", 4, "london", K::Circuit, 0, -1},
        {"vpvwcup", 5, "sf", K::CrashCourse, 0, 3},
        {"vpvwcup", 6, "london", K::CrashCourse, 0, 3},
    };
    return rules;
}

bool unlocked(const PlayerRecord& record, const std::string& car, int paintJob, bool professional,
              const RaceCount& raceCount) {
    for (const UnlockRule& r : unlockRules()) {
        if (r.car != car || r.paintJob != paintJob)
            continue;
        if (r.kind == RaceKind::CrashCourse)
            return record.hasPassed(r.city, r.lesson);
        const int need = r.count > 0 ? r.count : raceCount ? raceCount(r.city, r.kind) : 0;
        return need > 0 && record.placedIn(r.city, r.kind, professional, professional ? 1 : 3) >= need;
    }
    return true; // no rule: open from the start
}

std::string unlockText(const UnlockRule& rule, bool professional) {
    const std::string what = rule.paintJob < 0 ? "this vehicle" : "this custom paint job";
    if (rule.kind == RaceKind::CrashCourse) {
        static const char* const kExams[] = {"Midterm Exam One", "Midterm Exam Two", "Midterm Exam Three"};
        if (rule.lesson == 12)
            return "To unlock " + what + ", successfully complete the " + cityName(rule.city) + " Crash Course";
        const int exam = rule.lesson / 4; // 3, 7, 11
        return "To unlock " + what + ", pass " + cityName(rule.city) + " Crash Course " + kExams[exam < 3 ? exam : 2];
    }
    static const char* const kNumbers[] = {"", "one", "two", "three", "four", "five", "six", "seven", "eight"};
    const std::string how = professional ? "finish first in " : "place 1st, 2nd, or 3rd in ";
    const std::string many = rule.count > 0 && rule.count < 9 ? kNumbers[rule.count] : "all";
    return "To unlock " + what + ", " + how + many + " " + cityName(rule.city) + " " + kindPlural(rule.kind);
}

bool raceOpen(const PlayerRecord& record, const std::string& city, RaceKind kind, int index, bool professional,
              int group, int mustPlace) {
    if (kind == RaceKind::CrashCourse)
        return index == 0 || record.hasPassed(city, index - 1);
    if (group <= 0 || index < group)
        return true;
    const int before = index / group * group; // every race before this one's group
    for (int i = 0; i < before; ++i) {
        int best = record.bestPlace(city, kind, i, professional);
        if (!professional) {
            const int p = record.bestPlace(city, kind, i, true);
            if (p > 0 && (best == 0 || p < best))
                best = p;
        }
        if (best == 0 || best > mustPlace)
            return false;
    }
    return true;
}

} // namespace reluva::age
