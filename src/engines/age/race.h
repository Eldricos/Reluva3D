// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "engines/age/vecmath.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::age {

// A city's races, from tune/<city>.cinfo and race/<city>/: their names,
// settings (mm<kind>data.csv), checkpoints (<file><n>waypoints.csv) and
// the opponents' paths (<file><n>-<a|p>-<i>.opp). See
// docs/formats/age/race.md.

enum class RaceKind { Blitz, Circuit, Checkpoint };
const char* raceKindName(RaceKind kind); // "Blitz"...

// One difficulty's columns of a race's row.
struct RaceSettings {
    int carType = 0;
    int timeOfDay = 0, weather = 0; // as the 16 lightings: time * 4 + weather
    int opponents = 0, cops = 0;
    float ambient = 0, peds = 0; // traffic and pedestrian densities
    int laps = 0;
    float timeLimit = 0; // seconds
    int difficulty = 0;
};

struct Waypoint {
    Vec3 position{};
    float heading = 0; // degrees: 0 faces -Z, 90 faces +X
    float radius = 0;  // metres; 0 where the file gives none (circuits)
};

struct RaceDefinition {
    RaceKind kind = RaceKind::Blitz;
    int index = 0;
    std::string name;
    std::array<RaceSettings, 2> settings{}; // amateur, professional
    std::vector<Waypoint> waypoints;        // the first is the start
    // Each difficulty's opponents: a start (position and heading) and the
    // points they drive through.
    struct Opponent {
        Vec3 start{};
        float heading = 0; // the first line's fourth value (not a reliable heading)
        std::vector<Vec3> path;
        // The first path point when it lies behind the start (as in 540 of
        // the 600 files: its meaning isn't known), left out of `path`.
        bool skippedFirst = false;
        float startYaw = 0; // facing the first point of `path`
    };
    std::array<std::vector<Opponent>, 2> opponents;
};

// How many races of a kind a city has, and their names (.cinfo).
std::vector<std::string> raceNames(const SearchSet& files, const std::string& city, RaceKind kind);

Error loadRace(const SearchSet& files, const std::string& city, RaceKind kind, int index, RaceDefinition& out);

// The race's settings for the computer-driven cars, people and police:
// race/<city>/<file><n>.aimap, or .aimap_p for a professional run (see
// aimap.h).
std::string raceAiMapName(const std::string& city, RaceKind kind, int index, bool professional);
// The race's own name in its files: "blitz3", "circuit0", "race5".
std::string raceStem(RaceKind kind, int index);

// Parsers for the files, for tests.
Error parseRaceSettings(const std::string& csv, int index, std::array<RaceSettings, 2>& out);
Error parseWaypoints(const std::string& csv, std::vector<Waypoint>& out);
Error parseOpponentPath(const std::string& csv, RaceDefinition::Opponent& out);

// A waypoint heading (degrees) as a turn about Y, as CarSim::reset takes.
inline float headingToYaw(float degrees) { return -degrees * 3.14159265358979f / 180; }

} // namespace reluva::age
