// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Wormholes: how they pair, open and close, and carry the ships sent into
// them out of their partners, as the program's wormhole_obj does
// (docs/engines/storm3d/games/armada/systems/wormholes.md).

#include "engines/storm3d/play/mission_view.h"

#include "audio/wav.h"
#include "common/text.h"

#include <algorithm>
#include <cmath>

namespace reluva::storm3d {

namespace {

// The handles scripts give the map's own objects from (script_calls.cpp).
constexpr int32_t kObjectHandles = 0x100000;
// How far out a ship joins a wormhole's queue, and how far past its
// partner it comes out to (0x54dae0, 0x54d3f0).
constexpr float kQueueReach = 100;

gfx::Vec3 hermite(const std::array<gfx::Vec3, 4> &c, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float a = 2 * t3 - 3 * t2 + 1;
    const float b = t3 - 2 * t2 + t;
    const float d = -2 * t3 + 3 * t2;
    const float e = t3 - t2;
    gfx::Vec3 p{};
    for (int k = 0; k < 3; ++k)
        p[k] = c[0][k] * a + c[2][k] * b + c[1][k] * d + c[3][k] * e;
    return p;
}

float distance(const gfx::Vec3 &a, const gfx::Vec3 &b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// A curve's length, by eight chords.
float lengthOf(const std::array<gfx::Vec3, 4> &curve) {
    float length = 0;
    gfx::Vec3 last = curve[0];
    for (int i = 1; i <= 8; ++i) {
        const gfx::Vec3 next = hermite(curve, float(i) / 8);
        length += distance(last, next);
        last = next;
    }
    return std::max(length, 1.0f);
}

} // namespace

void MissionView::addWormhole(const Odf &def, std::optional<size_t> unit, std::optional<size_t> object) {
    // Its keys' defaults where it gives none (0x44aaf0).
    Wormhole w;
    w.unit = unit;
    w.object = object;
    w.closedScale = float(def.number("ClosedScale", 10));
    w.openScale = float(def.number("OpenScale", 50));
    const float time = float(def.number("AnimationTime", 2));
    w.rate = time > 0 ? (w.openScale - w.closedScale) / time : w.openScale;
    w.scale = w.closedScale;
    w.speedRange = float(def.number("SpeedEffectRange", 50));
    w.maxSpeed = float(def.number("MaxSpeedFactor", 4));
    w.openSound = def.string("openSound");
    w.closeSound = def.string("closeSound");
    // It pairs with the one made before it if that one's waiting; else it
    // waits for the next (0x4511f0).
    const int hole = int(_wormholes.size());
    if (_waitingWormhole >= 0) {
        w.partner = _waitingWormhole;
        _wormholes[size_t(_waitingWormhole)].partner = hole;
        _waitingWormhole = -1;
    } else {
        _waitingWormhole = hole;
    }
    _wormholes.push_back(w);
    if (unit)
        _scene.scaleUnit(*unit, w.scale);
}

std::optional<size_t> MissionView::wormholeOfHandle(int32_t handle) const {
    for (size_t i = 0; i < _wormholes.size(); ++i) {
        const Wormhole &w = _wormholes[i];
        if (w.gone)
            continue;
        if ((w.unit && int32_t(*w.unit) + 1 == handle) || (w.object && kObjectHandles + int32_t(*w.object) == handle))
            return i;
    }
    return std::nullopt;
}

std::optional<size_t> MissionView::wormholeNear(const gfx::Vec3 &point) const {
    // A move to within 5 of the nearest wormhole is a move into it (0x451830).
    std::optional<size_t> best;
    float bestDistance = 5;
    for (size_t i = 0; i < _wormholes.size(); ++i)
        if (!_wormholes[i].gone) {
            const float d = distance(point, wormholeAt(i));
            if (d < bestDistance)
                best = i, bestDistance = d;
        }
    return best;
}

gfx::Vec3 MissionView::wormholeAt(size_t hole) const {
    const Wormhole &w = _wormholes[hole];
    if (w.unit && *w.unit < _scene.units().size())
        return _scene.units()[*w.unit].at;
    if (w.object && *w.object < _mapObjects.size())
        return _mapObjects[*w.object].second;
    return {};
}

bool MissionView::inWormholeQueue(size_t unit) const {
    return std::any_of(_passages.begin(), _passages.end(), [unit](const Passage &p) { return p.unit == unit; });
}

void MissionView::sendThrough(size_t unit, size_t hole) {
    // To its queue's point: 100 from its middle towards the ship, or where
    // the ship is if it's nearer than that (0x54dae0).
    Movement::Ship &ship = _ships[unit];
    const gfx::Vec3 middle = wormholeAt(hole);
    const float dx = ship.at[0] - middle[0];
    const float dz = ship.at[2] - middle[2];
    const float far = std::hypot(dx, dz);
    _boundFor.erase(unit);
    if (distance(ship.at, middle) <= kQueueReach || far <= 0) {
        enterWormhole(unit, hole);
        return;
    }
    const gfx::Vec3 point{middle[0] + dx / far * kQueueReach, ship.at[1], middle[2] + dz / far * kQueueReach};
    ship.route.clear();
    ship.goal = point;
    ship.waiting = Movement::kSetOff;
    _boundFor[unit] = {hole, point};
}

void MissionView::enterWormhole(size_t unit, size_t hole) {
    // Down into its middle: away along its heading at first, straight down
    // at the end (0x54d2f0).
    Movement::Ship &ship = _ships[unit];
    const gfx::Vec3 middle = wormholeAt(hole);
    const float d = distance(ship.at, middle);
    Passage p;
    p.unit = unit;
    p.hole = hole;
    p.height = ship.at[1];
    const float f = 0.3f * d;
    p.curve = {ship.at, middle, gfx::Vec3{std::sin(ship.heading) * f, 0, std::cos(ship.heading) * f},
               gfx::Vec3{0, -0.42f * d, 0}};
    p.length = lengthOf(p.curve);
    ship.goal.reset();
    ship.route.clear();
    ship.speed = 0;
    _boundFor.erase(unit);
    _passages.push_back(p);
}

void MissionView::loseWormhole(size_t hole) {
    // Its partner is left without one, and waits for the next made if none
    // is waiting (0x4516f0).
    Wormhole &w = _wormholes[hole];
    w.gone = true;
    if (_waitingWormhole == int(hole))
        _waitingWormhole = -1;
    for (size_t i = 0; i < _wormholes.size(); ++i)
        if (_wormholes[i].partner == int(hole)) {
            _wormholes[i].partner = -1;
            if (_waitingWormhole < 0 && !_wormholes[i].gone)
                _waitingWormhole = int(i);
        }
    w.partner = -1;
    std::erase_if(_boundFor, [hole](const auto &b) { return b.second.first == hole; });
}

void MissionView::playWormholeSound(const std::string &name) {
    if (!_mixer || name.empty())
        return;
    // The definitions say ".wave" of what the install has as ".wav" *(inferred)*.
    std::string path = pathByName(name);
    if (path.empty() && text::hasSuffixIgnoreCase(name, ".wave"))
        path = pathByName(name.substr(0, name.size() - 1));
    auto sound = std::make_shared<audio::Sound>();
    std::vector<uint8_t> data;
    if (!path.empty() && !_files.read(path, data) && !audio::decodeWav(data, *sound))
        _mixer->play(sound, audio::SoundType::kEffects);
}

void MissionView::runWormholes(double seconds) {
    if (_wormholes.empty())
        return;
    const std::vector<MapScene::Unit> &units = _scene.units();
    // Ships sent in join the queue where the move ends, if that's within
    // their tooCloseToTurn of its point; sent elsewhere, they're not bound
    // for it any more (0x426bc0).
    for (auto it = _boundFor.begin(); it != _boundFor.end();) {
        const size_t unit = it->first;
        const auto [hole, point] = it->second;
        const Movement::Ship &ship = _ships[unit];
        if (_dead[unit] || _wormholes[hole].gone || (ship.goal && distance(*ship.goal, point) > 0.5f)) {
            it = _boundFor.erase(it);
            continue;
        }
        if (ship.goal) {
            ++it;
            continue;
        }
        const float reach = _physics[unit] ? _physics[unit]->turnFrom : 40;
        const bool there = std::hypot(ship.at[0] - point[0], ship.at[2] - point[2]) <= reach;
        it = _boundFor.erase(it);
        if (there)
            enterWormhole(unit, hole);
    }
    // Along their curves, faster the nearer the wormhole's middle
    // (0x54df84), whatever the wormholes' state.
    for (size_t i = 0; i < _passages.size();) {
        Passage &p = _passages[i];
        Movement::Ship &ship = _ships[p.unit];
        if (_dead[p.unit]) {
            _passages.erase(_passages.begin() + std::ptrdiff_t(i));
            continue;
        }
        // An order given on the way waits till it's out (0x54cb30).
        if (ship.goal) {
            p.held = ship.goal;
            ship.goal.reset();
            ship.route.clear();
        }
        ship.speed = 0;
        const Wormhole &w = _wormholes[p.hole];
        const float d = distance(ship.at, wormholeAt(p.hole));
        const float r = w.speedRange;
        const float factor = r > 0 && d < r ? w.maxSpeed + d * d * (1 - w.maxSpeed) / (r * r) : 1;
        const float top = _physics[p.unit] ? _physics[p.unit]->topSpeed : 50;
        p.t += float(seconds) * top * factor / p.length;
        ship.at = hermite(p.curve, std::min(p.t, 1.0f));
        if (p.t < 1) {
            _scene.moveUnit(p.unit, ship.at, ship.heading);
            ++i;
            continue;
        }
        if (!p.out) {
            // Through at once to its partner's middle, heading as it was,
            // then out 100 ahead at the height it went in at (0x54dc10,
            // 0x54d3f0). Without a partner it stays where it is
            // *(Reluva's own: the program's case not traced)*.
            const int partner = _wormholes[p.hole].partner;
            if (partner < 0 || _wormholes[size_t(partner)].gone) {
                _scene.moveUnit(p.unit, ship.at, ship.heading);
                _passages.erase(_passages.begin() + std::ptrdiff_t(i));
                continue;
            }
            const gfx::Vec3 from = wormholeAt(size_t(partner));
            const gfx::Vec3 ahead{std::sin(ship.heading), 0, std::cos(ship.heading)};
            const gfx::Vec3 to{from[0] + ahead[0] * kQueueReach, p.height, from[2] + ahead[2] * kQueueReach};
            constexpr float kTangent = 45;
            p.curve = {from, to, gfx::Vec3{ahead[0] * kTangent, 0, ahead[2] * kTangent},
                       gfx::Vec3{ahead[0] * kTangent, 0, ahead[2] * kTangent}};
            p.length = lengthOf(p.curve);
            p.hole = size_t(partner);
            p.out = true;
            p.t = 0;
            ship.at = from;
            _scene.moveUnit(p.unit, ship.at, ship.heading);
            ++i;
            continue;
        }
        // Out: the order held, or a stop (0x54dcf0).
        if (p.held) {
            ship.goal = p.held;
            ship.waiting = Movement::kSetOff;
        }
        _scene.moveUnit(p.unit, ship.at, ship.heading);
        _passages.erase(_passages.begin() + std::ptrdiff_t(i));
    }
    // Opening, both at once, when one with a partner has a ship in its
    // queue; closing, both, once neither has any (0x451960, 0x451a80).
    auto queued = [this](size_t hole, bool entering) {
        return std::any_of(_passages.begin(), _passages.end(),
                           [&](const Passage &p) { return p.hole == hole && (!entering || !p.out); });
    };
    for (size_t i = 0; i < _wormholes.size(); ++i) {
        Wormhole &w = _wormholes[i];
        if (w.gone)
            continue;
        Wormhole *partner = w.partner >= 0 ? &_wormholes[size_t(w.partner)] : nullptr;
        switch (w.state) {
        case Wormhole::State::kClosed:
            if (partner && queued(i, true)) {
                w.state = Wormhole::State::kOpening;
                partner->state = Wormhole::State::kOpening;
                playWormholeSound(w.openSound);
            }
            break;
        case Wormhole::State::kOpening:
            w.scale = std::min(w.openScale, w.scale + w.rate * float(seconds));
            if (w.scale >= w.openScale)
                w.state = Wormhole::State::kOpen;
            break;
        case Wormhole::State::kOpen:
            if (!queued(i, false) && (!partner || !queued(size_t(w.partner), false))) {
                w.state = Wormhole::State::kClosing;
                if (partner)
                    partner->state = Wormhole::State::kClosing;
                playWormholeSound(w.closeSound);
            }
            break;
        case Wormhole::State::kClosing:
            w.scale = std::max(w.closedScale, w.scale - w.rate * float(seconds));
            if (w.scale <= w.closedScale)
                w.state = Wormhole::State::kClosed;
            break;
        }
        if (w.unit && *w.unit < units.size())
            _scene.scaleUnit(*w.unit, w.scale);
    }
}

} // namespace reluva::storm3d
