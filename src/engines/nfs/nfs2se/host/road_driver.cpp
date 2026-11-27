// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/host/road_driver.h"

#include "engines/nfs/host/game_host.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace reluva::nfs::nfs2se {

namespace {

// nfs2sen.exe's, from its code (road-driver.md).
// The local player's controls, sampled from the keys and joystick into a
// record, go each frame (0x448720) into a ring of every player's, by
// frame, which drives the cars (0x45e8a0) and the replay; on a link, sent
// to the other copies too (0x45dae0). The driver writes the record just
// before, at that call, so that all of them have its controls.
constexpr uint32_t kStoreInputCall = 0x4403a9; // call 0x448720
constexpr uint32_t kStoreInput = 0x448720;
constexpr uint32_t kControls = 0x4ddb10; // s8 steering, u8 gas, u8 brake, u8 buttons
constexpr uint8_t kShiftDown = 0x01;
constexpr uint8_t kShiftUp = 0x02;
constexpr uint32_t kCars = 0x4dcbe8; // pointers, by slot
constexpr uint32_t kPlayerSlot = 0x4dd2ec;
constexpr uint32_t kRoadPoints = 0x4dc9d8; // the .col's, 36 bytes each (col.md)
constexpr uint32_t kRoadCount = 0x4dc9d0;
constexpr uint32_t kRaceRunning = 0x4fd580;
constexpr uint32_t kRaceCars = 0x4dcf64;
constexpr uint32_t kRaceLaps = 0x4dd2dc;
// The computer cars' speeds round the track, in m/s: a u16 each two
// points, its low byte (read at 0x4258a5).
constexpr uint32_t kPace = 0x4c2b08;
constexpr uint32_t kPaceAhead = 4; // points

// A car's fields: 16.16 unless said.
constexpr uint32_t kSlice = 0x14; // its road point
constexpr uint32_t kX = 0x9c;
constexpr uint32_t kZ = 0xa4;
constexpr uint32_t kRight = 0xc4;   // the orientation's rows: right,
constexpr uint32_t kForward = 0xdc; // and forward (x, height, z)
constexpr uint32_t kLaps = 0x1f8;
constexpr uint32_t kProgress = 0x220; // laps (less those owed) times the road's points, plus its point
constexpr uint32_t kFinished = 0x280; // 2 once over the line for the last time
constexpr uint32_t kSpeed = 0x2b8;    // m/s
constexpr uint32_t kGear = 0x2da;     // u8: 0 reverse, 1 neutral, 2 drive

constexpr float kPi = 3.14159265f;
// What the car is taken to do, in m/s²: turn at (its grip) and brake at;
// and its top speed, in m/s (the McLaren's 228 mph).
constexpr float kGrip = 14.0f;
constexpr float kBraking = 9.0f;
constexpr float kTopSpeed = 102.0f;

} // namespace

Error RoadDriver::install(GameHost &host, const Options &options) {
    x86::Cpu &cpu = host.cpu();
    const std::array<uint8_t, 5> expected = {0xe8, 0x72, 0x83, 0x00, 0x00};
    for (size_t i = 0; i < expected.size(); ++i)
        if (!cpu.valid(kStoreInputCall + uint32_t(i), 1) || cpu.read8(kStoreInputCall + uint32_t(i)) != expected[i])
            return Error::fail("the road driver needs the retail nfs2sen.exe");
    _options = options;
    const uint32_t native = cpu.addNative("nfs2se road driver", [this, &host](x86::Cpu &c) {
        // Ours over the keys' for the local player's car, then on as the
        // game's.
        if (const uint32_t car = c.read32(kCars + 4 * c.read32(kPlayerSlot)))
            drive(host, car);
        uint32_t ignored = 0;
        if (Error e = c.call(kStoreInput, {}, ignored); e && log)
            log("road driver: the game's storing of the controls failed: " + e.message);
        return 0u;
    });
    if (!native)
        return Error::fail("the road driver: no room for a native function");
    cpu.write32(kStoreInputCall + 1, native - (kStoreInputCall + 5));
    return Error::success();
}

void RoadDriver::drive(GameHost &host, uint32_t car) {
    x86::Cpu &c = host.cpu();
    const uint64_t now = host.process().now();
    auto fixed = [&](uint32_t a) { return float(int32_t(c.read32(a))) / 65536.0f; };
    auto s8 = [&](uint32_t a) { return float(int8_t(c.read8(a))); };

    // Each lap, with the place then: one after the cars further round
    // (their laps and road points, as the game ranks them); after the
    // last, one after those over the line before it. The game's own
    // standings (0x4ffdec) are sorted a moment later, and a finished car
    // isn't asked for its controls again. The next race's count starting
    // again at 0 isn't a lap.
    const int laps = int(c.read32(car + kLaps));
    const bool lapped = laps != _laps && laps > 0;
    _laps = laps;
    if (lapped && log) {
        const uint32_t cars = std::min<uint32_t>(c.read32(kRaceCars), 16);
        const bool last = laps > 0 && uint32_t(laps) >= c.read32(kRaceLaps);
        uint32_t place = 1;
        for (uint32_t i = 0; i < cars; ++i) {
            const uint32_t other = c.read32(kCars + 4 * i);
            if (other != car)
                place += last ? c.read32(other + kFinished) == 2
                              : int32_t(c.read32(other + kProgress)) > int32_t(c.read32(car + kProgress));
        }
        log("road driver: lap " + std::to_string(laps) + " done at " + std::to_string(now) + " ms, " +
            (last ? "finished " : "") + std::to_string(place) + " of " + std::to_string(cars));
    }
    if (c.read32(car + kFinished) == 2)
        return;
    const uint32_t points = c.read32(kRoadPoints);
    const uint32_t n = c.read32(kRoadCount);
    if (now < _options.fromMs || now >= _options.toMs || !c.read32(kRaceRunning) || !points || n == 0 || n > 100000)
        return;

    const float x = fixed(car + kX);
    const float z = fixed(car + kZ);
    const float v = fixed(car + kSpeed);
    uint32_t slice = c.read32(car + kSlice) % n;
    auto point = [&](uint32_t i) { return points + 36 * (i % n); };
    auto distance = [&](uint32_t a, uint32_t b) {
        return std::hypot(fixed(a) - fixed(b), fixed(a + 8) - fixed(b + 8));
    };
    // Off the road where it passes near itself (Pacific Spirit's points 278
    // and 372, some 70 m apart), the game's road point for the car jumps
    // between the two as it moves; aimed at each by turns, it backed off
    // and on for half a minute. A jump of more than 30 points to one it's
    // far from is taken as that: it keeps the last.
    {
        const uint32_t q = point(slice);
        const float off = (x - fixed(q)) * s8(q + 18) / 128 + (z - fixed(q + 8)) * s8(q + 20) / 128;
        const int64_t jump = (int64_t(slice) - int64_t(_slice) + int64_t(n) * 3 / 2) % int64_t(n) - int64_t(n) / 2;
        if (_slice < n && std::abs(jump) > 30 && std::fabs(off) > 25.0f)
            slice = _slice;
        _slice = slice;
    }
    // The middle of the road at a point, from its distances to the edges
    // (col.md, The virtual road), or of its right side where it splits, and
    // right of it as asked: the points
    // themselves jog sideways where the edges change (TR00's 1190, 4.5 m),
    // the road's middle doesn't.
    struct Flat {
        float x;
        float z;
    };
    // Where the road splits round something (its drivable slots, each a
    // lane wide, with a gap between: col.md, Where a car can drive), the
    // middle of the drivable slots on its right, as the game's drivers keep
    // to their side (0x43a9a0); bit 7 the first right of the point, 6 the
    // next, 8 the first left. But a car already well inside those on the
    // left keeps to them: put there passing, it pressed against the
    // Outback's median at its point 1670, steering for the other side.
    auto splitSide = [&](uint32_t q, float &centre) {
        const uint32_t mask = c.read16(q + 24);
        const uint32_t low = mask & (0u - mask);
        if (mask == 0 || ((mask + low) & mask) == 0)
            return false;
        auto run = [&](uint32_t bitOf0, int step, int &first, int &last) {
            first = -1;
            last = -1;
            for (int k = 0; k < 8; ++k)
                if (mask & (step > 0 ? bitOf0 << k : bitOf0 >> k)) {
                    if (first < 0)
                        first = k;
                    last = k;
                } else if (first >= 0) {
                    break;
                }
        };
        int first = 0;
        int last = 0;
        run(0x100u, 1, first, last);
        const float left = float(c.read8(q + 32)) / 2;
        const float mine = (x - fixed(q)) * s8(q + 18) / 128 + (z - fixed(q + 8)) * s8(q + 20) / 128;
        if (first >= 0 && mine < -float(first) * left - 1.0f && mine > -float(last + 1) * left) {
            centre = -float(first + last + 1) / 2 * left;
            return true;
        }
        run(0x80u, -1, first, last);
        if (first < 0)
            return false;
        centre = float(first + last + 1) / 2 * float(c.read8(q + 33)) / 2;
        return true;
    };
    // How far from the point a car's middle can go across its lanes (byte
    // 31, each way's lanes; 32 and 33 their widths in half metres), a
    // little over half a car's width inside: the road's edges take in
    // verges and car parks (Pacific Spirit's at its point 275, signposts
    // standing in it).
    struct Lanes {
        float left;
        float right;
    };
    auto lanesAt = [&](uint32_t q) {
        const uint8_t lanes = c.read8(q + 31);
        Lanes room{float(lanes >> 4) * float(c.read8(q + 32)) / 2, float(lanes & 15) * float(c.read8(q + 33)) / 2};
        if (lanes == 0)
            room = Lanes{float(c.read16(q + 26)) / 256.0f, float(c.read16(q + 28)) / 256.0f};
        return Lanes{room.left - 1.2f, room.right - 1.2f};
    };
    auto across = [&](uint32_t q, float px, float pz) {
        return (px - fixed(q)) * s8(q + 18) / 128 + (pz - fixed(q + 8)) * s8(q + 20) / 128;
    };
    // Off the slots that stop being drivable over the next 12 points (a
    // gap in the word at 24, where something stands in the road: Mystic
    // Peaks' at its points 1797 and 1806), to the middle of the nearest
    // slot that stays drivable: at its edge, half a car and a little off,
    // it still caught them. The slots their widths at the point.
    auto clear = [&](uint32_t q, float off) {
        const uint32_t i = (q - points) / 36;
        uint32_t mask = 0xffff;
        for (uint32_t k = 0; k < 12; ++k)
            if (const uint32_t m = c.read16(point(i + k) + 24))
                mask &= m;
        const float right = float(c.read8(q + 33)) / 2;
        const float left = float(c.read8(q + 32)) / 2;
        if (mask == 0 || right <= 0 || left <= 0)
            return off;
        auto drivable = [&](float a) {
            const int bit = a >= 0 ? 7 - std::min(int(a / right), 7) : 8 + std::min(int(-a / left), 7);
            return (mask >> bit) & 1;
        };
        if (drivable(off - 1.5f) && drivable(off + 1.5f))
            return off;
        float best = off;
        float nearest = 1e9f;
        for (int k = 0; k < 8; ++k) {
            const float slots[2] = {(float(k) + 0.5f) * right, -(float(k) + 0.5f) * left};
            for (float a : slots)
                if (drivable(a) && std::fabs(a - off) < nearest) {
                    nearest = std::fabs(a - off);
                    best = a;
                }
        }
        return best;
    };
    // Moved over before a place it was stuck at (below), from 40 points
    // before it to 10 after: what it hit there isn't in the road's data
    // (Pacific Spirit's signposts at its point 278, in the middle of where
    // the road forks round its car park).
    auto moved = [&](uint32_t q) {
        const uint32_t i = (q - points) / 36;
        float shift = 0;
        for (const auto &[at, stop] : _slowAt)
            if ((at + n + 10 - i) % n <= 50)
                shift = stop.shift;
        return shift;
    };
    auto line = [&](uint32_t q) {
        float centre = 0;
        if (!splitSide(q, centre))
            centre = (float(c.read16(q + 28)) - float(c.read16(q + 26))) / 512.0f;
        return clear(q, centre + _options.right + moved(q)) + (now < _asideUntil ? _aside : 0.0f);
    };

    // Past a car ahead in its way, and faster or close: 3 m to the side of
    // its line nearer ours, if the lanes have room, moved over at 4 m/s
    // and held 1.5 s after it's no longer ahead, so as not to turn back
    // into it alongside; with room on neither side, behind it at its
    // speed. Aimed past it at one point instead, as far as the road's
    // edges, it swerved into Pacific Spirit's car park.
    float follow = kTopSpeed;
    {
        const uint32_t q = point(slice);
        const float fx = fixed(car + kForward);
        const float fz = fixed(car + kForward + 8);
        const float rx = fixed(car + kRight);
        const float rz = fixed(car + kRight + 8);
        const uint32_t cars = std::min<uint32_t>(c.read32(kRaceCars), 16);
        float nearest = 40.0f;
        uint32_t ahead = 0;
        for (uint32_t i = 0; i < cars; ++i) {
            const uint32_t other = c.read32(kCars + 4 * i);
            if (!other || other == car)
                continue;
            const float ox = fixed(other + kX) - x;
            const float oz = fixed(other + kZ) - z;
            const float along = ox * fx + oz * fz;
            const float side = ox * rx + oz * rz;
            if (along > 3.0f && along < nearest && std::fabs(side) < 2.5f &&
                (along < 15.0f || v > fixed(other + kSpeed) + 1.0f)) {
                nearest = along;
                ahead = other;
            }
        }
        if (ahead) {
            const Lanes lanes = lanesAt(q);
            const float at = across(q, fixed(ahead + kX), fixed(ahead + kZ));
            const float mine = across(q, x, z);
            const float right = at + 3.0f;
            const float left = at - 3.0f;
            const float home = line(q);
            const bool rightFits = right <= lanes.right;
            const bool leftFits = left >= -lanes.left;
            if (rightFits || leftFits) {
                const float to =
                    rightFits && (!leftFits || std::fabs(right - mine) < std::fabs(left - mine)) ? right : left;
                _passWanted = to - home;
                _passUntil = now + 1500;
            } else {
                follow = fixed(ahead + kSpeed);
            }
        }
        if (now >= _passUntil)
            _passWanted = 0;
        const float step = 4.0f * float(std::min<uint64_t>(now - std::min(now, _lastMs), 100)) / 1000.0f;
        _pass += std::clamp(_passWanted - _pass, -step, step);
        _lastMs = now;
    }
    auto middle = [&](uint32_t q) {
        float off = line(q);
        if (_pass != 0) {
            const Lanes lanes = lanesAt(q);
            off = std::clamp(off + _pass, -lanes.left, std::max(-lanes.left, lanes.right));
        }
        return Flat{fixed(q) + off * s8(q + 18) / 128, fixed(q + 8) + off * s8(q + 20) / 128};
    };

    // The road's way at a point, from its middle, not its points' forward
    // vectors: a few of those are far off (TR00's 1190, 50°).
    auto heading = [&](uint32_t i) {
        const Flat a = middle(point(i));
        const Flat b = middle(point(i + 2));
        return std::atan2(b.x - a.x, b.z - a.z);
    };
    auto turnBetween = [&](uint32_t a, uint32_t b) {
        float turn = std::fabs(heading(b) - heading(a));
        return turn > kPi ? 2 * kPi - turn : turn;
    };

    // Aim at the road a little ahead, farther the faster (the points are 2
    // to 5 m apart), but not past where it has turned 30°: aimed round a
    // hairpin, it cut across the inside into the wall (Pacific Spirit's
    // point 281).
    const float lookahead = std::clamp(10.0f + v * 0.6f, 10.0f, 48.0f);
    uint32_t j = slice;
    for (float along = 0; along < lookahead && j < slice + n; ++j) {
        if (j > slice + 1 && turnBetween(slice, j) > 0.52f)
            break;
        along += distance(point(j), point(j + 1));
    }
    const uint32_t p = point(j);
    Flat target = middle(p);
    const float dx = target.x - x;
    const float dz = target.z - z;
    const float angle = std::atan2(dx * fixed(car + kRight) + dz * fixed(car + kRight + 8),
                                   dx * fixed(car + kForward) + dz * fixed(car + kForward + 8));

    // As fast as the computer cars are told to go at each point, the
    // slowest over the next few (the controls reach the car a few frames
    // late); the road's own bends, if there's no such table.
    float wanted = kTopSpeed;
    if (const uint32_t pace = c.read32(kPace)) {
        for (uint32_t k = 0; k <= kPaceAhead; ++k) {
            const uint32_t at = pace + 2 * (((slice + k) % n) / 2);
            if (c.valid(at, 1))
                wanted = std::min(wanted, float(c.read8(at)));
        }
    } else {
        // Each bend's speed by its curve (the turn of the road over about
        // 15 m), and the braking there is room for before it.
        float along = 0;
        const float seen = v * v / (2 * kBraking) + 30.0f;
        for (uint32_t k = slice; along < seen && k < slice + n; ++k) {
            float span = 0;
            uint32_t m = k;
            while (span < 15.0f && m < k + 20) {
                span += distance(point(m), point(m + 1));
                ++m;
            }
            const float curve = turnBetween(k, m) / std::max(span, 1.0f);
            const float corner = curve > 1e-4f ? std::sqrt(kGrip * _options.grip / curve) : kTopSpeed;
            wanted = std::min(wanted, std::sqrt(corner * corner + 2 * kBraking * along));
            along += distance(point(k), point(k + 1));
        }
    }

    wanted = std::min(wanted, follow);
    // Where it was stuck before in this race, slower: at the speed it came
    // in at, less, and the braking there's room for before it (Mystic
    // Peaks' bends at its points 120 and 250, into the wall every lap).
    for (const auto &[at, stop] : _slowAt) {
        const float cap = stop.cap;
        const uint32_t ahead = (at + n - slice) % n;
        if (ahead > 60 && ahead < n - 10)
            continue;
        float along = 0;
        for (uint32_t k = slice; k < slice + std::min(ahead, 60u); ++k)
            along += distance(point(k), point(k + 1));
        wanted = std::min(wanted, std::sqrt(cap * cap + 2 * kBraking * along));
    }
    if (v > 15.0f) {
        _fastSpeed = v;
        _fastAt = now;
    }
    // On full lock and still well off its aim for 0.3 s, it's going too
    // fast to turn as the road does: the computer cars, which can't skid,
    // take some bends faster than a car can (Mystic Peaks' at its points
    // 120 and 250, into the outside wall at 63 and 30 m/s). Slowed till it
    // turns. Not at once: the aim jumps now and then, and slowed for each
    // jump the laps took longer.
    if (std::fabs(angle) > 0.45f && v > 15.0f) {
        if (!_lockSince)
            _lockSince = now;
        if (now > _lockSince + 300)
            wanted = std::min(wanted, v - 3.0f);
    } else {
        _lockSince = 0;
    }

    int steer = int(std::clamp(angle / 0.35f * 127.0f, -128.0f, 124.0f));
    int gas = v < wanted ? 255 : 0;
    int brake = v > wanted + 2 ? 255 : 0;
    uint8_t buttons = 0;
    const uint8_t gear = c.read8(car + kGear);
    // Stuck (against a wall, or turned round, or crawling along one under
    // 5 m/s, as against the Outback's median): back off for a while,
    // steering hard the other way, then on again a little to one side.
    // Not while backing off: going backwards, it's slow too, and would
    // only back off again.
    // Going on along the road again, the next back off starts afresh.
    const int32_t progress = int32_t(c.read32(car + kProgress));
    if (progress > _progressMark + 30 || progress < _progressMark - 100) { // or a new race
        _progressMark = progress;
        _backOffs = 0;
    }
    // No farther along the road for 3 s is stuck too: pushing against
    // something, its wheels spinning, its speed doesn't stay low.
    if (progress > _bestProgress || progress < _bestProgress - 100) {
        if (progress < _bestProgress - 100) // a new race
            _slowAt.clear();
        _bestProgress = progress;
        _advancedAt = now;
    }
    const bool noFurther = now > _advancedAt + 3000;
    if (now >= _reverseUntil && (std::fabs(angle) > kPi / 2 || v < 5.0f || noFurther)) {
        if (!_slowSince)
            _slowSince = now;
    } else {
        _slowSince = 0;
    }
    if (_slowSince && now > _slowSince + 1500 && now >= _reverseUntil && now > _options.fromMs + 4000) {
        // Each time, a little wider and longer, to either side by turns:
        // what it hit may stand in the road's middle (Mystic Peaks' rock
        // columns).
        constexpr float kAside[] = {3, -3, 6, -6, 9, -9};
        const int tries = _backOffs++;
        // Remembered, if it came in fast: there, 15% slower than then, or
        // than the last time it was stuck there, and 4 m to the other side
        // of the road's line from where it was, 2 m more each time after.
        if (tries == 0 && now < _fastAt + 3000) {
            const uint32_t q = point(slice);
            const float side = across(q, x, z) < line(q) ? 1.0f : -1.0f;
            Stop stop{0.85f * _fastSpeed, 4.0f * side};
            for (auto it = _slowAt.begin(); it != _slowAt.end();)
                if ((it->first + n + 10 - slice) % n <= 20) {
                    stop.cap = std::min(stop.cap, 0.85f * it->second.cap);
                    if (it->second.shift * side > 0)
                        stop.shift = it->second.shift + 2.0f * side;
                    it = _slowAt.erase(it);
                } else {
                    ++it;
                }
            _slowAt[slice] = stop;
        }
        _reverseUntil = now + 2500;
        _aside = kAside[tries % 6];
        _asideUntil = _reverseUntil + 4000 + 1000 * uint64_t(tries % 6);
        _slowSince = 0;
        // Given 3 s from the end of backing off to get past where it was.
        _bestProgress = progress;
        _advancedAt = _reverseUntil;
    }
    if (now < _reverseUntil) {
        if (gear != 0)
            buttons = kShiftDown;
        // Full lock the other way, so that backing off turns its nose to
        // the road: a little only backs it into what it hit again.
        steer = angle > 0 ? -128 : 124;
        gas = 255;
        brake = 0;
    } else if (gear < 2) {
        buttons = kShiftUp;
    }
    // A shift is taken as the button goes down: held, from 2 it stops at
    // neutral, short of reverse. So every other frame.
    if (buttons && _shifted)
        buttons = 0;
    _shifted = buttons != 0;
    c.write8(kControls, uint8_t(int8_t(steer)));
    c.write8(kControls + 1, uint8_t(gas));
    c.write8(kControls + 2, uint8_t(brake));
    c.write8(kControls + 3, buttons);
    if (log && now >= _logged + 1000) {
        _logged = now;
        char line[200];
        std::snprintf(line, sizeof line,
                      "road driver at %llu ms: slice %u, %.1f m/s (wanted %.1f), %.2f rad, steer %d gas %d brake %d"
                      " gear %u off %.1f pass %.1f",
                      (unsigned long long)now, slice, v, wanted, angle, steer, gas, brake, gear,
                      across(point(slice), x, z), _pass);
        log(line);
    }
}

} // namespace reluva::nfs::nfs2se
