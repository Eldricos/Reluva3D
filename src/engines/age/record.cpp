// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/record.h"

#include <algorithm>
#include <cstdio>
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

Error PlayerRecord::load(const std::filesystem::path& folder, const std::string& file) {
    places_.clear();
    lessons_.clear();
    times_.clear();
    last_.clear();
    path_ = folder.empty() ? std::filesystem::path() : folder / file;
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
    // Lines: "race <city> <kind> <index> <a|p> <place>", "time <city>
    // <kind> <index> <a|p> <seconds>", "lesson <city> <index>" and "last
    // <city> <kind> <index>"; '#' starts a comment.
    places_.clear();
    lessons_.clear();
    times_.clear();
    last_.clear();
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
        } else if (what == "time") {
            std::string city, kind, level;
            int index = 0;
            float seconds = -1;
            if (words >> city >> kind >> index >> level >> seconds && seconds > 0) {
                const std::string key = city + " " + kind + " " + std::to_string(index) + " " + level;
                auto it = times_.find(key);
                if (it == times_.end() || seconds < it->second)
                    times_[key] = seconds;
            }
        } else if (what == "last") {
            std::string city, kind;
            int index = 0;
            if (words >> city >> kind >> index)
                last_ = city + " " + kind + " " + std::to_string(index);
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
    for (const auto& [key, seconds] : times_) {
        char buf[32];
        std::snprintf(buf, sizeof buf, " %.2f", double(seconds));
        out += "time " + key + buf + "\n";
    }
    for (const auto& [key, done] : lessons_)
        if (done)
            out += "lesson " + key + "\n";
    if (!last_.empty())
        out += "last " + last_ + "\n";
    return out;
}

void PlayerRecord::raced(const std::string& city, RaceKind kind, int index, bool professional, int place, float time) {
    if (time > 0) {
        const std::string key = raceKey(city, kind, index, professional);
        auto it = times_.find(key);
        if (it == times_.end() || time < it->second)
            times_[key] = time;
    }
    if (place <= 0)
        return;
    int& best = places_[raceKey(city, kind, index, professional)];
    if (best == 0 || place < best)
        best = place;
}

float PlayerRecord::bestTime(const std::string& city, RaceKind kind, int index, bool professional) const {
    auto it = times_.find(raceKey(city, kind, index, professional));
    return it == times_.end() ? -1 : it->second;
}

void PlayerRecord::setLastRace(const std::string& city, RaceKind kind, int index) {
    last_ = city + " " + kindWord(kind) + " " + std::to_string(index);
}

bool PlayerRecord::lastRace(std::string& city, RaceKind& kind, int& index) const {
    std::istringstream words(last_);
    std::string k;
    if (!(words >> city >> k >> index))
        return false;
    for (RaceKind r : {RaceKind::Blitz, RaceKind::Circuit, RaceKind::Checkpoint, RaceKind::CrashCourse})
        if (k == kindWord(r)) {
            kind = r;
            return true;
        }
    return false;
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

Error DriverRoster::load(const std::filesystem::path& folder) {
    folder_ = folder;
    drivers_.clear();
    current_ = 0;
    std::ifstream in(folder / kFileName, std::ios::binary);
    for (std::string line; in && std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.rfind("current ", 0) == 0) {
            current_ = std::atoi(line.c_str() + 8);
            continue;
        }
        // <file> TAB <a|p> TAB <name>
        const size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos || line[0] == '#')
            continue;
        drivers_.push_back({line.substr(b + 1), line.substr(a + 1, b - a - 1) == "p", line.substr(0, a)});
    }
    if (drivers_.empty())
        drivers_.push_back({"Player", false, PlayerRecord::kFileName}); // before there were drivers
    current_ = std::clamp(current_, 0, int(drivers_.size()) - 1);
    return Error::success();
}

Error DriverRoster::save() const {
    if (folder_.empty())
        return Error::fail("No save folder");
    std::error_code ec;
    std::filesystem::create_directories(folder_, ec);
    std::ofstream out(folder_ / kFileName, std::ios::binary);
    out << "# Reluva's Midtown Madness 2 drivers: record file, a or p, name\n";
    for (const DriverEntry& d : drivers_)
        out << d.file << '\t' << (d.professional ? 'p' : 'a') << '\t' << d.name << '\n';
    out << "current " << current_ << '\n';
    return out ? Error::success() : Error::fail("Can't write " + (folder_ / kFileName).string());
}

void DriverRoster::setCurrent(int index) {
    if (!drivers_.empty())
        current_ = std::clamp(index, 0, int(drivers_.size()) - 1);
}

bool DriverRoster::has(const std::string& name) const {
    auto lower = [](std::string s) {
        for (char& c : s)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    for (const DriverEntry& d : drivers_)
        if (lower(d.name) == lower(name))
            return true;
    return false;
}

Error DriverRoster::add(const std::string& name, bool professional) {
    if (name.empty() || has(name))
        return Error::fail("That driver name already exists");
    // record-<n>.txt: the first number not taken.
    std::string file;
    for (int n = 1;; ++n) {
        file = "record-" + std::to_string(n) + ".txt";
        bool taken = false;
        for (const DriverEntry& d : drivers_)
            taken = taken || d.file == file;
        std::error_code ec;
        if (!taken && !std::filesystem::exists(folder_ / file, ec))
            break;
    }
    drivers_.push_back({name, professional, file});
    current_ = int(drivers_.size()) - 1;
    return Error::success();
}

Error DriverRoster::remove(int index) {
    if (drivers_.size() <= 1)
        return Error::fail("You cannot delete your only driver");
    if (index < 0 || size_t(index) >= drivers_.size())
        return Error::fail("No such driver");
    std::error_code ec;
    if (!folder_.empty())
        std::filesystem::remove(folder_ / drivers_[size_t(index)].file, ec);
    drivers_.erase(drivers_.begin() + index);
    current_ = std::clamp(current_ > index ? current_ - 1 : current_, 0, int(drivers_.size()) - 1);
    return Error::success();
}

} // namespace reluva::age
