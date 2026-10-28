// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/race.h"

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
    return k == RaceKind::Blitz ? "blitz" : k == RaceKind::Circuit ? "circuit" : "race";
}
const char* infoKey(RaceKind k) {
    return k == RaceKind::Blitz ? "Blitz" : k == RaceKind::Circuit ? "Circuit" : "Checkpoint";
}

std::string textOf(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

} // namespace

const char* raceKindName(RaceKind kind) { return infoKey(kind); }

std::string raceStem(RaceKind kind, int index) { return fileStem(kind) + std::to_string(index); }

std::string raceAiMapName(const std::string& city, RaceKind kind, int index, bool professional) {
    return "race/" + city + "/" + fileStem(kind) + std::to_string(index) + (professional ? ".aimap_p" : ".aimap");
}

std::vector<std::string> raceNames(const SearchSet& files, const std::string& city, RaceKind kind) {
    std::vector<uint8_t> data;
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

Error parseWaypoints(const std::string& csv, std::vector<Waypoint>& out) {
    // x, y, z, a (heading), then the radius in blitzes and checkpoint
    // races (a "poly count" in circuits, not a radius), and columns that
    // are 0 throughout.
    out.clear();
    const auto rows = lines(csv);
    const bool radius = !rows.empty() && rows[0].find("radius") != std::string::npos;
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

Error loadRace(const SearchSet& files, const std::string& city, RaceKind kind, int index, RaceDefinition& out) {
    out = RaceDefinition{};
    out.kind = kind;
    out.index = index;
    const auto names = raceNames(files, city, kind);
    out.name = size_t(index) < names.size() ? names[size_t(index)] : "";
    const std::string dir = "race/" + city + "/";
    std::vector<uint8_t> data;
    const std::string table = dir + "mm" + fileStem(kind) + "data.csv";
    if (Error err = files.read(table, data))
        return err;
    if (Error err = parseRaceSettings(textOf(data), index, out.settings))
        return Error::fail(table + ": " + err.message);
    const std::string stem = dir + fileStem(kind) + std::to_string(index);
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
