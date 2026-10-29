// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Cops & Robbers, the multiplayer game of the gold: picked up by driving
// through it, knocked loose by ramming the car that has it, delivered to
// your side's base for a point. The places are the city's
// race/<city>/multicopwaypoints.csv; the rules are Reluva's reading of the
// game's menus, messages and announcer lines (the game's program, which
// has them, is encrypted). See docs/engines/age-multiplayer.md.

#include "engines/age/net_session.h"
#include "engines/age/race.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::age {

struct GoldEvent {
    enum class Kind : uint8_t {
        Got,       // picked up where it started
        Recovered, // picked up where it was dropped
        Dropped,   // knocked loose (or the car went out of the city)
        Delivered, // to the side's base: a point
        Returned,  // lay too long, or fell out of the city: back where it started
        TimeUp,
        PointLimit,
    };
    Kind kind = Kind::Got;
    uint8_t player = kNoPlayer;
    uint8_t side = 0; // the player's side then
    uint32_t seq = 0;
};

class GoldGame {
public:
    struct Car {
        uint8_t id = 0;
        uint8_t team = 0;
        Vec3 position{}, velocity{};
    };

    // The places to play between; the seed picks the gold's home and the
    // bases from them.
    void start(const NetSettings& settings, const std::vector<Waypoint>& points, uint32_t seed);
    // The host: a step of the rules with every car where it is. `floor`:
    // below this the gold has left the city.
    void step(float clock, float dt, const std::vector<Car>& cars, float floor);

    // A player's side: its team in the team modes, else itself.
    uint8_t sideOf(const Car& car) const { return teams() ? car.team : car.id; }
    bool teams() const { return mode_ != GoldMode::FreeForAll; }
    GoldMode mode() const { return mode_; }

    uint8_t holder() const { return holder_; }
    const Vec3& goldPosition() const { return gold_; }
    // The base a side delivers to (in a free-for-all, everyone's hideout).
    Vec3 base(uint8_t side) const;
    const Vec3& home() const { return home_; }
    int score(uint8_t side) const;
    const std::map<uint8_t, int>& scores() const { return scores_; }
    bool over() const { return over_; }
    // The events since `seq` (the last one seen), oldest first.
    std::vector<GoldEvent> eventsAfter(uint32_t seq) const;
    uint32_t lastEvent() const { return eventSeq_; }

    // As the host sends it, and as a player reads it.
    std::vector<uint8_t> encode() const;
    bool decode(const std::vector<uint8_t>& data);

private:
    void event(GoldEvent::Kind kind, uint8_t player, uint8_t side);
    void restAt(const Vec3& at, bool atHome);

    GoldMode mode_ = GoldMode::FreeForAll;
    GoldLimit limit_ = GoldLimit::None;
    int limitValue_ = 0;
    std::vector<Vec3> points_;
    Vec3 home_{}, gold_{};
    std::array<Vec3, 2> bases_{};
    uint8_t holder_ = kNoPlayer;
    bool atHome_ = true;
    float restingFor_ = 0;            // seconds it's lain away from home
    uint8_t lastDropper_ = kNoPlayer; // can't pick it straight up again
    float dropperWait_ = 0;
    float holderSafe_ = 0; // a new holder can't be knocked loose at once
    std::map<uint8_t, int> scores_;
    bool over_ = false;
    std::vector<GoldEvent> events_; // the last few
    uint32_t eventSeq_ = 0;
};

// The game's points for Cops & Robbers: race/<city>/multicopwaypoints.csv.
Error loadGoldPoints(const SearchSet& files, const std::string& city, std::vector<Waypoint>& out);

// The announcer's lines: aud/spchdata/CNR<CITY>.csv, each event's name
// (BLUETEAMHASGOLD, ROBGETLOOT...) and the recordings it may play.
std::map<std::string, std::vector<std::string>> parseSpeechTable(const std::string& csv);

} // namespace reluva::age
