// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The park awards, held while the park measures up to the general
// settings' ParkAward: docs/engines/tpw/games/simcoaster/systems/park-awards.md.

#include "engines/tpw/park/park.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace reluva::tpw {

void Park::loadAwards(const SamFile &general) {
    const auto number = [&](const char *name, int fallback) {
        return int(general.number(std::string("ParkAward.") + name, fallback));
    };
    _awardRules = {number("CoasterHeight", 110), number("CoasterSpeed", 100),
                   number("Hygiene", 90),        number("HygieneMinParkSize", 1000),
                   number("Security", 25),       number("Decoration", 15),
                   number("Training", 50),       number("TrainingMinStaff", 10),
                   number("GokartSpeed", 5),     number("WaterLength", 50)};
}

// Each award's measure against its setting ("Percentage of unlittered
// cells in the park", "percentage of park covered by guard patrol areas
// + percentage covered by security cameras", "percentage of cells
// covered by objects, which are features", "percentage of staff that
// have been trained in a special ability"); the coasters', go-kart
// tracks' and water rides' can't be measured, there being none.
std::array<Park::AwardStanding, Park::kAwardCount> Park::awards() const {
    std::array<AwardStanding, kAwardCount> out{};
    const AwardRules &r = _awardRules;
    out[size_t(Award::kHighestCoaster)].needed = r.coasterHeight;
    out[size_t(Award::kFastestCoaster)].needed = r.coasterSpeed;
    out[size_t(Award::kFastestGokart)].needed = r.gokartSpeed;
    out[size_t(Award::kLongestWaterRide)].needed = r.waterLength;

    // The park's cells: its own, its paths and queues, its objects'; the
    // features', the patrols' and the cameras' among them.
    const auto at = [&](const Cell &c) { return size_t(c.y) * size_t(_mapWidth) + size_t(c.x); };
    std::vector<uint8_t> featured(_ground.size(), 0);
    std::vector<uint8_t> watched(_ground.size(), 0);
    for (const PlacedObject &o : _objects) {
        const Attractions::Attraction *a = _attractions.find(o.id);
        if (!a || a->obstacle)
            continue;
        if (a->kind == Attractions::Kind::kFeature)
            for (const Cell &c : footprint(a->shape, o.at, o.angle).cells)
                if (ground(c) == Ground::kTaken)
                    featured[at(c)] = 1;
        if (a->security)
            for (int y = o.at.y - kCameraCells; y <= o.at.y + kCameraCells; ++y)
                for (int x = o.at.x - kCameraCells; x <= o.at.x + kCameraCells; ++x)
                    if (ground({x, y}) != Ground::kOutside)
                        watched[at({x, y})] = 1;
    }
    std::vector<uint8_t> patrolled(_ground.size(), 0);
    for (const StaffMember &m : _staff)
        if (m.kind == StaffKind::kGuard && m.patrolling)
            for (int y = std::min(m.patrolFrom.y, m.patrolTo.y); y <= std::max(m.patrolFrom.y, m.patrolTo.y); ++y)
                for (int x = std::min(m.patrolFrom.x, m.patrolTo.x); x <= std::max(m.patrolFrom.x, m.patrolTo.x); ++x)
                    if (ground({x, y}) != Ground::kOutside)
                        patrolled[at({x, y})] = 1;
    std::vector<uint8_t> littered(_ground.size(), 0);
    for (const Litter &l : _litter) {
        const Cell c{int(std::floor(l.x / kCellUnits)), int(std::floor(l.z / kCellUnits))};
        if (ground(c) != Ground::kOutside)
            littered[at(c)] = 1;
    }
    int owned = 0;
    int covered = 0;
    int walked = 0;
    int clean = 0;
    int features = 0;
    int patrols = 0;
    int cameras = 0;
    for (size_t i = 0; i < _ground.size(); ++i) {
        const Ground g = _ground[i];
        if (g == Ground::kOutside)
            continue;
        ++owned;
        covered += g == Ground::kTaken;
        features += featured[i];
        patrols += patrolled[i];
        cameras += watched[i];
        if (g == Ground::kPath || g == Ground::kQueue) {
            ++walked;
            clean += !littered[i];
        }
    }
    const auto percent = [](int part, int whole) { return whole > 0 ? part * 100 / whole : 0; };

    AwardStanding &hygiene = out[size_t(Award::kCleanestPark)];
    hygiene = {true, percent(clean, walked), r.hygiene};
    hygiene.held = covered >= r.hygieneMinCells && walked > 0 && hygiene.value >= r.hygiene;
    AwardStanding &decoration = out[size_t(Award::kMostDecoration)];
    decoration = {true, percent(features, covered), r.decoration};
    decoration.held = covered > 0 && decoration.value >= r.decoration;
    AwardStanding &security = out[size_t(Award::kBestSecurity)];
    security = {true, percent(patrols, owned) + percent(cameras, owned), r.security};
    security.held = security.value >= r.security;
    const int trained =
        int(std::count_if(_staff.begin(), _staff.end(), [](const StaffMember &m) { return m.ability > 0; }));
    AwardStanding &training = out[size_t(Award::kBestStaffTraining)];
    training = {true, percent(trained, int(_staff.size())), r.training};
    training.held = int(_staff.size()) >= r.trainingMinStaff && training.value >= r.training;
    for (AwardStanding &a : out)
        a.held = a.measured && a.held;
    return out;
}

int Park::awardsHeld() const {
    const std::array<AwardStanding, kAwardCount> standing = awards();
    return int(std::count_if(standing.begin(), standing.end(), [](const AwardStanding &a) { return a.held; }));
}

} // namespace reluva::tpw
