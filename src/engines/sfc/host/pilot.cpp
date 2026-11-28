// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/host/pilot.h"

#include "common/events.h"

#include <cmath>
#include <cstdio>

namespace reluva::sfc {

namespace {

using Action = Pilot::Action;

Action press(uint32_t key) {
    return Action{key, 0, 0};
}

// The game's keys (Assets/specs/bindings.ini, and the readme's list).
constexpr uint32_t kRedAlert = 'r';
constexpr uint32_t kFaster = 's';
constexpr uint32_t kFire = 'z';
constexpr uint32_t kNearestEnemy = '`';
// The keypad's shield keys, by the shield facing the enemy: forward, then
// round to the right (ReinforceForwardShield, ...RightShield, ...).
constexpr uint32_t kReinforce[6] = {key::kKp0 + 8, key::kKp0 + 9, key::kKp0 + 3,
                                    key::kKp0 + 2, key::kKp0 + 1, key::kKp0 + 7};

// Steering: a click on the 3D view turns the ship toward that side of it,
// the camera sitting behind the ship (as the tutorials have it). At 800
// by 600 the view is right of the panels, its middle at x 500.
constexpr int kViewMiddleX = 500;
constexpr int kViewMiddleY = 300;
constexpr int kTurnReach = 280;

// Ranges, as the game shows them. It steers for the target; inside
// kTooClose, where the enemy's overloads hit hardest, it turns away until
// past kTooFar. It fires inside kFireRange with the target within
// kFireArc degrees of the bow, where a frigate's photons and phasers bear.
constexpr float kTooFar = 10.0f;
constexpr float kTooClose = 3.0f;
constexpr float kFireRange = 8.0f;
constexpr float kFireArc = 60.0f;
// Within this many degrees of the way it wants it holds its course.
constexpr float kOnCourse = 15.0f;

// Frames between shots, between choices of target and between turns: a
// shot fires what's ready, and a turn takes a moment to show.
constexpr uint64_t kFireEvery = 30;
constexpr uint64_t kSelectEvery = 4;
constexpr uint64_t kReselectEvery = 120;
constexpr uint64_t kSteerEvery = 20;
// Presses of S go on, the speed the helm is given rising a step a press:
// the first, as a mission starts, can come before its helm takes them.
constexpr uint64_t kSpeedEvery = 90;

} // namespace

std::set<int> Pilot::hostiles(const std::vector<Battle::Object> &ships, int player) {
    // As the game reckons the sides (Battle::Object::relation): neutrals,
    // as a peace conference's delegates, are left alone.
    std::set<int> foes;
    for (const Battle::Object &s : ships)
        if (s.id != player && s.hostile())
            foes.insert(s.id);
    return foes;
}

std::optional<Pilot::Action> Pilot::step(uint64_t frame) {
    // One input a frame: the game reads them as Windows' messages and
    // drops presses that come together (six presses of S in one frame
    // left the speed at 1).
    for (const Action &a : decide(frame))
        _queue.push_back(a);
    if (_queue.empty())
        return std::nullopt;
    const Action a = _queue.front();
    _queue.pop_front();
    return a;
}

std::vector<Pilot::Action> Pilot::decide(uint64_t frame) {
    std::vector<Action> out;
    _note.clear();
    if (!_battle.running() || !_queue.empty())
        return out;
    const int playerId = _battle.playerId();
    std::vector<Battle::Object> ships;
    for (const Battle::Object &o : _battle.objects())
        if (o.ship)
            ships.push_back(o);
    const Battle::Object *player = nullptr;
    for (const Battle::Object &s : ships)
        if (s.id == playerId)
            player = &s;
    if (!player)
        return out;
    if (playerId != _playerId) {
        // A new ship to fly: the player's was lost and the game moved them.
        _playerId = playerId;
        _started = false;
        _away = false;
        _shield = -1;
    }
    if (!_started) {
        _started = true;
        _lastSelect = frame;
        return {press(kRedAlert), press(kFaster), press(kFaster), press(kFaster),
                press(kFaster),   press(kFaster), press(kFaster), press(kNearestEnemy)};
    }

    // The game's own "nearest enemy" picks the target: again when the one
    // held isn't hostile, and every so often, as others come nearer.
    const std::set<int> foes = hostiles(ships, playerId);
    const bool held = foes.count(player->target) != 0;
    if ((!held && frame - _lastSelect >= kSelectEvery) || frame - _lastSelect >= kReselectEvery) {
        _lastSelect = frame;
        return {press(kNearestEnemy)};
    }
    const Battle::Object *target = nullptr;
    for (const Battle::Object &s : ships)
        if (s.id == player->target && held)
            target = &s;
    if (!target)
        return out;

    const float range = Battle::range(*player, *target);
    // Where the target bears, in degrees from the bow, positive to the
    // right: the heading turned a quarter clockwise is the right.
    const float dx = target->x - player->x;
    const float dy = target->y - player->y;
    const float ahead = dx * player->headingX + dy * player->headingY;
    const float right = dx * player->headingY - dy * player->headingX;
    const float bearing = std::atan2(right, ahead) * 57.29578f;

    if (!_away && range < kTooClose)
        _away = true;
    else if (_away && range > kTooFar)
        _away = false;
    // The way it wants to go: at the target, or straight away from it.
    float want = _away ? bearing + 180.0f : bearing;
    while (want > 180.0f)
        want -= 360.0f;
    while (want < -180.0f)
        want += 360.0f;
    if (std::fabs(want) > kOnCourse && frame - _lastSteer >= kSteerEvery) {
        _lastSteer = frame;
        const int side = want > 0 ? 1 : -1;
        out.push_back(Action{0, kViewMiddleX + side * kTurnReach, kViewMiddleY});
    }
    // The shield the target faces, of six, each 60 degrees from the bow's.
    const int shield = static_cast<int>(std::floor((bearing + 30.0f + 360.0f) / 60.0f)) % 6;
    if (shield != _shield) {
        _shield = shield;
        out.push_back(press(kReinforce[shield]));
    }
    if (frame - _lastSpeed >= kSpeedEvery) {
        _lastSpeed = frame;
        out.push_back(press(kFaster));
    }
    if (range <= kFireRange && std::fabs(bearing) <= kFireArc && frame - _lastFire >= kFireEvery) {
        _lastFire = frame;
        out.push_back(press(kFire));
    }
    char line[160];
    std::snprintf(line, sizeof line, "pilot: ship %d, target %d at range %.0f bearing %.0f, %s, %zu hostile", playerId,
                  target->id, double(range), double(std::round(bearing / 10.0f) * 10.0f),
                  _away ? "turned away" : "closing", foes.size());
    if (_lastNote != line)
        _note = _lastNote = line;
    return out;
}

} // namespace reluva::sfc
