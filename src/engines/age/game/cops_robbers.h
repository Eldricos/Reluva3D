// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Cops & Robbers, the multiplayer game of the gold: picked up by driving
// through it, knocked loose by ramming the car that has it, delivered to
// your side's base for a point. The places are the city's
// race/<city>/multicopwaypoints.csv; the rules are Reluva's reading of the
// game's menus, messages and announcer lines (the game's program, which
// has them, is encrypted). See docs/engines/age-multiplayer.md.

#include "engines/age/game/net_session.h"
#include "engines/age/formats/race.h"
#include "engines/age/game/road_graph.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::age {

struct GoldEvent {
    enum class Kind : uint8_t {
        kGot,       // picked up where it started
        kRecovered, // picked up where it was dropped
        kDropped,   // knocked loose (or the car went out of the city)
        kDelivered, // to the side's base: a point
        kReturned,  // lay too long, or fell out of the city: back where it started
        kTimeUp,
        kPointLimit,
    };
    Kind kind = Kind::kGot;
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
        // Put back (fallen out, stuck) in the last moments: it drops the
        // gold it had where it was, and can't take it or ram it loose for
        // a while, so a car can't be "put back" onto the gold or its base.
        bool putBack = false;
    };

    // The places to play between; the seed picks the gold's home and the
    // bases from them.
    void start(const NetSettings& settings, const std::vector<Waypoint>& points, uint32_t seed);
    // The host: a step of the rules with every car where it is. `floor`:
    // below this the gold has left the city.
    void step(float clock, float dt, const std::vector<Car>& cars, float floor);

    // A player's side: its team in the team modes, else itself.
    uint8_t sideOf(const Car& car) const { return teams() ? car.team : car.id; }
    bool teams() const { return _mode != GoldMode::kFreeForAll; }
    GoldMode mode() const { return _mode; }

    uint8_t holder() const { return _holder; }
    const Vec3& goldPosition() const { return _gold; }
    // The base a side delivers to (in a free-for-all, everyone's hideout).
    Vec3 base(uint8_t side) const;
    const Vec3& home() const { return _home; }
    int score(uint8_t side) const;
    const std::map<uint8_t, int>& scores() const { return _scores; }
    bool over() const { return _over; }
    // The events since `seq` (the last one seen), oldest first.
    std::vector<GoldEvent> eventsAfter(uint32_t seq) const;
    uint32_t lastEvent() const { return _eventSeq; }

    // As the host sends it, and as a player reads it.
    std::vector<uint8_t> encode() const;
    bool decode(const std::vector<uint8_t>& data);

private:
    void event(GoldEvent::Kind kind, uint8_t player, uint8_t side);
    void restAt(const Vec3& at, bool atHome);

    GoldMode _mode = GoldMode::kFreeForAll;
    GoldLimit _limit = GoldLimit::kNone;
    int _limitValue = 0;
    std::vector<Vec3> _points;
    Vec3 _home{}, _gold{};
    std::array<Vec3, 2> _bases{};
    uint8_t _holder = kNoPlayer;
    bool _atHome = true;
    float _restingFor = 0;            // seconds it's lain away from home
    uint8_t _lastDropper = kNoPlayer; // can't pick it straight up again
    float _dropperWait = 0;
    float _holderSafe = 0; // a new holder can't be knocked loose at once
    std::map<uint8_t, int> _scores;
    bool _over = false;
    std::vector<GoldEvent> _events; // the last few
    uint32_t _eventSeq = 0;
};

// A computer player's mind: where to drive, as a path along the roads for
// its PathDriver. In Cops & Robbers it takes the gold, carries it home,
// keeps near a teammate who has it and rams whoever else has it; in a
// cruise it drives from place to place.
class GoldBot {
public:
    void start(uint32_t seed) { *this = GoldBot(), _seed = seed; }
    // Where it wants to be: `chasing` when that's another side's car.
    static Vec3 goal(uint8_t id, uint8_t team, const GoldGame& gold, const std::vector<GoldGame::Car>& cars,
                     bool& chasing);
    // A new path when it's time for one (the goal moved, the last path
    // was driven, or a while has passed); false to keep on. `gold` null:
    // a cruise, between `places`.
    bool plan(float dt, const Vec3& at, uint8_t id, uint8_t team, const GoldGame* gold,
              const std::vector<GoldGame::Car>& cars, const std::vector<Waypoint>& places, const RoadGraph& roads,
              bool pathDone, std::vector<Vec3>& path);
    // Hardly moved for six seconds with its goal still away: the caller
    // puts it back on the road (the nearest point of `roads`) and calls
    // unstuck().
    bool stuck() const { return _stillFor > 6; }
    void unstuck() {
        _stillFor = 0;
        _replanIn = 0;
    }

private:
    uint32_t _seed = 1;
    float _replanIn = 0;
    Vec3 _planned{};
    bool _planning = false;
    size_t _place = 0;
    Vec3 _still{};       // where it was when it last moved 4 m
    float _stillFor = 0; // and how long ago
};

// The game's points for Cops & Robbers: race/<city>/multicopwaypoints.csv.
Error loadGoldPoints(const SearchSet& files, const std::string& city, std::vector<Waypoint>& out);

// The announcer's lines: aud/spchdata/CNR<CITY>.csv, each event's name
// (BLUETEAMHASGOLD, ROBGETLOOT...) and the recordings it may play.
std::map<std::string, std::vector<std::string>> parseSpeechTable(const std::string& csv);

} // namespace reluva::age
