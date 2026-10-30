// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/race.h"

#include "engines/age/aimap.h"
#include "engines/age/lang.h"

#include <cmath>
#include <cstdlib>

namespace reluva::age {

namespace {

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos)
            eol = text.size();
        std::string line = text.substr(at, eol - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        out.push_back(line);
        at = eol + 1;
    }
    return out;
}

std::vector<std::string> split(const std::string& line, char sep) {
    std::vector<std::string> out;
    size_t at = 0;
    while (true) {
        const size_t s = line.find(sep, at);
        out.push_back(line.substr(at, s == std::string::npos ? std::string::npos : s - at));
        if (s == std::string::npos)
            return out;
        at = s + 1;
    }
}

float number(const std::vector<std::string>& f, size_t i) {
    return i < f.size() ? std::strtof(f[i].c_str(), nullptr) : 0;
}

std::string iniValue(const std::string& text, const std::string& key) {
    for (const std::string& line : lines(text))
        if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=')
            return line.substr(key.size() + 1);
    return {};
}

// The files' stem for each kind (checkpoint races are "race"), and the
// .cinfo keys.
const char* fileStem(RaceKind k) {
    switch (k) {
    case RaceKind::Blitz: return "blitz";
    case RaceKind::Circuit: return "circuit";
    case RaceKind::Checkpoint: return "race";
    case RaceKind::CrashCourse: return "crash";
    case RaceKind::Cruise:
        return "roam";
    }
    return "race";
}
const char* infoKey(RaceKind k) {
    switch (k) {
    case RaceKind::Blitz: return "Blitz";
    case RaceKind::Circuit: return "Circuit";
    case RaceKind::Checkpoint: return "Checkpoint";
    case RaceKind::CrashCourse: return "Crash Course";
    case RaceKind::Cruise:
        return "Cruise";
    }
    return "Checkpoint";
}

std::string textOf(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

} // namespace

const char* raceKindName(RaceKind kind) { return infoKey(kind); }

std::string raceStem(RaceKind kind, int index) { return fileStem(kind) + std::to_string(index); }

std::string raceAiMapName(const std::string& city, RaceKind kind, int index, bool professional) {
    // A cruise's police patrol as roam.aimap says (the stunt has none).
    if (kind == RaceKind::Cruise)
        return "race/" + city + "/" +
               (index == kStuntRace       ? "accel0"
                : index == kPrototypeRace ? "stunt0"
                                          : "roam") +
               (professional ? ".aimap_p" : ".aimap");
    return "race/" + city + "/" + fileStem(kind) + std::to_string(index) + (professional ? ".aimap_p" : ".aimap");
}

RaceUnlocking raceUnlocking(const SearchSet& files, const std::string& city) {
    RaceUnlocking out;
    std::vector<uint8_t> data;
    if (files.read("tune/" + city + ".cinfo", data))
        return out;
    const std::string text = textOf(data);
    if (const std::string v = iniValue(text, "MustPlace"); !v.empty())
        out.mustPlace = std::atoi(v.c_str());
    if (const std::string v = iniValue(text, "UnlockGroup"); !v.empty())
        out.group = std::atoi(v.c_str());
    return out;
}

std::vector<std::string> raceNames(const SearchSet& files, const std::string& city, RaceKind kind) {
    std::vector<uint8_t> data;
    if (kind == RaceKind::CrashCourse) {
        // As many as there are crash<n>data.csv; named in mmlang.dll.
        GameStrings strings;
        strings.load(files);
        std::vector<std::string> names;
        const uint32_t first = city == "london" ? strings::kLondonLessons : strings::kSfLessons;
        for (uint32_t i = 0; files.has("race/" + city + "/crash" + std::to_string(i) + "data.csv"); ++i)
            names.push_back(strings.get(i < strings::kLessonCount ? first + i : 0, "Lesson " + std::to_string(i + 1)));
        return names;
    }
    if (kind == RaceKind::Cruise) {
        // mmlang.dll's words for them.
        GameStrings strings;
        strings.load(files);
        std::vector<std::string> names{strings.get(588, "Cruise")};
        if (files.has("race/" + city + "/accel0waypoints.csv")) {
            names.push_back(strings.get(199, "Stunt Mode: Acceleration"));
            // The mode names' "Stunt Prototype": stunt0's start, traffic and
            // closed roads (its opponent's path, opp-c0.2, isn't shipped).
            if (files.has("race/" + city + "/stunt0waypoints.csv"))
                names.push_back(strings.get(526, "Stunt Prototype"));
        }
        return names;
    }
    if (files.read("tune/" + city + ".cinfo", data))
        return {};
    const std::string text = textOf(data);
    const int count = std::atoi(iniValue(text, std::string(infoKey(kind)) + "Count").c_str());
    std::vector<std::string> names = split(iniValue(text, std::string(infoKey(kind)) + "Names"), '|');
    names.resize(size_t(std::max(count, 0)));
    for (size_t i = 0; i < names.size(); ++i) {
        while (!names[i].empty() && names[i].back() == ' ')
            names[i].pop_back();
        if (names[i].empty())
            names[i] = std::string(infoKey(kind)) + " " + std::to_string(i + 1);
    }
    return names;
}

Error parseRaceSettings(const std::string& csv, int index, std::array<RaceSettings, 2>& out) {
    // A heading line, then a row a race: a description, then the amateur
    // and the professional columns (CarType, TimeofDay, Weather,
    // Opponents, Cops, Ambient, Peds, NumLaps, TimeLimit, Difficulty).
    const auto rows = lines(csv);
    if (size_t(index) + 1 >= rows.size())
        return Error::fail("no row for race " + std::to_string(index));
    const auto f = split(rows[size_t(index) + 1], ',');
    for (size_t d = 0; d < 2; ++d) {
        const size_t b = 1 + d * 10;
        RaceSettings& s = out[d];
        s.carType = int(number(f, b));
        s.timeOfDay = int(number(f, b + 1));
        s.weather = int(number(f, b + 2));
        s.opponents = int(number(f, b + 3));
        s.cops = int(number(f, b + 4));
        s.ambient = number(f, b + 5);
        s.peds = number(f, b + 6);
        s.laps = int(number(f, b + 7));
        s.timeLimit = number(f, b + 8);
        s.difficulty = int(number(f, b + 9));
    }
    return Error::success();
}

Error parseWaypoints(const std::string& csv, std::vector<Waypoint>& out, bool alwaysRadius) {
    // x, y, z, a (heading), then the radius in blitzes and checkpoint
    // races (a "poly count" in circuits, not a radius), and columns that
    // are 0 throughout.
    out.clear();
    const auto rows = lines(csv);
    const bool radius = alwaysRadius || (!rows.empty() && rows[0].find("radius") != std::string::npos);
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].empty())
            continue;
        const auto f = split(rows[i], ',');
        if (f.size() < 4)
            return Error::fail("line " + std::to_string(i + 1) + ": too few values");
        out.push_back({{number(f, 0), number(f, 1), number(f, 2)}, number(f, 3), radius ? number(f, 4) : 0});
    }
    if (out.size() < 2)
        return Error::fail("fewer than two waypoints");
    return Error::success();
}

Error parseOpponentPath(const std::string& csv, RaceDefinition::Opponent& out) {
    // x, y, z, then (on the first line only) the start's heading in the
    // "brake" column; the other columns are 0 throughout.
    out = RaceDefinition::Opponent{};
    const auto rows = lines(csv);
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].empty())
            continue;
        const auto f = split(rows[i], ',');
        const Vec3 p{number(f, 0), number(f, 1), number(f, 2)};
        if (i == 1) {
            out.start = p;
            out.heading = number(f, 3);
        } else {
            out.path.push_back(p);
        }
    }
    if (out.path.empty())
        return Error::fail("no path");
    // A first point on the far side of the start from the second isn't
    // driven to; the opponent faces the first point it drives to.
    if (out.path.size() >= 2) {
        Vec3 a = out.path[0] - out.start, b = out.path[1] - out.start;
        a[1] = b[1] = 0;
        if (dot(a, b) < 0) {
            out.path.erase(out.path.begin());
            out.skippedFirst = true;
        }
    }
    Vec3 to = out.path[0] - out.start;
    to[1] = 0;
    out.startYaw = length(to) > 0.1f ? std::atan2(-to[0], -to[2]) : 0;
    return Error::success();
}

Error parseLessonSettings(const std::string& csv, LessonSettings& out) {
    // A heading, then one row: Filename, Event, Checkpoints, TimeLimit,
    // AmbDensity, then five "extra" columns.
    out = LessonSettings{};
    const auto rows = lines(csv);
    if (rows.size() < 2 || rows[1].empty())
        return Error::fail("no lesson row");
    const auto f = split(rows[1], ',');
    out.file = f[0];
    while (!out.file.empty() && out.file.back() == ' ')
        out.file.pop_back();
    if (out.file.empty())
        return Error::fail("no lesson file");
    out.event = LessonEvent(int(number(f, 1)));
    out.checkpoints = int(number(f, 2));
    out.timeLimit = number(f, 3);
    out.ambient = number(f, 4);
    for (size_t i = 0; i < out.extra.size(); ++i)
        out.extra[i] = number(f, 5 + i);
    return Error::success();
}

Error loadRace(const SearchSet& files, const std::string& city, RaceKind kind, int index, RaceDefinition& out) {
    out = RaceDefinition{};
    out.kind = kind;
    out.index = index;
    const auto names = raceNames(files, city, kind);
    out.name = size_t(index) < names.size() ? names[size_t(index)] : "";
    const std::string dir = "race/" + city + "/";
    std::vector<uint8_t> data;
    if (kind == RaceKind::Cruise) {
        // No table: Reluva's own conditions to start from (the race menu
        // changes them). The cruise starts where the race page puts it;
        // the stunt along accel0's points, the last the end of the pier.
        // The cruise's traffic as its roam.aimap's [Density] says (London's
        // 0.1); San Francisco's gives none, so the same.
        float traffic = 0.1f;
        AiMapSettings roam;
        if (!files.read(dir + (index == kPrototypeRace ? "stunt0.aimap" : "roam.aimap"), data)) {
            parseAiMap(textOf(data), roam);
            if (roam.density >= 0)
                traffic = roam.density;
        }
        for (RaceSettings& s : out.settings) {
            s.ambient = traffic;
            s.peds = 0.5f;
            s.cops = index == kCruiseRace ? 4 : 0;
            s.laps = 1;
        }
        if (index == kCruiseRace)
            return Error::success();
        const std::string points = dir + (index == kStuntRace ? "accel0waypoints.csv" : "stunt0waypoints.csv");
        if (Error err = files.read(points, data))
            return err;
        // The prototype's has one point, its start: enough.
        if (Error err = parseWaypoints(textOf(data), out.waypoints, true);
            err && !(index == kPrototypeRace && out.waypoints.size() == 1))
            return Error::fail(points + ": " + err.message);
        return Error::success();
    }
    const std::string table = dir + "mm" + fileStem(kind) + "data.csv";
    if (Error err = files.read(table, data))
        return err;
    if (Error err = parseRaceSettings(textOf(data), index, out.settings))
        return Error::fail(table + ": " + err.message);
    const std::string stem = dir + fileStem(kind) + std::to_string(index);
    if (kind == RaceKind::CrashCourse) {
        // The lesson's own row names its checkpoints' file; its aimap its
        // opponents' paths (the car to follow...), the same at both levels.
        if (Error err = files.read(stem + "data.csv", data))
            return err;
        if (Error err = parseLessonSettings(textOf(data), out.lesson))
            return Error::fail(stem + "data.csv: " + err.message);
        const std::string checkpoints = dir + out.lesson.file + ".csv";
        if (Error err = files.read(checkpoints, data))
            return err;
        if (Error err = parseWaypoints(textOf(data), out.waypoints, true))
            return Error::fail(checkpoints + ": " + err.message);
        AiMapSettings ai;
        if (!files.read(stem + ".aimap", data))
            parseAiMap(textOf(data), ai);
        for (const AiMapSettings::Opponent& opponent : ai.opponents) {
            RaceDefinition::Opponent o;
            if (files.read(dir + opponent.path, data) || parseOpponentPath(textOf(data), o))
                continue;
            out.opponents[0].push_back(o);
            out.opponents[1].push_back(std::move(o));
        }
        return Error::success();
    }
    if (Error err = files.read(stem + "waypoints.csv", data))
        return err;
    if (Error err = parseWaypoints(textOf(data), out.waypoints))
        return Error::fail(stem + "waypoints.csv: " + err.message);
    for (int d = 0; d < 2; ++d)
        for (int i = 0; i < 16; ++i) {
            const std::string name = stem + "-" + (d ? "p" : "a") + "-" + std::to_string(i) + ".opp";
            RaceDefinition::Opponent o;
            if (files.read(name, data) || parseOpponentPath(textOf(data), o))
                break;
            out.opponents[size_t(d)].push_back(std::move(o));
        }
    return Error::success();
}

} // namespace reluva::age
