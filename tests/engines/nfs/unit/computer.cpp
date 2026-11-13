// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

//   reluva-tests nfs.unit.computer

#include "tests/engines/nfs/fixtures.h"

using namespace reluva::nfs::testing;

namespace {

// The computer's cars in the air and landing (car_dynamics 0x4a2830 with
// +0x164 set, car_landing 0x4a1ce0, car_body_under_road 0x4a1210): on a
// straight level road along z, the road's frame set as aiRoadFrame would.
void testComputerAir() {
    const TrackCollision col = straightRoad(60);
    auto make = [&](float lift, const std::array<float, 3> &vel) {
        AiCar c;
        c.slice = c.velSlice = 20;
        c.along = 0.5f;
        c.halfWidth = 1.18f;
        c.halfHeight = 0.74f;
        c.halfLength = 2.41f;
        c.mass = 1672;
        c.sideKept = true;
        c.roadRight = {1, 0, 0};
        c.roadUp = {0, 1, 0};
        c.roadForward = {0, 0, 1};
        c.roadPoint = col.road[20].position;
        c.roadRightSet = c.roadUpSet = true;
        c.liftUp = c.roadUp;
        c.liftPoint = c.roadPoint;
        c.liftPlaneSet = true;
        c.lift = lift;
        c.airTicks = 1;
        aiSetWorld(c, col, vel, {0, 0, 1});
        return c;
    };
    // High in the air: gravity straight down (-0.3065625 a tick), the drag
    // (x and z only: 20^2 x 0.002 x |forward z| / 32 off the speed along
    // z), the air ticks count up and the spins are held to +-2.4.
    {
        AiCar c = make(3, {0, 0, 20});
        aiSteerStep(c, col, 2);
        CHECK(std::fabs(c.vel[1] + 0.3065625f) < 1e-6f && std::fabs(c.vel[2] - (20 - 400 * 0.002f / 32)) < 1e-4f &&
              c.vel[0] == 0 && c.airTicks == 2);
        AiCar spun = make(3, {0, 0, 20});
        spun.yawRate = 3;
        spun.spinX = -2.5f;
        spun.spinZ = 1;
        aiSteerStep(spun, col, 2);
        CHECK(spun.yawRate == 2.4f && spun.spinX == -2.4f && spun.spinZ == 1);
        // Moved down by the velocity / 64 each step: the clearance falls by
        // the moves along the road's up.
        aiSteerStep(c, col, 3);
        aiSteerStep(c, col, 4);
        CHECK(std::fabs(c.lift - (3 - 0.3065625f / 32)) < 0.01f && c.airTicks == 3);
        // Within 0.6 of the road, falling, nothing under it yet: the air
        // ticks stay and nothing but gravity and the drag acts.
        AiCar low = make(0.5f, {0, -1, 20});
        aiSteerStep(low, col, 2);
        CHECK(low.airTicks == 1 && std::fabs(low.vel[1] + 1.3065625f) < 1e-5f);
    }
    // Landing flat, falling at 4 m/s, the box 2 cm into the road: the
    // corners' impulses and the landing's x 0.6 take the speed into the
    // road down, never round (no bounce), and with every spin under 0.6
    // the air ticks end in the same tick; the on-road code takes the rest
    // the next.
    {
        AiCar c = make(-0.02f, {0, -4, 20});
        aiSteerStep(c, col, 2);
        CHECK(c.airTicks == 0 && c.vel[1] < 0 && c.vel[1] > -4 && std::fabs(c.spinX) < 0.6f &&
              std::fabs(c.yawRate) < 0.6f && std::fabs(c.spinZ) < 0.6f);
        CHECK(std::fabs(c.vel[2] - 20) < 0.5f && std::fabs(c.lift) < 0.01f);
        aiSteerStep(c, col, 3);
        aiSteerStep(c, col, 4);
        CHECK(c.airTicks == 0 && std::fabs(c.vel[1]) < 0.01f);
        // Moving away from the road (the speed along its up 0.5 or more)
        // it isn't over: no landing, the air ticks stay.
        AiCar up = make(-0.02f, {0, 1.5f, 20});
        aiSteerStep(up, col, 2);
        CHECK(up.airTicks == 1 && std::fabs(up.vel[1] - (1.5f - 0.3065625f)) < 1e-5f);
    }
    // The Suspension Stiffness (carp field 64, P +0x2E0) sets the landing's
    // scale 0.5 + 0.1 x it, kept within 0.5 and 0.7: a stiffer car keeps
    // more of its speed into the road.
    {
        AiCar soft = make(-0.02f, {0, -4, 20});
        AiCar stiff = soft;
        soft.suspStiffness = 0;
        stiff.suspStiffness = 5;
        aiSteerStep(soft, col, 2);
        aiSteerStep(stiff, col, 2);
        CHECK(stiff.vel[1] < soft.vel[1] && soft.vel[1] < 0);
        CHECK(std::fabs(stiff.vel[1] / soft.vel[1] - 0.7f / 0.5f) < 1e-3f);
    }
    // Tipped past 25 degrees on the road (+0x10C at most 0.9): put in the
    // air (car_flat_or_roof 0x49fde0), and the landing doesn't end it.
    {
        AiCar c = make(0, {0, 0, 10});
        c.airTicks = 0;
        c.roll = 0.1f; // 36 degrees
        aiSteerStep(c, col, 2);
        CHECK(c.airTicks == 1);
        for (int k = 3; k < 9; ++k)
            aiSteerStep(c, col, k);
        CHECK(c.airTicks >= 1);
    }
    // carp field 64 is read into the computer's data.
    {
        AiCarData d;
        std::string carp;
        for (int f = 0; f <= 76; ++f) {
            carp += "field(" + std::to_string(f) + ")\n";
            carp += f == 64 ? "1.5\n" : (f >= 67 && f <= 74 ? "1 1 1 1 1 1 1 1 1 1 1 1 1 1\n" : "1\n");
        }
        CHECK(parseCarpAi(carp, d) && d.suspStiffness == 1.5f);
    }
    // Gravity's up row (+0x19C) is the road polygon's up as car_road_dots
    // copied it at the last turn (0x4a2690, 0x4a0f30): a car whose last up
    // was tilted 0.01 rad towards its forward gains g x cos x sin along it
    // (the contact takes out only the part along the road's up now), and
    // the turn copies the up now for the next tick.
    {
        AiCar a = make(0, {0, 0, 20});
        a.airTicks = 0;
        AiCar b = a;
        b.gravityUp = {0, std::cos(0.01f), std::sin(0.01f)};
        b.gravityUpSet = true;
        aiSteerStep(a, col, 2);
        aiSteerStep(b, col, 2);
        CHECK(std::fabs((b.vel[2] - a.vel[2]) - (-0.30656251f * std::cos(0.01f) * std::sin(0.01f))) < 2e-5f);
        CHECK(b.gravityUpSet && b.gravityUp == b.roadUp && a.gravityUpSet);
    }
    // aiSetFrame: any forward and up come back from the yaw, roll and pitch.
    {
        AiCar c = make(0, {0, 0, 0});
        auto dot3 = [](const std::array<float, 3> &a, const std::array<float, 3> &b) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        auto unit3 = [&](const std::array<float, 3> &a) {
            const float l = std::sqrt(dot3(a, a));
            return std::array<float, 3>{a[0] / l, a[1] / l, a[2] / l};
        };
        const std::array<float, 3> up = unit3({-0.4942f, 0.7313f, -0.4701f});
        std::array<float, 3> fw = unit3({0.6840f, -0.0067f, -0.7294f});
        const float k = dot3(fw, up);
        fw = unit3({fw[0] - k * up[0], fw[1] - k * up[1], fw[2] - k * up[2]});
        aiSetFrame(c, col, fw, up);
        std::array<float, 3> f{};
        std::array<float, 3> r{};
        std::array<float, 3> u{};
        aiTiltedAxes(col, c, f, r, u);
        for (size_t i = 0; i < 3; ++i) {
            CHECK(std::fabs(f[i] - fw[i]) < 1e-4f && std::fabs(u[i] - up[i]) < 1e-4f);
        }
    }
    // In the air the body turns as 0x4a2cc0 turns its rows, about the
    // world's axes: rolled 45 degrees, a spin about the world's z (which
    // has a part along the body's up) turns its forward across the world
    // too, as the rows do.
    {
        AiCar c = make(3, {0, 0, 0});
        c.roll = 0.125f;
        c.spinX = 0.5f;
        std::array<float, 3> f0{};
        std::array<float, 3> r0{};
        std::array<float, 3> u0{};
        aiTiltedAxes(col, c, f0, r0, u0);
        aiSteerStep(c, col, 2);
        std::array<float, 3> f1{};
        std::array<float, 3> r1{};
        std::array<float, 3> u1{};
        aiTiltedAxes(col, c, f1, r1, u1);
        const float a = 0.5f * 2 * 3.14159265f / 32;
        auto turnX = [&](const std::array<float, 3> &v) {
            return std::array<float, 3>{v[0], v[1] * std::cos(a) - v[2] * std::sin(a),
                                        v[1] * std::sin(a) + v[2] * std::cos(a)};
        };
        const std::array<float, 3> fw = turnX(f0);
        const std::array<float, 3> up = turnX(u0);
        for (size_t i = 0; i < 3; ++i) {
            CHECK(std::fabs(f1[i] - fw[i]) < 1e-3f && std::fabs(u1[i] - up[i]) < 1e-3f);
        }
    }
    // On its roof car_on_road (0x4a1b18) takes the body's up on the side
    // it is on: a car rolled 170 degrees is turned on to its roof, not back
    // over; flat on its roof its spins about x and z are 0.
    {
        AiCar c = make(0, {0, 0, 0});
        c.airTicks = 0;
        c.roll = 0.47f;
        const float before = c.roll;
        aiTilt(c, 2, col);
        CHECK(c.roll > before);
        c.roll = 0.5f;
        c.spinX = c.spinZ = 0.3f;
        aiTilt(c, 4, col);
        CHECK(c.spinX == 0 && c.spinZ == 0);
    }
    // The reset (car_reset_timer 0x429260, car_reset_tipped 0x428f90): on
    // its roof and still, the timer starts at 700 and loses 1 + 8 a tick;
    // at 0 the car is put 5 slices on, 1.5 m up, still and in the air.
    {
        AiCar c = make(0, {0, 0, 0});
        c.airTicks = 0;
        c.roll = 0.5f;
        c.groundSpeed = 0;
        c.yawRate = 0.5f;
        std::vector<AiCar> v{c};
        aiImpacts(v, col, 1); // odd: nothing
        CHECK(v[0].resetTimer == 0);
        aiImpacts(v, col, 2);
        CHECK(v[0].resetTimer == 700);
        aiImpacts(v, col, 4);
        // On the ground past 60 degrees the spins lose 1/64 a tick.
        CHECK(v[0].resetTimer == 691 && v[0].yawRate == 0.5f - 0.5f * 0.015625f);
        for (int k = 6; v[0].resets == 0 && k < 400; k += 2)
            aiImpacts(v, col, k);
        CHECK(v[0].resets == 1 && v[0].slice == 25 && v[0].resetTimer == 0 && v[0].airTicks == 1 && v[0].lift == 1.5f &&
              v[0].vel == (std::array<float, 3>{0, 0, 0}) && v[0].roll == 0 && v[0].ghostTicks == 64 &&
              v[0].yawRate == 0);
        // Upright now: no new timer.
        aiImpacts(v, col, 400);
        CHECK(v[0].resetTimer == 0);
        // Moving (+0xB8 + |vy| at least 1) it loses only 1 a tick.
        AiCar m = c;
        m.groundSpeed = 2;
        std::vector<AiCar> w{m};
        aiImpacts(w, col, 2);
        aiImpacts(w, col, 4);
        CHECK(w[0].resetTimer == 699);
        // 0x428f90 leaves an upright car on the road where it is.
        AiCar u = make(0, {0, 0, 0});
        u.airTicks = 0;
        std::vector<AiCar> x{u};
        aiResetTipped(x, 0, col);
        CHECK(x[0].resets == 1 && x[0].slice == 20 && x[0].along == 0.5f);
        // Spinning about x at 1 turn/s or more it is put back.
        x[0].spinX = 1.0f;
        aiResetTipped(x, 0, col);
        CHECK(x[0].resets == 2 && x[0].slice == 25);
    }
    // 0x429100 raises the car 1.5 m too (0x5efdd0) and holds +0x524 at 0x20:
    // no other put back for 32 ticks.
    {
        AiCar c = make(0, {0, 0, 10});
        std::vector<AiCar> v{c};
        aiPutBack(v, 0, col);
        CHECK(v[0].slice == 25 && v[0].lift == 1.5f && v[0].putBackHold == 0x20);
        aiPutBack(v, 0, col);
        CHECK(v[0].slice == 25);
        for (int k = 2; k <= 64; k += 2)
            aiImpacts(v, col, k);
        CHECK(v[0].putBackHold == 0);
        aiPutBack(v, 0, col);
        CHECK(v[0].slice == 30);
    }
}

void testComputerGears() {
    // ai_gear_rpm (0x4061b0), made-up gearbox: reverse, neutral and three
    // gears, shift delay 3, minimum 1000 rpm, redline 6000.
    AiCarData::Gears g;
    g.gears = 5;
    g.shiftDelay = 3;
    g.rpmMin = 1000;
    g.redline = 6000;
    g.velocityToRpm = {-200, 0, 250, 150, 100, 0, 0, 0};
    AiGearbox box; // neutral, 0 rpm
    // Wanting 40 m/s, past the section's 22.35 + 10 and +0x7C4's 10 + 20:
    // the aim is the redline less 200; first's 2500 is nearest, so first,
    // the timer at the delay and the rpm an eighth of the way.
    aiGearRpm(box, g, 40, 10, 22.35f, false);
    CHECK(box.gear == 2 && box.timer == 3 && box.rpm == 312);
    // While the timer runs: 0.4 x the redline (past the section's band),
    // the timer down by one.
    aiGearRpm(box, g, 40, 10, 22.35f, false);
    CHECK(box.gear == 2 && box.timer == 2 && box.rpm == 312 + (2400 - 312) / 8);
    box = {2, 3000, 0};
    // Within 10 m/s of the section's speed the aim is half the redline:
    // at 22 m/s second (3300) is nearer 3000 than third (2200).
    aiGearRpm(box, g, 30, 22, 22.35f, false);
    CHECK(box.gear == 3 && box.timer == 3 && box.rpm == 3000 + (3300 - 3000) / 8);
    // Falling: an eighth of the way down, as an arithmetic shift.
    box = {4, 3000, 0};
    aiGearRpm(box, g, 30, 5, 22.35f, false);
    CHECK(box.gear == 2 && box.rpm == 3000 - (3000 - 1250) / 8);
    // Under the minimum the aim is the minimum; reversing is gear 0 at
    // -ratio x speed.
    box = {2, 900, 0};
    aiGearRpm(box, g, 30, 1, 22.35f, false);
    CHECK(box.rpm == 900 + (1000 - 900) / 8);
    box = {2, 1000, 0};
    aiGearRpm(box, g, 30, 10, 22.35f, true);
    CHECK(box.gear == 0 && box.timer == 3 && box.rpm == 1000 + (2000 - 1000) / 8);
}

void testComputerDriving() {
    TrackCollision col = straightRoad(200);
    const auto &p = col.road[0];
    // 0x40e2b0: 7 + offset / width, rounded to the nearest (even on a tie).
    CHECK(aiLaneAt(p, 1) == 7 && aiLaneAt(p, 2) == 7 && aiLaneAt(p, 2.1f) == 8 && aiLaneAt(p, 6) == 9);
    CHECK(aiLaneAt(p, -1) == 6 && aiLaneAt(p, -3) == 5 && aiLaneAt(p, 100) == 13);
    // The product offset x 1 / width goes to fistp unrounded: with 3.5 m
    // lanes (byte 7) 12.249999 m is 3.49999988 lanes (lane 10), which a
    // float product would round to 3.5 and so to 4 (lane 11).
    {
        TrackCollision::RoadPoint q = p;
        q.extra[2] = 7;
        CHECK(aiLaneAt(q, 12.249999046325684f) == 10 && aiLaneAt(q, 12.25f) == 11);
    }
    // 0x40daf0: lane middles, the lanes past the road a half width out,
    // kept within the walls.
    CHECK(aiLaneOffset(p, 7, 1) == 2 && aiLaneOffset(p, 8, 1) == 6 && aiLaneOffset(p, 6, 1) == -2);
    CHECK(aiLaneOffset(p, 9, 1) == 9 && aiLaneOffset(p, 9, 1.5f) == 8.5f && aiLaneOffset(p, 4, 1) == -9);
    CHECK(aiLaneOffset(p, 7, 1, 1) == 3);
    CHECK(aiLaneOpen(p, 5) && aiLaneOpen(p, 8) && !aiLaneOpen(p, 9) && !aiLaneOpen(p, 4));
    // The speed table: speeds for slice pairs, then a byte a slice.
    std::vector<uint8_t> speeds(100, 40);
    speeds.insert(speeds.end(), 200, 0x80); // the best line in lane 8
    speeds[100 + 4] = 0x50;                 // slice 4's in lane 5
    CHECK(aiLineLane(speeds, 3, 200) == 8 && aiLineLane(speeds, 4, 200) == 5 && aiLineLane({}, 3, 200) == 7);
    CHECK(aiLineLane(speeds, 4, 199) == 5); // (199 + 1) / 2 speeds
    // The drivers' factors (0x408dc0) and the tracks' (0x40e410).
    CHECK(aiDriverFactor({2, false, 2}, 0) == 1 &&
          std::fabs(aiDriverFactor({0, true, 4}, 128) - 0.94f * 1.05f) < 1e-6f);
    CHECK(std::fabs(aiDriverFactor({1, true, 0}, 384) - 0.97f * 0.95f * 0.95f) < 1e-6f);
    CHECK(aiTrackFactor(0) == 0.82f && aiTrackFactor(1) == 0.84f);

    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.topSpeedCap = 80;
    // One car: the best line's vote (10 for skill 2) takes it a lane
    // right, its aim 0.2 a call of the 32 Hz model (the even steps)
    // towards the lane's middle; there it stays. Its driver acts on the
    // steps the gate lets through (0x401240): (step / 2 + phase) mod 4 = 0
    // on the even ones, every 8 steps.
    CHECK(aiDriverActs(AiCar{}, 0) && aiDriverActs(AiCar{}, 8) && !aiDriverActs(AiCar{}, 1) &&
          !aiDriverActs(AiCar{}, 2) && !aiDriverActs(AiCar{}, 9));
    AiCar car;
    car.slice = 10;
    car.halfWidth = 1;
    car.driver = {2, false, 2};
    car.offset = 2;
    car.speed = 40;
    std::vector<AiCar> cars{car};
    aiStep(cars, 0, col, speeds, data, 0, 1, false);
    CHECK(cars[0].pick == 8 && cars[0].target == 6 && std::fabs(cars[0].offset - 2.2f) < 1e-5f);
    aiStep(cars, 0, col, speeds, data, 1, 1, false); // an odd step: the aim stays
    CHECK(std::fabs(cars[0].offset - 2.2f) < 1e-5f);
    for (int step = 2; step < 80; ++step)
        aiStep(cars, 0, col, speeds, data, step, 1, false);
    // At the race's start (0x40f050): 400 slices with 5 or more racers,
    // 200 with 2-4, 50 with one, 26 less backwards, the place x 14 added.
    CHECK(aiAtRaceStart(400, 0, 8) && !aiAtRaceStart(401, 0, 8) && aiAtRaceStart(302, 7, 8) &&
          !aiAtRaceStart(303, 7, 8));
    CHECK(aiAtRaceStart(200, 0, 2) && !aiAtRaceStart(201, 0, 4) && !aiAtRaceStart(51, 0, 1));
    CHECK(!aiAtRaceStart(375, 0, 8, true) && aiAtRaceStart(374, 0, 8, true) && aiAtRaceStart(474, 0, 8, false, 100));
    {
        // There the best line's vote is left out: the car keeps its lane
        // (keeping right gives lane 8 only +1 against its own lane's +1).
        std::vector<AiCar> early{car};
        early[0].distance = 100;
        aiStep(early, 0, col, speeds, data, 0, 1, false);
        CHECK(early[0].pick == 7 && early[0].target == 2);
        early[0].distance = 401;
        aiStep(early, 0, col, speeds, data, 8, 1, false);
        CHECK(early[0].pick == 8);
    }
    // In lane 8, with no road strip in lane 9 beside it (0x40d1f0), it is
    // nudged a half width left of the lane's middle.
    CHECK(cars[0].pick == 8 && cars[0].lane == 8 && cars[0].offset == 5 && !cars[0].braking);
    // Too fast for the table: the brakes (x 1.25 for an AI racer), held
    // only while more than 5 m/s over.
    cars[0].speed = 60;
    aiStep(cars, 0, col, speeds, data, 80, 1, false);
    CHECK(cars[0].braking && std::fabs(cars[0].speed - (60 - 12.5f / 32)) < 1e-4f && cars[0].wanted == 40);
    cars[0].speed = 44;
    aiStep(cars, 0, col, speeds, data, 82, 1, false);
    CHECK(!cars[0].braking && cars[0].speed < 44);
    // Slower: the acceleration table (x 0.9 with ai Normal), over 1/32 s.
    cars[0].speed = 30;
    aiStep(cars, 0, col, speeds, data, 84, 1, false);
    CHECK(std::fabs(cars[0].speed - (30 + 3.6f / 32)) < 1e-4f);
    {
        // Not past race distance 100 (0x406591): the table as it is.
        std::vector<AiCar> start{cars[0]};
        start[0].speed = 30;
        start[0].distance = 100;
        aiStep(start, 0, col, speeds, data, 84, 1, false);
        CHECK(std::fabs(start[0].speed - (30 + 4.0f / 32)) < 1e-4f);
        // The speed wanted (0x409330): the table's 40 held under the cap x
        // the track's factor (25 x 0.84 = 21) before the scale (0.5): 10.5,
        // not the scaled 20 under 21.
        AiCarData capped = data;
        capped.topSpeedCap = 25;
        start[0].scale = 0.5f;
        start[0].distance = 1 << 20;
        aiStep(start, 0, col, speeds, capped, 88, 1, false);
        CHECK(std::fabs(start[0].wanted - 21.0f * 0.5f) < 1e-4f);
    }
    {
        // The force model (ai_ctl_rails 0x4076b0, ai_integrate 0x4071a0):
        // nothing holds the speed at the speed wanted, so a car just under
        // it goes past it by the acceleration and is braked back.
        std::vector<AiCar> one{cars[0]};
        one[0].speed = 39.99f;
        aiStep(one, 0, col, speeds, data, 84, 1, false);
        CHECK(one[0].wanted == 40 && std::fabs(one[0].speed - (39.99f + 3.6f / 32)) < 1e-4f);
        aiStep(one, 0, col, speeds, data, 86, 1, false);
        CHECK(!one[0].braking && std::fabs(one[0].speed - (39.99f + 3.6f / 32 - 12.5f / 32)) < 1e-4f);
        // Accelerating over 44.703 m/s a racer's drive loses 3.45 / its
        // lateral grip multiplier (0x536364).
        std::vector<uint8_t> fast(100, 60);
        fast.insert(fast.end(), 200, 0x80);
        AiCarData grip = data;
        grip.latGrip = 3.45f / 2;
        one[0].speed = 45;
        aiStep(one, 0, col, fast, grip, 88, 1, false);
        CHECK(one[0].wanted == 60 && std::fabs(one[0].speed - (45 + (3.6f - 2) / 32)) < 1e-4f);
        // Gravity (0x4a2a0c): -0.30656 m/s a tick x the body's forward's
        // rise (+0x1A8, the body's own row for a car upright on the road),
        // the way the car runs: lined up with a 10 % slope it loses 0.0306
        // m/s more a tick, running the other way it gains it.
        TrackCollision hill = col;
        for (auto &point : hill.road) {
            const float k = 1 / std::sqrt(1.01f);
            point.forward = {0, 0.1f * k, k};
            point.normal = {0, k, -0.1f * k};
        }
        std::vector<AiCar> up{cars[0]};
        up[0].speed = 30;
        aiStep(up, 0, hill, speeds, data, 84, 1, false);
        CHECK(std::fabs(up[0].speed - (30 + 3.6f / 32 - 0.3065625f * 0.1f / std::sqrt(1.01f))) < 1e-4f);
        up[0].way = -1;
        up[0].speed = 30;
        aiStep(up, 0, hill, speeds, data, 86, 1, false);
        CHECK(std::fabs(up[0].speed - (30 + 3.6f / 32 + 0.3065625f * 0.1f / std::sqrt(1.01f))) < 1e-4f);
        // The body's up is the world's: a car left level where the road
        // climbs (within 0.02 of it, car_on_road 0x4a1610 doesn't turn it)
        // is pulled by its own forward's rise, not the road's.
        TrackCollision gentle = col;
        for (auto &point : gentle.road) {
            const float k = 1 / std::sqrt(1 + 0.015f * 0.015f);
            point.forward = {0, 0.015f * k, k};
            point.normal = {0, k, -0.015f * k};
        }
        std::vector<AiCar> level{cars[0]};
        level[0].speed = 30;
        aiStep(level, 0, gentle, speeds, data, 84, 1, false);
        const float onRoad = level[0].speed;
        level[0] = cars[0];
        level[0].speed = 30;
        aiSetTilt(level[0], gentle, {0, 1, 0});
        aiStep(level, 0, gentle, speeds, data, 84, 1, false);
        CHECK(std::fabs(onRoad - (30 + 3.6f / 32 - 0.3065625f * 0.015f / std::sqrt(1 + 0.015f * 0.015f))) < 1e-4f);
        CHECK(std::fabs(level[0].speed - (30 + 3.6f / 32)) < 2e-4f && level[0].pitchRate == 0);
        // Past 0.02 the body turns towards the road (car_on_road 0x4a1b18):
        // level on a 5 % slope, its pitch against the road shrinks.
        TrackCollision steep = col;
        for (auto &point : steep.road) {
            const float k = 1 / std::sqrt(1 + 0.05f * 0.05f);
            point.forward = {0, 0.05f * k, k};
            point.normal = {0, k, -0.05f * k};
        }
        AiCar flat = cars[0];
        aiSetTilt(flat, steep, {0, 1, 0});
        const float before = flat.pitch;
        aiTilt(flat, 2, steep);
        CHECK(before != 0 && flat.pitchRate != 0 && std::fabs(flat.pitch) < std::fabs(before));
    }

    // Held up behind a car in its lane within 2 slices, wanting more than
    // 8.94 m/s over its speed: its lights a step up for 64 steps (0x40de50),
    // its speed wanted the other's (0x4094d0).
    AiCar ahead = cars[0];
    ahead.slice = cars[0].slice + 1; // aiStep moves the car along (aiAdvance)
    ahead.speed = 20;
    cars[0].speed = 20;
    cars[0].beams = 1;
    cars.push_back(ahead);
    aiStep(cars, 0, col, speeds, data, 104, 1, false);
    CHECK(cars[0].flashing && cars[0].beams == 2 && cars[0].wanted == 20);
    cars[1].slice = 100; // gone
    for (int step = 105; step <= 175; ++step)
        aiStep(cars, 0, col, speeds, data, step, 1, false);
    CHECK(cars[0].flashing && cars[0].beams == 2); // its 64 steps were up at 168, its next call 176
    aiStep(cars, 0, col, speeds, data, 176, 1, false);
    CHECK(!cars[0].flashing && cars[0].beams == 1);
    // By day the lights come on for the flash.
    cars[0].beams = 0;
    cars[0].speed = 20;
    cars[1].slice = cars[0].slice + 1;
    aiStep(cars, 0, col, speeds, data, 200, 1, false);
    CHECK(cars[0].beams == 1);
    // Not at the race's start (0x404840 asks 0x40f050).
    {
        std::vector<AiCar> early = cars;
        early[0].beams = 0;
        early[0].flashing = false;
        early[0].speed = 20;
        early[0].distance = 0;
        aiStep(early, 0, col, speeds, data, 208, 1, false);
        CHECK(!early[0].flashing && early[0].beams == 0 && early[0].wanted == 20);
    }
    // A car going the other way keeps to its own right (lanes under 7).
    std::vector<AiCar> oncoming{car};
    oncoming[0].way = -1;
    oncoming[0].slice = 150;
    for (int step = 1; step < 80; ++step)
        aiStep(oncoming, 0, col, speeds, data, step, 1, false);
    CHECK(oncoming[0].lane < 7);

    // Cover (0x420bf0) and how far it reaches (0x4ba350).
    TrackCollision tunnel = straightRoad(40);
    for (int i = 10; i <= 20; ++i)
        tunnel.road[size_t(i)].unknown = (tunnel.road[size_t(i)].unknown & 0xffff) | 0x22u << 16;
    CHECK(pointCovered(tunnel.road[15], {1, 0, 60}) && !pointCovered(tunnel.road[5], {1, 0, 20}));
    CHECK(coverDepth(tunnel, 15, {1, 0, 60}) == 6 && coverDepth(tunnel, 11, {1, 0, 44}) == 2 &&
          coverDepth(tunnel, 5, {1, 0, 20}) == 0);
    TrackCollision::RoadPoint half = col.road[0];
    half.unknown = 0x20u << 16; // covered on the left only; the right's 0 takes the left's
    CHECK(pointCovered(half, {-1, 0, 0}) && pointCovered(half, {1, 0, 0}));
    half.unknown = 0x24u << 16;
    CHECK(pointCovered(half, {-1, 0, 0}) && !pointCovered(half, {1, 0, 0}));
    // The road polygons' surfaces (byte 1 & 0x3F of their records).
    Track::Block block;
    block.polygonRoad = {0, 0x41, 0, 0, 0, 0, 0, 0, 0, 14};
    CHECK(roadSurface(block, 0) == 1 && roadSurface(block, 1) == 14 && roadSurface(block, 2) == 0);
    // A slice's run of road polygons (the positions record: first, count)
    // and where a point is across it (the track position's +0x6C): three
    // polygons side by side, 4 wide, from x -6 to 6; polygon i's edge 0-3
    // is the one it shares with i - 1, 1-2 with i + 1.
    Track runTrack;
    runTrack.blocks.resize(1);
    Track::Block &run = runTrack.blocks[0];
    for (int i = 0; i < 4; ++i) {
        const float x = -6.0f + 4.0f * static_cast<float>(i);
        run.vertices.push_back({x, 0, 4}); // front
        run.vertices.push_back({x, 0, 0}); // back
    }
    for (uint16_t i = 0; i < 3; ++i) {
        Track::Polygon poly;
        // 0 front left, 1 front right, 2 back right, 3 back left
        poly.vertices = {uint16_t(2 * i), uint16_t(2 * i + 2), uint16_t(2 * i + 3), uint16_t(2 * i + 1)};
        run.polygons[4].push_back(poly);
    }
    run.positions = {0, 0, 3, 0, 0xff, 0xff, 0xff, 0xff};
    run.polygonRoad = {0, 1, 0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0};
    size_t found = 99;
    CHECK(sliceRunSide(run, 0, {0, 0, 2}, found) == 0 && found == 1);
    CHECK(sliceRunSide(run, 0, {-5, 0, 2}, found) == 0 && found == 0);
    CHECK(sliceRunSide(run, 0, {5, 0, 2}, found) == 0 && found == 2);
    CHECK(sliceRunSide(run, 0, {-7, 0, 2}, found) == 1 && found == 0);
    CHECK(sliceRunSide(run, 0, {9, 0, 2}, found) == 2 && found == 2);
    CHECK(sliceRunSide(run, 1, {0, 0, 2}, found) == -1); // no record for slice 1
    // The track position's search (0x41e990) over the same run: found in
    // polygons 0 and 2, not in 1 (surface 14); beyond the run's ends side 1
    // (0x41ee86) and side 2 (0x41ebd7), nothing found at the other slices
    // (they have no records).
    {
        RoadSearch rs;
        CHECK(roadSearch(runTrack, rs, 5, 2) && rs.foundSlice == 0 && rs.polygon == 2 && rs.index == 2);
        rs = {};
        CHECK(roadSearch(runTrack, rs, -5, 2) && rs.polygon == 0 && rs.side == 0);
        rs = {};
        CHECK(!roadSearch(runTrack, rs, 9, 2) && rs.side == 2);
        rs = {};
        CHECK(!roadSearch(runTrack, rs, -7, 2) && rs.side == 1);
        rs = {};
        CHECK(!roadSearch(runTrack, rs, 0, 2) && rs.side == 0 && !rs.found);
        // A walk goes on past a polygon farther than the one before only
        // when it is of surface 14 (0x41ecd2): the run's polygon 2 is far
        // off and of surface 14, so the walk up from polygon 1 goes on to
        // 3, which holds the point.
        Track far;
        far.blocks.resize(1);
        Track::Block &fb = far.blocks[0];
        for (float x : {-2.0f, 2.0f, 6.0f, 20.0f, 24.0f, -6.0f}) {
            fb.vertices.push_back({x, 0, 4});
            fb.vertices.push_back({x, 0, 0});
        }
        auto quad = [&](uint16_t left, uint16_t right) {
            Track::Polygon q;
            q.vertices = {uint16_t(2 * left), uint16_t(2 * right), uint16_t(2 * right + 1), uint16_t(2 * left + 1)};
            fb.polygons[4].push_back(q);
        };
        quad(0, 1); // x -2..2
        quad(1, 2); // 2..6, the start ((4 - 1) / 2)
        quad(3, 4); // 20..24, surface 14
        quad(5, 0); // -6..-2
        fb.positions = {0, 0, 4, 0, 0xff, 0xff, 0xff, 0xff};
        fb.polygonRoad = {0, 1,  0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                          0, 14, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
        rs = {};
        CHECK(roadSearch(far, rs, -4, 2) && rs.polygon == 3 && rs.foundSlice == 0);
        fb.polygonRoad[17] = 4; // the far polygon a road one: the walk stops there
        rs = {};
        CHECK(!roadSearch(far, rs, -4, 2));
        // The down walk starts from the first slice's start less one, held
        // to nothing (0x41ecf6): from slice 0 (5 polygons, start 2) to
        // slice 1 (1 polygon) it reads polygon 1 of slice 1's run, which is
        // slice 2's first, and finds the point there.
        Track rows;
        rows.blocks.resize(1);
        Track::Block &rb = rows.blocks[0];
        auto rowQuad = [&](float x0, float x1, float z0) {
            const uint16_t v = static_cast<uint16_t>(rb.vertices.size());
            rb.vertices.push_back({x0, 0, z0 + 4});
            rb.vertices.push_back({x1, 0, z0 + 4});
            rb.vertices.push_back({x1, 0, z0});
            rb.vertices.push_back({x0, 0, z0});
            Track::Polygon q;
            q.vertices = {v, uint16_t(v + 1), uint16_t(v + 2), uint16_t(v + 3)};
            rb.polygons[4].push_back(q);
            for (uint8_t byte : {0, 1, 0, 0, 0, 0, 0, 0})
                rb.polygonRoad.push_back(byte);
        };
        for (int i = 0; i < 5; ++i)
            rowQuad(-10.0f + 4.0f * static_cast<float>(i), -6.0f + 4.0f * static_cast<float>(i), 0);
        rowQuad(-2, 2, 4);
        rowQuad(-2, 2, 8);
        rb.positions = {0,    0,    5,    0,    0xff, 0xff, 0xff, 0xff, 5,    0,    1,    0,
                        0xff, 0xff, 0xff, 0xff, 6,    0,    1,    0,    0xff, 0xff, 0xff, 0xff};
        rs = {};
        CHECK(roadSearch(rows, rs, 0, 10) && rs.foundSlice == 1 && rs.polygon == 6 && rs.index == 1 && rs.pastRun == 1);
        // From slice 2 itself it is found where it is.
        rs = {};
        rs.slice = 2;
        CHECK(roadSearch(rows, rs, 0, 10) && rs.foundSlice == 2 && rs.polygon == 6 && rs.index == 0 && rs.pastRun == 0);
    }
    // The officer's spot (0x401d78): kept on polygons 0 (surface 1) and 2
    // (4), not on 1 (surface 14) or off the run.
    CHECK(officerSpotOnRoad(runTrack, 0, {-5, 0, 2}) && officerSpotOnRoad(runTrack, 0, {5, 0, 2}));
    CHECK(!officerSpotOnRoad(runTrack, 0, {0, 0, 2}) && !officerSpotOnRoad(runTrack, 0, {-8, 0, 2}));
    // The put back's road test (0x428ad0 after 0x4205a0, 0x4207e0): a
    // polygon found from the slice, its byte 1 without 0x40 and 0x80 and a
    // surface not 0 or 14, its road vector's up y above 0.
    {
        Track spot = runTrack;
        Track::Block &sb = spot.blocks[0];
        sb.virtualRoad.assign(24, 0);
        sb.virtualRoad[2] = 0xff; // record 0: up (0, 0x7FFF, 0), forward (0, 0, 0x7FFF)
        sb.virtualRoad[3] = 0x7f;
        sb.virtualRoad[10] = 0xff;
        sb.virtualRoad[11] = 0x7f;
        sb.virtualRoad[14] = 0x01; // record 1: up (0, -0x7FFF, 0)
        sb.virtualRoad[15] = 0x80;
        CHECK(aiSpotOnRoad(spot, 0, {-5, 0, 2}) && aiSpotOnRoad(spot, 0, {5, 0, 2}));
        CHECK(!aiSpotOnRoad(spot, 0, {0, 0, 2}) && !aiSpotOnRoad(spot, 0, {-8, 0, 2}));
        sb.polygonRoad[17] = 0x44; // polygon 2 flagged 0x40
        CHECK(!aiSpotOnRoad(spot, 0, {5, 0, 2}));
        sb.polygonRoad[17] = 4;
        sb.polygonRoad[16] = 1; // polygon 2 on record 1, upside down
        CHECK(!aiSpotOnRoad(spot, 0, {5, 0, 2}));
    }

    // carp.txt's fields for the computer.
    std::string carp =
        "maximum velocity of car [m/s](14)\n77.5\ntop speed cap [m/s](15)\n78.23\nmaximum braking deceleration(18)\n10.06\n";
    for (int row = 0; row < 8; ++row) {
        carp += "AI ACC" + std::to_string(row) + " acceleration table section(" + std::to_string(67 + row) + ")\n";
        for (int i = 0; i < 14; ++i)
            carp += std::to_string(row * 14 + i) + ".5,";
        carp += "\n";
    }
    AiCarData parsed;
    CHECK(parseCarpAi(carp, parsed) && std::fabs(parsed.topSpeedCap - 78.23f) < 1e-4f &&
          std::fabs(parsed.maxBrake - 10.06f) < 1e-4f);
    CHECK(parsed.accel[0] == 0.5f && parsed.accel[111] == 111.5f);
    CHECK(parsed.latGrip == 0); // field 30 left out: no drive loss over 44.703 m/s by it
    CHECK(parseCarpAi(carp + "lateral acceleration grip multiplier(30)\n3.300000\n", parsed) &&
          std::fabs(parsed.latGrip - 3.3f) < 1e-6f);
    CHECK(!parseCarpAi("maximum braking deceleration(18)\n10\n", parsed));
}

void testComputerCatchUpAndCops() {
    // The track sections' speeds (0x40ed40), the switch falling through.
    CHECK(trackSectionSpeed(0, 0x200) == 13.4109f && trackSectionSpeed(0, 10) == 22.3515f);
    CHECK(trackSectionSpeed(1, 0x317) == 31.2921f && trackSectionSpeed(1, 0xab) == 29.05695f &&
          trackSectionSpeed(1, 5) == 24.58665f);
    CHECK(trackSectionSpeed(4, 0x130) == 8.9406f && trackSectionSpeed(4, 5) == 22.3515f);
    CHECK(trackSectionSpeed(8, 500) == 15.64605f && trackSectionSpeed(8, 5) == 20.11635f);
    // The catch-up (0x4090a0), as nfs host's dumps of Hometown have it
    // (+0x7BC): the human behind them all, places 2-5 x 0.9 and 6-8 x
    // 0.8; some ahead, one 50 or more slices ahead x 0.85; none ahead,
    // one over 15 behind x 1.1.
    CHECK(aiRacersAhead(1, {85, 87, 92}) == 2 && aiRacersAhead(230, {236, 84}) == 1 && aiRacersAhead(196, {40}) == 0);
    CHECK(aiRacersAhead(0, {}) == 0);
    AiRace race;
    race.humanDistance = 1;
    race.ahead = 2;
    CHECK(aiCatchUpFactor(race, 92, 5) == 0.9f && aiCatchUpFactor(race, 88, 6) == 0.8f);
    race.raceType = 3;
    CHECK(aiCatchUpFactor(race, 88, 6) == 0.85f);
    race = {};
    race.humanDistance = 230;
    race.ahead = 1;
    CHECK(aiCatchUpFactor(race, 236, 1) == 1 && aiCatchUpFactor(race, 280, 1) == 0.85f &&
          aiCatchUpFactor(race, 279, 1) == 1);
    race.ahead = 0;
    race.humanDistance = 196;
    CHECK(aiCatchUpFactor(race, 40, 8) == 1.1f && aiCatchUpFactor(race, 181, 2) == 1);
    race.raceType = 2;
    race.expert = true;
    CHECK(aiCatchUpFactor(race, 190, 2) == 1.15f);
    race.link = 2;
    CHECK(aiCatchUpFactor(race, 40, 8) == 1);
    race = {};
    race.human = false;
    CHECK(aiCatchUpFactor(race, 0, 8) == 1);

    // A computer cop (0x4032e0): it acts every 8 steps on its phase.
    TrackCollision col = straightRoad(200);
    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.maxVelocity = 70;
    AiCar cop;
    cop.cop = true;
    cop.racer = false;
    cop.slice = 100;
    cop.halfWidth = 1;
    cop.offset = cop.target = 2;
    cop.speed = 10;
    cop.wanted = 20;
    std::vector<AiCar> cars{cop};
    aiCopStep(cars, 0, col, data, 2, 1, false); // (2 / 2 + 0) & 3 = 1: not its call
    CHECK(cars[0].copState == 0 && std::fabs(cars[0].speed - (10 + 3.6f / 32)) < 1e-4f);
    aiCopStep(cars, 0, col, data, 8, 1, false);
    // Patrolling on track 1's slice 100: 24.59 x 1.19.
    CHECK(cars[0].copState == 1 && std::fabs(cars[0].wanted - 24.58665f * 1.19f) < 1e-4f);
    // With the siren: x 2, at most its top speed.
    cars[0].copState = 7;
    aiCopStep(cars, 0, col, data, 16, 1, false);
    CHECK(std::fabs(cars[0].wanted - 24.58665f * 2) < 1e-4f);
    // A racer within 16 slices that has been over the section's speed by
    // more than 2 m/s (+0x6F0, brought up to date as the cop looks,
    // 0x4014b0) becomes its target (0x403f80), and the chase begins (5).
    AiCar racer;
    racer.slice = 110;
    racer.halfWidth = 1;
    racer.offset = racer.target = 2;
    racer.speed = 24.58665f + 1.9f;
    cars.push_back(racer);
    aiCopStep(cars, 0, col, data, 24, 1, false);
    CHECK(cars[0].copState == 7 && cars[0].chasing == -1 && std::fabs(cars[1].overspeed - 1.9f) < 1e-4f);
    cars[1].speed = 24.58665f + 2.1f;
    aiCopStep(cars, 0, col, data, 32, 1, false);
    CHECK(cars[0].copState == 5 && cars[0].chasing == 1 && cars[0].siren && cars[1].recordOpen &&
          cars[1].chaseFrom == 32 && cars[1].callAt == 32 + 640);
    // The most it was over stays when it slows (+0x6F0 is a maximum).
    cars[1].speed = 10;
    aiCopStep(cars, 0, col, data, 40, 1, false);
    CHECK(std::fabs(cars[1].overspeed - 2.1f) < 1e-4f);
    cars[1].speed = 24.58665f + 2.1f;
    // The chase's speed (0x402420): 10 slices behind, the top speed;
    // within 8, closing: slices x (top - its speed) / 8 + its speed + 5.81.
    aiCopStep(cars, 0, col, data, 40, 1, false);
    CHECK(cars[0].wanted == 70);
    cars[0].slice = 106;
    aiCopStep(cars, 0, col, data, 48, 1, false);
    CHECK(std::fabs(cars[0].wanted - (4 * (70 - cars[1].speed) * 0.125f + cars[1].speed + 5.81139f)) < 1e-3f);
    // Turned to a target coming the other way (0x402910), lost past 50.
    cars[1].way = -1;
    aiCopStep(cars, 0, col, data, 56, 1, false);
    CHECK(cars[0].way == -1);
    cars[1].slice = 170;
    aiCopStep(cars, 0, col, data, 64, 1, false);
    CHECK(cars[0].copState == 1 && cars[0].chasing == -1 && !cars[0].siren);

    // The side offset as driven (+0x7B8) follows the aim, lagging it,
    // and the lanes are taken there (0x40e350).
    AiCar weaver;
    weaver.slice = 50;
    weaver.halfWidth = 1;
    weaver.offset = weaver.target = 2;
    weaver.speed = 30;
    weaver.wanted = 30;
    std::vector<AiCar> lone{weaver};
    std::vector<uint8_t> flat(100, 30);
    aiStep(lone, 0, col, flat, data, 1, 1, false); // the first step takes the aim as it
    CHECK(lone[0].sideKept && lone[0].side == 2);
    lone[0].offset = 5.9f;
    lone[0].target = 5.9f;
    aiStep(lone, 0, col, flat, data, 2, 1, false);
    CHECK(lone[0].side > 2 && lone[0].side < 2.2f && lone[0].lane == 7);
    // Then it weaves about lane 7's middle as the game's cars do: the
    // controller overshoots past 2 m, the lane at the offset (rounded at
    // the lane's middle) flips to 8 and back, and the aim with it.
    float lo = 100;
    float hi = -100;
    int flips = 0;
    int last = lone[0].lane;
    for (int step = 3; step < 400; ++step) {
        aiStep(lone, 0, col, flat, data, step, 1, false);
        if (step > 100) {
            lo = std::min(lo, lone[0].side);
            hi = std::max(hi, lone[0].side);
            flips += lone[0].lane != last ? 1 : 0;
        }
        last = lone[0].lane;
    }
    CHECK(lo > 0 && hi < 4.5f && flips >= 2);

    // A dump of the game's car records (0x9AC bytes each).
    std::vector<uint8_t> dump(2 * 0x9ac, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(dump.data() + at, &v, 4); };
    auto putF = [&](size_t at, float v) { std::memcpy(dump.data() + at, &v, 4); };
    dump[0x89] = 1;
    put32(0x200, 5);
    put32(0x1c, 230);
    putF(0x98, 472.5f);
    putF(0xd8 + 8, 1);
    put32(0x660, 2);
    const size_t b = 0x9ac;
    dump[b + 0x89] = 1;
    put32(b + 0x200, 0x2b);
    put32(b + 0x734, 1);
    dump[b + 0x509] = 255;
    put32(b + 0x944, 0x1801);
    putF(b + 0x7c4, 31.5f);
    // The officer on foot beside the second car (+0x8E4, +0x8E8, +0x8EC rows, +0x910).
    put32(b + 0x8e4, 1);
    put32(b + 0x8e8, 1);
    putF(b + 0x904 + 8, 1);
    putF(b + 0x8ec, 1);
    putF(b + 0x910, 742.75f);
    putF(b + 0x918, -266.25f);
    // Its tyres (+0x5C0, +0x5C4, +0x164, +0x10C, +0x160, +0x18C, the wheels' 16.16 points).
    putF(b + 0x5c0, 0.5f);
    putF(b + 0x5c4, 12.0f);
    putF(b + 0x10c, 1.0f);
    put32(b + 0x18c, 0x41);
    putF(b + 0xa4, 3.0f);
    put32(b + 0x304 + 2 * 0x78, static_cast<uint32_t>(1302 * 65536 + 32768));
    put32(b + 0x304 + 2 * 0x78 + 8, static_cast<uint32_t>(-951 * 65536));
    put32(0x164, 3);
    const std::vector<DumpedCar> parsedCars = parseCarDump(dump);
    CHECK(parsedCars[1].frontSkid == 0.5f && parsedCars[1].rearSkid == 12.0f && parsedCars[1].upright == 1 &&
          !parsedCars[1].airborne && parsedCars[0].airborne && parsedCars[1].surface == 1 &&
          parsedCars[1].velocity[0] == 3 && parsedCars[1].wheelAt[2][0] == 1302.5f &&
          parsedCars[1].wheelAt[2][2] == -951.0f);
    // The particles' slots (0x7ddfd0, 128 bytes each): the live ones.
    {
        std::vector<uint8_t> parts(3 * 128, 0);
        auto p32 = [&](size_t at, uint32_t v) { std::memcpy(parts.data() + at, &v, 4); };
        auto pF = [&](size_t at, float v) { std::memcpy(parts.data() + at, &v, 4); };
        p32(128, 199);
        p32(128 + 4, 6);
        p32(128 + 8, 256);
        pF(128 + 0xc, 1303.25f);
        pF(128 + 0x24, 1.5f);
        p32(128 + 0x54, 0x64f1f1f1);
        parts[128 + 0x7d] = 1;
        const std::vector<Particle> live = parseParticleDump(parts);
        CHECK(live.size() == 1 && live[0].life == 199 && live[0].kind == 6 && live[0].lifeMax == 256 &&
              live[0].position[0] == 1303.25f && live[0].size == 1.5f && live[0].colour == 0x64f1f1f1 &&
              live[0].halved);
    }
    CHECK(!parsedCars[0].officer && parsedCars[1].officer && parsedCars[1].officerAt[0] == 742.75f &&
          parsedCars[1].officerAt[2] == -266.25f && parsedCars[1].officerForward[2] == 1 &&
          parsedCars[1].officerRight[0] == 1);
    CHECK(parsedCars.size() == 2 && parsedCars[0].human && !parsedCars[0].cop && parsedCars[0].slice == 230);
    CHECK(parsedCars[0].at[0] == 472.5f && parsedCars[0].forward[2] == 1 && parsedCars[0].beams == 2 &&
          !parsedCars[0].siren);
    CHECK(parsedCars[1].cop && parsedCars[1].siren && parsedCars[1].braking && parsedCars[1].beamScroll == 1 &&
          parsedCars[1].speed == 31.5f && !parsedCars[1].human);
}

// Car against car (0x4310c0 -> 0x430c80: the box test 0x42fee0, the hit
// 0x42f280 with 0x42e820).
void testCarHits() {
    using namespace reluva::nfs;
    // Side by side, 0.166 m into each other: b's left face centre is the
    // first point inside a's box; one round of 0.25 parts them, half each.
    CarBox a;
    CarBox b;
    a.mass = b.mass = 1000;
    b.at = {2.2f, 0, 0};
    a.velocity = {1, 0, 0};
    b.velocity = {-1, 0, 0};
    std::array<float, 3> point{};
    CarBox ta = a;
    CarBox tb = b;
    CHECK(carBoxTouch(ta, tb, point));
    CHECK(std::fabs(point[0] - (2.2f - 1.183f)) < 1e-5f && std::fabs(ta.at[0] + 0.125f) < 1e-6f &&
          std::fabs(tb.at[0] - 2.325f) < 1e-6f);
    CHECK(!carBoxTouch(ta, tb, point));
    // The hit: each closing at 2 m/s, J = 2 x 1000 along x, a's share
    // J / both = 1 m/s back along J; no spin from a face centre at the
    // centre's height; the strength 1, not air.
    CHECK(carBoxCollide(a, b));
    CHECK(std::fabs(a.velocity[0]) < 1e-5f && std::fabs(b.velocity[0]) < 1e-5f && std::fabs(a.strength - 1) < 1e-5f &&
          std::fabs(b.strength - 1) < 1e-5f && a.airTicks == 0 && a.spin == (std::array<float, 3>{0, 0, 0}));
    // Moving apart: no hit (J along the centre line is not closing).
    CarBox c = a;
    CarBox d = b;
    c.at = {0, 0, 0};
    d.at = {2.2f, 0, 0};
    c.velocity = {-1, 0, 0};
    d.velocity = {1, 0, 0};
    c.strength = d.strength = 0;
    CHECK(carBoxCollide(c, d) && c.velocity[0] == -1 && d.velocity[0] == 1 && c.strength == 0);
    // Hard, from behind and off the middle: J over 5 x the mass is cut to
    // 0.75; the strength over 5 puts both in the air (+0x164).
    CarBox e;
    CarBox f;
    e.mass = f.mass = 1000;
    f.at = {1.0f, 0, 4.6f};
    e.velocity = {0, 0, 30};
    CHECK(carBoxCollide(e, f) && e.airTicks == 1 && f.airTicks == 1);
    CHECK(e.velocity[2] < 30 && f.velocity[2] > 0 && std::fabs(e.spin[1]) > 0 && std::fabs(e.spin[1]) <= 1.8f);
    // Far apart, or not in play: nothing.
    std::vector<CarBox> list{a, b};
    list[1].at = {0, 0, 20};
    CHECK(carBoxCollisions(list) == 0);
    list[1].at = {1, 0, 0};
    CHECK(carBoxCollisions(list, {true, false}) == 0 && carBoxCollisions(list) == 1);
    // A racer's mass (0x42bcb0): P's over 1.2.
    CHECK(std::fabs(aiCarMass(1472, true) - 1226.67f) < 0.01f && aiCarMass(1930, false) == 1930);

    // The computer's cars (aiCollide, the even steps): side by side on a
    // straight road, the right one moving across into the left one.
    const TrackCollision road = straightRoad(60);
    AiCar left;
    AiCar right;
    for (AiCar *c : {&left, &right}) {
        c->slice = 20;
        c->along = 0.5f;
        c->speed = 30;
        c->sideKept = true;
        c->velSlice = 20;
    }
    left.lateral = left.side = 0;
    right.lateral = right.side = 2.2f;
    right.sideSpeed = -2;
    std::vector<AiCar> pair{left, right};
    aiCollide(pair, road, 101); // odd: not a collision tick
    CHECK(pair[1].sideSpeed == -2 && pair[1].lateral == 2.2f);
    aiCollide(pair, road, 100);
    CHECK(std::fabs(pair[0].lateral + 0.125f) < 1e-4f && std::fabs(pair[1].lateral - 2.325f) < 1e-4f);
    CHECK(std::fabs(pair[0].sideSpeed + 1) < 1e-4f && std::fabs(pair[1].sideSpeed + 1) < 1e-4f &&
          std::fabs(pair[0].speed - 30) < 1e-4f && pair[0].slice == 20 && std::fabs(pair[0].along - 0.5f) < 1e-5f);
    // The side offset along the road polygon's right (+0x124, aiRoadFrame):
    // a right turned 2 degrees measures the place across and along the
    // slice; aiLateralFor goes back.
    {
        AiCar m;
        m.slice = 20;
        m.lateral = 3;
        m.along = 0.5f;
        CHECK(aiMeasuredSide(road, m) == 3); // the slice's right
        const float a = 2 * 3.14159265f / 180;
        m.roadRight = {std::cos(a), 0, -std::sin(a)};
        m.roadRightSet = true;
        const float want = 3 * std::cos(a) - 0.5f * 4 * std::sin(a);
        CHECK(std::fabs(aiMeasuredSide(road, m) - want) < 1e-5f);
        CHECK(std::fabs(aiLateralFor(road, m, want) - 3) < 1e-5f);
        // On the next slice's polygons (+0x1C 21, aiSlice) it is measured
        // from that slice's point.
        CHECK(aiSlice(m) == 20);
        m.roadSlice = 21;
        CHECK(aiSlice(m) == 21 && std::fabs(aiMeasuredSide(road, m) - (want + 4 * std::sin(a))) < 1e-5f);
        CHECK(std::fabs(aiLateralFor(road, m, want + 4 * std::sin(a)) - 3) < 1e-5f);
    }
    // Measured from the body's centre (0x4a2bc0 takes +0x98), which sits
    // with the box's lowest corner on the road polygon's plane (0x4a0440,
    // car_on_road 0x4a1610): on a polygon banked 6 degrees under a level
    // slice the centre is above the page's place (on the slice's plane), and
    // the side offset is its own along the polygon's right.
    {
        const float b = 6 * 3.14159265f / 180;
        AiCar m;
        m.slice = m.roadSlice = 20;
        m.halfWidth = 1;
        m.halfHeight = 0.5f;
        m.halfLength = 2;
        m.roadRight = {std::cos(b), std::sin(b), 0};
        m.roadUp = {-std::sin(b), std::cos(b), 0};
        m.roadPoint = road.road[20].position;
        m.roadRightSet = m.roadUpSet = true;
        // The body level, along the slice: its reach along the polygon's up.
        const float reach = std::sin(b) * m.halfWidth + std::cos(b) * m.halfHeight;
        std::array<float, 3> centre{};
        for (size_t k = 0; k < 3; ++k)
            centre[k] = m.roadPoint[k] + 3 * m.roadRight[k] + reach * m.roadUp[k];
        centre[2] += 2;
        m.lateral = centre[0] - road.road[20].position[0];
        m.along = (centre[2] - road.road[20].position[2]) / 4;
        CHECK(std::fabs(aiMeasuredSide(road, m) - 3) < 1e-4f);
        CHECK(std::fabs(aiLateralFor(road, m, 3) - m.lateral) < 1e-4f);
        m.roadUpSet = false; // without it, the place's own: 10 cm short
        CHECK(std::fabs(aiMeasuredSide(road, m) - m.lateral * std::cos(b)) < 1e-4f && aiMeasuredSide(road, m) < 2.91f);
    }
    // car_on_road's velocity step (aiContactVelocity): through the rows and
    // back. Unit rows only drop the part into the road (under 0.5 m/s);
    // rows of a 16.16 frame, the forward 1 - 8e-5 long, also take
    // 20 x 1.6e-4 = 0.0032 m/s off the speed along it.
    {
        const std::array<float, 9> unit{1, 0, 0, 0, 1, 0, 0, 0, 1};
        const std::array<float, 3> v{0.5f, -0.4f, 20};
        auto out = aiContactVelocity(v, unit);
        CHECK(out[0] == 0.5f && out[1] == 0 && out[2] == 20);
        out = aiContactVelocity({0.5f, 0.6f, 20}, unit); // 0.5 or more: kept
        CHECK(out[1] == 0.6f);
        std::array<float, 9> fixed = unit;
        fixed[8] = 1 - 8e-5f;
        out = aiContactVelocity(v, fixed);
        CHECK(std::fabs(out[2] - (20 - 0.0032f)) < 2e-5f);
        // The half turn (0x4b7680): rows tipped 2 degrees about the right
        // turned half way to level are tipped 1; rows turned to themselves
        // come back as unit rows, so the rebuild through them keeps the
        // speed.
        const float a = 2 * 3.14159265f / 180;
        const std::array<float, 9> tipped{1, 0, 0, 0, std::cos(a), std::sin(a), 0, -std::sin(a), std::cos(a)};
        const auto h = aiHalfTurn(unit, tipped);
        CHECK(std::fabs(h[5] - std::sin(a / 2)) < 1e-5f && std::fabs(h[4] - std::cos(a / 2)) < 1e-5f &&
              std::fabs(h[7] + std::sin(a / 2)) < 1e-5f);
        const auto same = aiHalfTurn(fixed, fixed);
        CHECK(std::fabs(aiContactVelocity(v, same)[2] - 20) < 2e-4f);
    }
    // The impacts task: ai_ctl_slide for round(strength) x 7 steps.
    CHECK(pair[0].slideUntil == 107 && pair[1].slideUntil == 107 && pair[0].airTicks == 0);
    // At 30 m/s they don't reverse; the same hit at 8 m/s (0x406120, under
    // 10) starts it for 128 steps, and a second hit before that leaves it on.
    CHECK(!pair[0].reversing && !pair[1].reversing);
    pair = {left, right};
    for (AiCar &c : pair)
        c.speed = 8;
    aiCollide(pair, road, 200);
    CHECK(pair[0].reversing && pair[0].reverseTimed && pair[0].reverseUntil == 328 && pair[0].reverseCount == 1);
    aiReverseSet(pair[0], 2, 128, 300);
    CHECK(pair[0].reversing && pair[0].reverseCount == 1);
    // ai_reverse_set: 2 on a car not reversing starts it; on an untimed one
    // ends it; 1 ends it; ai_reverse_end ends a timed one past its time.
    AiCar back;
    aiReverseSet(back, 0, 0, 10);
    CHECK(back.reversing && !back.reverseTimed && back.reverseCount == 1);
    aiReverseSet(back, 2, 128, 11);
    CHECK(!back.reversing);
    aiReverseSet(back, 2, 128, 12);
    CHECK(back.reversing && back.reverseUntil == 140 && back.reverseCount == 2);
    aiReverseSet(back, 1, 0, 13);
    CHECK(!back.reversing && !back.reverseTimed);
    // The roll a hit gives: in the air the body turns by it as it is (the
    // landing, not aiTilt, ends the air ticks); on the road back towards
    // flat at most 0.5 turn/s, and nothing once within 0.02.
    AiCar tilted;
    tilted.airTicks = 1;
    tilted.rollRate = 1.0f;
    aiTilt(tilted, 1); // odd: nothing
    CHECK(tilted.rollRate == 1.0f && tilted.roll == 0);
    aiTilt(tilted, 2);
    aiTilt(tilted, 4);
    CHECK(tilted.rollRate == 1.0f && tilted.airTicks == 1 && std::fabs(tilted.roll - 2.0f / 32) < 1e-6f);
    tilted.airTicks = 0;
    tilted.roll = 0.125f; // 45 degrees: sin 0.707, held to 0.5
    aiTilt(tilted, 6);
    CHECK(tilted.rollRate == -0.5f && std::fabs(tilted.roll - (0.125f - 0.5f / 32)) < 1e-6f);
    tilted.roll = 0.002f; // within 0.02
    aiTilt(tilted, 8);
    CHECK(tilted.rollRate == 0 && tilted.roll == 0.002f);

    // +0x21C (0x42cd00): the half sizes across the road's right; lined up
    // the half width, turned square the half length, turned 30 degrees
    // both in part.
    {
        AiCar m;
        m.slice = 20;
        m.halfWidth = 1.2f;
        m.halfHeight = 0.6f;
        m.halfLength = 2.4f;
        CHECK(std::fabs(aiCarExtent(road, m) - 1.2f) < 1e-5f);
        m.heading = 3.14159265f / 2;
        CHECK(std::fabs(aiCarExtent(road, m) - 2.4f) < 1e-5f);
        m.heading = 3.14159265f / 6;
        CHECK(std::fabs(aiCarExtent(road, m) - (1.2f * std::cos(m.heading) + 2.4f * 0.5f)) < 1e-5f);
        // The race task (0x42ce70) writes it, +0x7B8 and +0x638 for every
        // car on the even steps only.
        m.sideKept = true;
        m.lateral = 3.0f;
        std::vector<AiCar> one{m};
        aiRaceTick(one, road, 1);
        CHECK(one[0].extent == 0);
        aiRaceTick(one, road, 2);
        CHECK(std::fabs(one[0].extent - aiCarExtent(road, m)) < 1e-6f && std::fabs(one[0].side - 3.0f) < 1e-5f &&
              one[0].lane == 8);
        // The lanes it spans (0x40e350) take +0x21C, not the half width
        // (lanes 4 wide here): at 3.0 with 1.2 its left side is in lane 8
        // (2.3), turned 30 degrees (+0x21C 2.24) in lane 7 (1.26).
        AiCarData d;
        d.accel.fill(4);
        std::vector<uint8_t> flat(100, 30);
        one[0].heading = 0;
        aiRaceTick(one, road, 4);
        aiStep(one, 0, road, flat, d, 8, 1, false, AiPhase::Drive);
        CHECK(one[0].laneLeft == 8 && one[0].lane == 8 && one[0].laneRight == 8);
        one[0].heading = 3.14159265f / 6;
        aiRaceTick(one, road, 10);
        aiStep(one, 0, road, flat, d, 16, 1, false, AiPhase::Drive);
        CHECK(one[0].laneLeft == 7 && one[0].lane == 8 && one[0].laneRight == 8);
        // The driver's half alone moves nothing; the model's half alone
        // votes nothing.
        CHECK(one[0].lateral == 3.0f && one[0].speed == 0 && one[0].wanted > 0);
        one[0].pick = -1;
        aiStep(one, 0, road, flat, d, 24, 1, false, AiPhase::Move);
        CHECK(one[0].pick == -1 && one[0].speed > 0);
    }
    // ai_ctl_rails' side force along the road polygon's right (+0x124):
    // with that right turned 10 degrees the push across the slice is
    // cos 10 of it and the rest goes along the road.
    {
        AiCar a;
        AiCar b;
        for (AiCar *c : {&a, &b}) {
            c->slice = 20;
            c->speed = 30;
            c->sideKept = true;
            c->velSlice = 20;
            c->offset = c->target = 3;
            c->lateral = c->side = 0;
            c->roadRight = {1, 0, 0};
            c->roadForward = {0, 0, 1};
            c->roadRightSet = true;
        }
        const float t = 10 * 3.14159265f / 180;
        b.roadRight = {std::cos(t), 0, -std::sin(t)};
        AiCarData d;
        d.accel.fill(0);
        std::vector<uint8_t> flat(100, 30);
        std::vector<AiCar> va{a};
        std::vector<AiCar> vb{b};
        aiStep(va, 0, road, flat, d, 1, 1, false, AiPhase::Move); // odd: the side offset measured along each right
        aiStep(vb, 0, road, flat, d, 1, 1, false, AiPhase::Move);
        vb[0].side = va[0].side;
        aiStep(va, 0, road, flat, d, 2, 1, false, AiPhase::Move);
        aiStep(vb, 0, road, flat, d, 2, 1, false, AiPhase::Move);
        CHECK(va[0].steer == AiCar::Rails && vb[0].steer == AiCar::Rails && va[0].sideSpeed > 0);
        CHECK(std::fabs(vb[0].sideSpeed - va[0].sideSpeed * std::cos(t)) < 1e-4f);
    }
    // Put back on the road (0x429100): 5 slices on, at the slice's point,
    // still, a ghost to the hits for 64 ticks; with a car there the next
    // free lane middle out from the point, the right first (0x428d60).
    {
        AiCar m;
        AiCar other;
        m.slice = m.roadSlice = 20;
        m.along = 0.4f;
        m.lateral = m.side = -9;
        m.speed = 3;
        m.sideSpeed = 1;
        m.heading = 0.5f;
        m.sideKept = true;
        other.slice = other.roadSlice = 40;
        other.lane = 7;
        std::vector<AiCar> v{m, other};
        aiPutBack(v, 0, road);
        CHECK(v[0].slice == 25 && v[0].along == 0 && v[0].lateral == 0 && v[0].speed == 0 && v[0].sideSpeed == 0 &&
              v[0].heading == 0 && v[0].ghostTicks == 64 && v[0].airTicks == 1 && v[0].lane == 7);
        v = {m, other};
        v[1].slice = v[1].roadSlice = 26;
        // Lanes 4 wide: the middles 2 and -2 are lanes 7 and 6, next to the
        // car's 7 a slice on; 6 and -6 are lanes 9 and 4 (0x40e2b0 rounds
        // 1.5 to 2), shut here. Free from slice 28, at lane 7's middle.
        CHECK(!aiSpotFree(v, 0, road, 25, 0) && !aiSpotFree(v, 0, road, 25, 2) && !aiSpotFree(v, 0, road, 25, -2) &&
              !aiSpotFree(v, 0, road, 25, 6) && !aiSpotFree(v, 0, road, 27, 2) && aiSpotFree(v, 0, road, 28, 2) &&
              aiSpotFree(v, 0, road, 25, 10.5f) == false);
        aiPutBack(v, 0, road);
        CHECK(v[0].slice == 28 && v[0].lateral == 2);
        // The race's way (0x429100), not the car's: a car running against
        // the slices still goes 5 on; on a backward race 5 back.
        v = {m, other};
        v[0].way = -1;
        aiPutBack(v, 0, road);
        CHECK(v[0].slice == 25);
        v = {m, other};
        v[0].raceBackward = true;
        aiPutBack(v, 0, road);
        CHECK(v[0].slice == 15);
        // A ghost isn't hit: the two side by side as above, one put back.
        std::vector<AiCar> two{left, right};
        two[1].ghostTicks = 2;
        aiCollide(two, road, 100);
        CHECK(two[1].sideSpeed == -2 && two[1].ghostTicks == 1);
        // From ai_target_point: more than 8 reversings at 10 m/s or less,
        // on the road's lanes.
        AiCar r;
        r.slice = r.roadSlice = 30;
        r.speed = 5;
        r.sideKept = true;
        r.velSlice = 30;
        r.lane = 7;
        r.reverseCount = 9;
        AiCarData d;
        d.accel.fill(4);
        std::vector<uint8_t> flat(100, 30);
        std::vector<AiCar> rv{r};
        aiStep(rv, 0, road, flat, d, 2, 1, false, AiPhase::Move);
        CHECK(rv[0].slice == 35 && rv[0].reverseCount == 0 && rv[0].ghostTicks == 64 && !rv[0].putBackDue);
        // Off the lanes (lane 11, two past the edge's 9) and slow: +0x7D8
        // counts; its bit 0x40 steers at its own slice, over 1000 it is
        // put back.
        AiCar o = r;
        o.reverseCount = 0;
        o.lane = 11;
        o.speed = 1;
        o.offRoadTicks = 1000;
        std::vector<AiCar> ov{o};
        aiStep(ov, 0, road, flat, d, 2, 1, false, AiPhase::Move);
        CHECK(ov[0].slice == 35 && ov[0].offRoadTicks == 0);
        o.offRoadTicks = 5;
        o.lane = 8;
        ov = {o};
        aiStep(ov, 0, road, flat, d, 2, 1, false, AiPhase::Move);
        CHECK(ov[0].offRoadTicks == 0 && ov[0].slice == 30);
    }
    // A roadblock's hit (0x42ac40 -> 0x42ad35): a racer swerving into a
    // police car beside it. Hits over 5 but not over 30 between a racer
    // and a police car (neither traffic, +0x200 bit 0x10: car_kind_from_role
    // 0x428700 gives police 0x20 and 2) start no smoke and so no fire, the
    // smoke's ticks run with each step's car (as the Track page now runs
    // them, from the cars' steps) for 200 steps; the same hit on a traffic
    // car smokes and stops once the car is down and still.
    {
        AiCar racer;
        AiCar cop;
        for (AiCar *c : {&racer, &cop}) {
            c->slice = 20;
            c->along = 0.5f;
            c->speed = 20;
            c->sideKept = true;
            c->velSlice = 20;
        }
        racer.racer = true;
        cop.racer = false;
        cop.cop = true;
        racer.lateral = racer.side = 0;
        cop.lateral = cop.side = 2.2f;
        racer.sideSpeed = 25;
        std::vector<AiCar> block{racer, cop};
        std::vector<AiHit> hits;
        ParticleRandom random;
        aiCollide(block, road, 100, &hits);
        CHECK(!hits.empty());
        bool over5 = false;
        for (const AiHit &h : hits) {
            over5 = over5 || h.strength > 5;
            CHECK(h.strength <= 30);
            CarSmoke smoke;
            const AiCar &c = block[h.car];
            CHECK(!startCarSmoke(smoke, h.strength, !c.racer && !c.cop, {h.point[0], h.point[1], h.point[2]}, {},
                                 {1, 0, 0}, {0, 1, 0}, {0, 0, 1}));
            ParticleSystem none;
            for (int step = 0; step < 200; ++step)
                carSmokeTick(none, smoke, {}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {}, step < 100, 1, 0.5f, 0.5f,
                             uint32_t(step), random);
            CHECK(smoke.timer == 0 && none.live() == 0);
            // As traffic: smoke at once, black from 17, fire past 64 only
            // while the car is still in the air or spinning.
            if (h.strength > 5) {
                CHECK(startCarSmoke(smoke, h.strength, true, {h.point[0], h.point[1], h.point[2]}, {}, {1, 0, 0},
                                    {0, 1, 0}, {0, 0, 1}));
                carSmokeTick(none, smoke, {}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {}, true, 1, 0, 0, 0, random);
                CHECK(smoke.timer > 1 && none.live() == 1);
                carSmokeTick(none, smoke, {}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {}, false, 1, 0.1f, 0.1f, 1, random);
                CHECK(smoke.timer == 0);
            }
        }
        CHECK(over5);
    }
    // The skid levels the controllers leave (+0x5C0, +0x5C4): rails over
    // 7 m/s across the road 2 slices on, the front x 0.15 and the rear x
    // 0.5 only while braking; slide both the side speed over 7; slow keeps
    // them.
    {
        AiCar s;
        s.steer = AiCar::Rails;
        aiSkid(s, -10, false);
        CHECK(std::fabs(s.skidFront - 0.45f) < 1e-6f && s.skidRear == 0);
        aiSkid(s, 10, true);
        CHECK(std::fabs(s.skidFront - 0.45f) < 1e-6f && std::fabs(s.skidRear - 1.5f) < 1e-6f);
        aiSkid(s, 6, true);
        CHECK(s.skidFront == 0 && s.skidRear == 0);
        s.steer = AiCar::Slide;
        aiSkid(s, -9, false);
        CHECK(s.skidFront == 2 && s.skidRear == 2);
        s.steer = AiCar::Slow;
        aiSkid(s, 20, true);
        CHECK(s.skidFront == 2 && s.skidRear == 2);
        // car_spiked_effects' wheel levels from them: the front x 7.5.
        CHECK(wheelSkidLevel(0, 2, 2, false, 1) == 10 && wheelSkidLevel(3, 2, 2, false, 1) == 2);
    }
    // The race's random numbers (0x4371d0 seeding, 0x40829a drawing):
    // seed 2 makes 3 and 7 (both odd), 3 x 7 x 3^7 kept to 16 bits a step.
    {
        AiStartRandom r;
        r.seed(2);
        uint32_t s = 21;
        for (int i = 0; i < 6; ++i)
            s = (s * 3) & 0xffff;
        s *= 3;
        CHECK(r.mult == 3 && r.low == (s & 0xffff));
        const uint32_t next = r.low * 3;
        CHECK(r.pick() == static_cast<int>(((next & 0xffff00) >> 8) * 14 >> 16) && r.low == (next & 0xffff));
        int seen = 0;
        r.seed(0x1234567);
        for (int i = 0; i < 2000; ++i) {
            const int k = r.pick();
            CHECK(k >= 0 && k < 14);
            seen |= 1 << k;
        }
        CHECK(seen == 0x3fff);
    }
    // The start's wheelspin (ai_ctl_slide 0x408244): a racer within race
    // distance 20 taking its table's first acceleration, when the pick is
    // its slot; a cop, a racer past 20 or another acceleration draws none.
    {
        AiCarData data;
        data.accel.fill(5.0f);
        data.accel[0] = 7.0f;
        AiCar car;
        car.racer = true;
        car.distance = 3;
        AiStartRandom r;
        AiStartRandom copy;
        r.seed(99);
        copy = r;
        int spins = 0;
        for (car.slot = 0; car.slot < 14; ++car.slot) {
            const int k = copy.pick();
            const bool spun = aiStartSpin(car, data, 7.0f, r);
            CHECK(spun == (k == car.slot));
            spins += spun ? 1 : 0;
        }
        CHECK(spins <= 14);
        copy = r;
        CHECK(!aiStartSpin(car, data, 5.0f, r) && r.low == copy.low); // not drawn
        car.distance = 20;
        CHECK(!aiStartSpin(car, data, 7.0f, r) && r.low == copy.low);
        car.distance = 0;
        car.cop = true;
        CHECK(!aiStartSpin(car, data, 7.0f, r) && r.low == copy.low);
        car.cop = false;
        car.slot = -1;
        CHECK(!aiStartSpin(car, data, 7.0f, r) && r.low != copy.low); // drawn, never picked
    }
    // A car out of the race (ai_finished_coast 0x407380).
    {
        AiCoast c;
        c.forward = 30;
        c.ground = 30;
        c.right = 2;
        c.yawRate = 0.3f;
        aiFinishedCoast(c);
        CHECK(c.forward == static_cast<float>(30.0 * 0.98));
        CHECK(c.yawRate == 0.15f && c.right == static_cast<float>(2 * 0.85));
        CHECK(c.skidFront == 0 && c.skidRear == static_cast<float>(static_cast<double>(c.forward) * 0.1));
        // Slow: halved, no rear level from the speed; the yaw over 0.2 adds.
        c = {};
        c.forward = 4;
        c.ground = 4;
        c.yawRate = 1;
        aiFinishedCoast(c);
        CHECK(c.forward == static_cast<float>(4 * 0.95) * 0.5f && c.skidRear == 0.5f && c.skidFront == 0);
        // Sliding more sideways than along: both levels the side speed.
        c = {};
        c.forward = -12;
        c.ground = 15;
        c.right = 20;
        aiFinishedCoast(c);
        CHECK(c.skidFront == 17.0f && c.skidRear == 17.0f && c.right == static_cast<float>(17.0 * 0.2));
    }
    // The laps (car_laps_owed 0x42cc20, car_race_distance 0x42ce10,
    // car_laps_task 0x4ce280): over slice 500 to 0-50 a lap, from 0 to over
    // 500 a lap owed; at the race's laps the racer has finished.
    {
        AiCar car;
        car.racer = true;
        car.laps = 2;
        car.slice = 590;
        aiLaps(car, 600, 0);
        CHECK(car.slicePrev == 590 && car.lap == 0 && car.raceState == 1);
        car.slice = 599;
        aiLaps(car, 600, 2);
        CHECK(car.distance == 599 && car.finishStep == 2);
        car.slice = 3;
        aiLaps(car, 600, 4);
        CHECK(car.lap == 1 && car.distance == 603 && car.raceState == 1 && car.lapCounted == 1);
        car.slice = 0;
        aiLaps(car, 600, 6);
        car.slice = 550; // back over the line: a lap owed, no race distance
        aiLaps(car, 600, 8);
        CHECK(car.lap == 1 && car.lapsOwed == 1 && car.distance == 0);
        car.slice = 10; // paid
        aiLaps(car, 600, 10);
        CHECK(car.lap == 1 && car.lapsOwed == 0 && car.distance == 610);
        car.slice = 520;
        aiLaps(car, 600, 12);
        car.slice = 2;
        aiLaps(car, 600, 14);
        CHECK(car.lap == 2 && car.raceState == 3 && car.finishStep == 14 && car.distance == 1202);
        aiLaps(car, 600, 16);
        CHECK(car.finishStep == 14); // kept once out of the race
        AiCar busted = car;
        busted.raceState = 4;
        busted.lap = busted.lapCounted = 1;
        busted.slice = 520;
        aiLaps(busted, 600, 18);
        busted.slice = 2;
        aiLaps(busted, 600, 20);
        CHECK(busted.lap == 2 && busted.raceState == 4);
        AiCar none; // no laps: nothing counted
        none.slice = 520;
        aiLaps(none, 600, 0);
        none.slice = 2;
        aiLaps(none, 600, 2);
        CHECK(none.lap == 0 && none.distance == 1 << 20);
    }
    // The default grid (0x42ba00): slice 3 x place + 1, half a lane width
    // left for the even places, right for the odd; backward the other way.
    {
        TrackCollision g = straightRoad(40);
        CHECK(aiGridSlice(0, 8, 40) == 1 && aiGridSlice(1, 8, 40) == 4 && aiGridSlice(7, 8, 40) == 22);
        CHECK(aiGridSlice(0, 8, 40, true) == 22 && aiGridSlice(7, 8, 40, true) == 1);
        CHECK(aiGridOffset(g.road[1], 0) == -2.0f && aiGridOffset(g.road[4], 1) == 2.0f);
        CHECK(aiGridOffset(g.road[1], 0, true) == 2.0f);
    }
    // The body's rows as the game keeps them: 16.16 rows a little short of
    // unit length come back from the page's axes (aiTiltedAxes), and the
    // turns since (+0x94) past 1/32 turn make them orthonormal again
    // (car_integrate_turn 0x4a2e20, 0x4b7840).
    {
        TrackCollision g = straightRoad(40);
        AiCar car;
        car.slice = 5;
        car.halfWidth = 1;
        car.offset = car.target = car.side = car.lateral = 2;
        car.sideKept = true;
        const float k = 0.99990f;
        const std::array<float, 3> right{k, 0, 0.001f};
        const std::array<float, 3> up{0, 0.9999f, 0};
        const std::array<float, 3> forward{-0.001f, 0.003f, k};
        aiSetRows(car, g, {0, 0, 20}, right, up, forward);
        CHECK(car.rowsShaped);
        std::array<float, 3> f{};
        std::array<float, 3> r{};
        std::array<float, 3> u{};
        aiTiltedAxes(g, car, f, r, u);
        for (size_t i = 0; i < 3; ++i)
            CHECK(std::fabs(f[i] - forward[i]) < 2e-6f && std::fabs(r[i] - right[i]) < 2e-6f &&
                  std::fabs(u[i] - up[i]) < 2e-6f);
        // 255 calls with no turn keep them; a turn of 1/32 turn renormalises.
        car.turnCount = 0;
        car.turnSum = 0.03f;
        car.yawRate = 0.01f;
        car.turnDue = true;
        aiAdvance(car, g);
        CHECK(car.rowsShaped && car.turnCount == 255 && std::fabs(car.turnSum - (0.03f + 0.01f / 32)) < 1e-6f);
        car.yawRate = 0.04f;
        car.turnDue = true;
        aiAdvance(car, g);
        CHECK(!car.rowsShaped && car.turnCount == 32 && car.turnSum == 0);
        aiTiltedAxes(g, car, f, r, u);
        CHECK(std::fabs(f[0] * f[0] + f[1] * f[1] + f[2] * f[2] - 1) < 1e-5f &&
              std::fabs(f[0] * u[0] + f[1] * u[1] + f[2] * u[2]) < 1e-5f);
    }
    // Out of the race the model only coasts (car_ai_model 0x408639): no
    // forces, the speed x 0.98 a call (+0xB8 over 22.35), the brake held;
    // the driver goes to state 4 (0x409d0c).
    std::vector<uint8_t> flat(100, 30);
    AiCarData go;
    go.accel.fill(4);
    go.maxBrake = 10;
    go.topSpeedCap = 80;
    {
        TrackCollision g = straightRoad(200);
        AiCar car;
        car.slice = 10;
        car.halfWidth = 1;
        car.racer = true;
        car.offset = car.target = car.side = car.lateral = 2;
        car.sideKept = true;
        car.speed = car.wanted = 30;
        car.raceState = 3;
        std::vector<AiCar> one{car};
        aiStep(one, 0, g, flat, go, 0, -1, false, AiPhase::Move);
        CHECK(std::fabs(one[0].speed - 30 * 0.98f) < 0.01f && one[0].braking && one[0].skidRear > 2.9f);
        aiStep(one, 0, g, flat, go, 0, -1, false, AiPhase::Drive);
        CHECK(one[0].pursuit == 4);
        for (int step = 2; step < 64 * 6; step += 2)
            aiStep(one, 0, g, flat, go, step, -1, false);
        CHECK(std::fabs(one[0].speed) < 0.05f);
        // Still racing, the same car is pushed on.
        car.raceState = 1;
        car.wanted = 40;
        one = {car};
        aiStep(one, 0, g, flat, go, 0, -1, false, AiPhase::Move);
        CHECK(one[0].speed > 30 && !one[0].braking);
    }
    // Tipped (+0x10C under 0.9): the dynamics put it in the air (+0x164 1,
    // car_flat_or_roof 0x49fde0), and instead of the model the body's
    // friction (0x4aa8e7) in the air: the velocity along the road x 0.98,
    // the skid levels 0.
    {
        TrackCollision g = straightRoad(200);
        AiCar car;
        car.slice = 10;
        car.halfWidth = 1;
        car.racer = true;
        car.offset = car.target = car.side = car.lateral = 2;
        car.sideKept = true;
        car.speed = car.wanted = 20;
        car.roll = 0.12f; // 43 degrees (the road turns it back 1/64 turn first)
        car.skidFront = car.skidRear = 5;
        std::vector<AiCar> one{car};
        aiStep(one, 0, g, flat, go, 0, -1, false, AiPhase::Move);
        CHECK(std::fabs(one[0].speed - 20 * 0.98f) < 0.02f && one[0].skidFront == 0 && one[0].skidRear == 0 &&
              one[0].airTicks == 1);
    }
    // Before the race's start (g_race_started) the car is held still; the
    // start's push (ai_ctl_slide 0x4082dc): 0.5 more side force and half of
    // the side force more yaw force for the racer the pick names.
    {
        TrackCollision g = straightRoad(200);
        AiCar car;
        car.slice = 4;
        car.halfWidth = 1;
        car.racer = true;
        car.offset = car.target = car.side = car.lateral = 2;
        car.sideKept = true;
        car.wanted = 30;
        car.distance = 4;
        car.raceStartStep = 4;
        std::vector<AiCar> one{car};
        aiStep(one, 0, g, flat, go, 2, -1, false, AiPhase::Move);
        CHECK(one[0].speed == 0 && one[0].braking);
        AiCarData start = go;
        start.accel[0] = 6;
        AiStartRandom saved = aiStartRandom();
        AiStartRandom probe = saved;
        car.raceStartStep = 0;
        car.slot = probe.pick();
        one = {car};
        aiStep(one, 0, g, flat, start, 4, -1, false, AiPhase::Move);
        aiStartRandom() = saved;
        car.slot = car.slot == 0 ? 1 : 0; // not picked
        std::vector<AiCar> other{car};
        aiStep(other, 0, g, flat, start, 4, -1, false, AiPhase::Move);
        aiStartRandom() = saved;
        CHECK(one[0].skidRear == 10 && other[0].skidRear != 10);
        CHECK(std::fabs((one[0].vel[0] - other[0].vel[0]) - 0.5f / 32) < 1e-5f);
        CHECK(std::fabs((one[0].yawRate - other[0].yawRate) - 0.25f / 32) < 1e-5f);
    }
}

void testComputerPursuits() {
    // Lanes 4 wide (bytes 8), two each side, walls 10 away.
    TrackCollision col = straightRoad(200);
    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.maxVelocity = 70;
    data.topSpeedCap = 80;
    std::vector<uint8_t> flat(100, 30);

    // The side offset as the game measures it (0x4a2bc0): from the slice's
    // point along its right, so on a bend it moves across the slice while
    // the car goes straight on.
    TrackCollision bent = straightRoad(20);
    for (size_t i = 6; i < bent.road.size(); ++i)
        bent.road[i].position[0] = 0.5f * static_cast<float>(i - 5);
    AiCar measured;
    measured.slice = 6;
    measured.halfWidth = 1;
    measured.offset = measured.target = measured.side = measured.lateral = 2;
    measured.sideKept = true;
    measured.along = 0.5f;
    measured.speed = measured.wanted = 30;
    std::vector<AiCar> one{measured};
    aiStep(one, 0, bent, flat, data, 0, 1, false);
    CHECK(one[0].along > 0.5f &&
          std::fabs(one[0].side - one[0].lateral - one[0].along * 0.5f) < 1e-4f); // the line's 0.5 x along

    // A strip (spike_lay 0x4c9dd0): across lane 7's middle, 1.5 lanes long.
    float from = 0;
    float to = 0;
    aiStripSpan(col.road[0], 7, from, to);
    CHECK(from == -1 && to == 5);
    aiStripSpan(col.road[0], 6, from, to);
    CHECK(from == -5 && to == 1);

    // The police car ordered to lay one (0x4039c0's order: kind 0 from its
    // slice - 6, 3 slices): it turns back to the range, goes to the
    // roadside (3), stops off the lanes (4) and lays it in lane 7 (an even
    // cop), radioing (0x40b9c0).
    AiCar cop;
    cop.cop = true;
    cop.racer = false;
    cop.slice = 100;
    cop.halfWidth = 1;
    cop.offset = cop.target = 2;
    cop.phase = 1;
    AiCar racer;
    racer.slice = 60;
    racer.halfWidth = 1;
    racer.offset = racer.target = 2;
    racer.speed = racer.wanted = 30;
    std::vector<AiCar> cars{racer, cop};
    aiOrderStrip(cars[1], 0, 200);
    CHECK(cars[1].orderKind == 0 && cars[1].orderFrom == 94 && cars[1].orderLength == 3 && cars[1].stripWanted);
    int laid = -1;
    for (int step = 0; step < 64 * 20 && laid < 0; ++step) {
        aiCopStep(cars, 1, col, data, step, 1, false); // moved on by its velocity (aiAdvance)
        if (cars[1].stripOn)
            laid = step;
    }
    CHECK(laid > 0 && cars[1].copState == 4 && cars[1].way == -1 && cars[1].stripLane == 7 &&
          cars[1].radio == 0x40b9c0);
    // Laid where it gets off the lanes, from the range's start on along its
    // way: turning out at 6.5 m/s (ai_ctl_slow) takes it a few seconds,
    // and this loop moves it on a slice each 16 steps.
    CHECK(cars[1].stripSlice <= 97 && cars[1].stripSlice > 70);
    // A racer 1-5 slices before it in that lane: -50 there, its speed
    // wanted x 0.9 (0x40d720), so it moves over.
    cars[0].slice = cars[1].stripSlice - 3;
    cars[0].offset = cars[0].target = cars[0].side = cars[0].lateral = 2;
    cars[0].sideKept = true;
    aiStep(cars, 0, col, flat, data, 0, 1, false);
    CHECK(cars[0].pick != 7 && std::fabs(cars[0].wanted - 30 * 0.9f) < 1e-4f);
    // Its front wheels 0.6 m short of the strip's line: not on it (out
    // codes against the wheels' +-0.5 m boxes, 0x4ca160).
    cars[0].slice = cars[1].stripSlice - 1;
    cars[0].along = (4.0f - 1.3f - 0.6f) / 4.0f;
    cars[0].offset = cars[0].target = cars[0].side = cars[0].lateral = 2;
    cars[0].speed = 40;
    cars[0].overspeed = 15; // as a cop's look at it left +0x6F0
    aiCopStep(cars, 1, col, data, 1, 1, false);
    CHECK(cars[1].stripOn && !cars[0].pulledOver);
    // Run over (the front wheels on its line), it is pulled over (0x401650)
    // and the cop takes it as its target; the racer pulls over (5), and
    // stops (6).
    cars[0].along = (4.0f - 1.3f) / 4.0f;
    aiCopStep(cars, 1, col, data, 1, 1, false);
    CHECK(!cars[1].stripOn && cars[0].pulledOver && cars[0].spiked && cars[1].chasing == 0 && cars[1].copState == 5 &&
          cars[1].siren && cars[0].recordStep == 1 + 320);
    cars[0].slice = cars[1].stripSlice;
    cars[0].along = 0;
    aiStep(cars, 0, col, flat, data, 8, 1, false);
    CHECK(cars[0].pursuit == 5 && cars[0].wanted == 11);
    cars[0].speed = 2;
    aiStep(cars, 0, col, flat, data, 16, 1, false);
    CHECK(cars[0].pursuit == 6);
    // The cop within 16 m of it: the stop (6), and 384 steps later the
    // ticket (0x401ff0): free of chases for 960 steps, back on patrol.
    cars[1].slice = cars[0].slice;
    cars[1].way = 1;
    cars[1].offset = cars[1].target = cars[1].side = cars[1].lateral = cars[0].lateral;
    aiCopStep(cars, 1, col, data, 2 * 64 + 6, 1, false); // (67 + 1) & 3 = 0
    CHECK(cars[1].copState == 6 && !cars[1].siren && cars[0].recordStep == 134 + 384);
    aiCopStep(cars, 1, col, data, 134 + 384, 1, false); // not past it yet
    CHECK(cars[1].copState == 6);
    aiCopStep(cars, 1, col, data, 134 + 392, 1, false);
    CHECK(cars[1].copState == 1 && cars[1].chasing == -1 && cars[0].tickets == 1 && !cars[0].pulledOver &&
          cars[0].chaseFree == 526 + 960 && cars[1].radio == 0x40b670 && cars[1].orderKind == 2);
    aiStep(cars, 0, col, flat, data, 528, 1, false);
    CHECK(cars[0].pursuit == 1);

    // The chase: past its stopped target by more than 7 slices and under
    // 3 m/s, the cop reverses at 9.83 m/s (0x402420), its aim the road's
    // point (ai_reverse_end); then slices x 5 back.
    AiCar chaser = cop;
    chaser.phase = 0;
    chaser.slice = 120;
    chaser.copState = 5;
    chaser.chasing = 0;
    chaser.speed = 0;
    std::vector<AiCar> chase{racer, chaser};
    chase[0].slice = 110;
    chase[0].speed = 0;
    chase[0].pulledOver = true;
    chase[0].pursuit = 6;
    chase[0].recordStep = 1000; // the stop's step not yet past (0x402110)
    aiCopStep(chase, 1, col, data, 0, 1, false);
    CHECK(chase[1].reversing && std::fabs(chase[1].wanted + 9.834f) < 1e-4f && chase[1].offset == 0);
    aiCopStep(chase, 1, col, data, 8, 1, false);
    // (backing off slice 120's point it is on slice 119: 9 slices back)
    CHECK(chase[1].reversing && chase[1].slice == 119 && chase[1].wanted == -45 && chase[1].speed < 0);

    // A roadblock (0x403b20's order, kind 4) at its own slice with no racer
    // within 240 m: in place (10), radioing (0x40bac0), then across the road
    // (11, 0x403180); the racer 5 slices past it (0x403ed0): patrol.
    AiCar blocker = cop;
    blocker.phase = 0;
    blocker.slice = 50;
    blocker.speed = 0;
    std::vector<AiCar> block{racer, blocker};
    block[0].slice = 150; // 400 m off
    aiOrderRoadblock(block[1], 0, 50);
    aiCopStep(block, 1, col, data, 0, 1, false);
    CHECK(block[1].copState == 11 && block[1].across && block[1].siren);
    block[0].slice = 56;
    aiCopStep(block, 1, col, data, 8, 1, false);
    CHECK(!block[1].across && !block[1].blockWatch && block[1].orderKind == 2);
    CHECK(std::string(aiRadioCall(0x40b670)) == "suspect ticketed" && std::string(aiRadioCall(0x123)).empty());
}

// The police stand-in's rules from November 2026's reading (d6): the stop's
// hold by race distance, the strip's quad and the wheels on it, the calls
// for help by heat, and the bend a car cuts as it steers at a point ahead.
void testCopCalls() {
    // cop_ai_stop_check (0x402110, 0x40237a): 160 / 192 / 224 steps.
    CHECK(aiStopHold(false, 61) == 160 && aiStopHold(false, 60) == 192 && aiStopHold(false, -61) == 224 &&
          aiStopHold(false, 0) == 192 && aiStopHold(true, 100) == 192);

    // spike_lay (0x4c9dd0): the quad's ends on the road, 0.5 above them.
    TrackCollision col = straightRoad(200);
    const auto quad = aiStripQuad(col.road[10], 7);
    CHECK(quad[0][0] == -1 && quad[1][0] == 5 && quad[0][2] == 40 && quad[2][1] == 0.5f && quad[3][0] == -1);
    // spike_hit_test (0x4ca160): the wheels within 0.5 m of it, a bit each.
    const std::array<float, 3> right{1, 0, 0};
    const std::array<float, 3> up{0, 1, 0};
    const std::array<float, 3> forward{0, 0, 1};
    const std::array<std::array<float, 2>, 4> wheels{{{-0.8f, 1.3f}, {0.8f, 1.3f}, {-0.8f, -1.3f}, {0.8f, -1.3f}}};
    CHECK(aiStripWheels(quad, {2, 0, 39}, right, up, forward, wheels) == 3);       // the front ones 0.3 m past it
    CHECK(aiStripWheels(quad, {2, 0, 38.1f}, right, up, forward, wheels) == 0);    // 0.6 m short
    CHECK(aiStripWheels(quad, {2, 0, 41.2f}, right, up, forward, wheels) == 12);   // the rear ones on it
    CHECK(aiStripWheels(quad, {7, 0, 38.7f}, right, up, forward, wheels) == 0);    // beside it
    CHECK(aiStripWheels(quad, {5.6f, 0, 38.7f}, right, up, forward, wheels) == 1); // the left front at its end

    // The calls for help (0x4032e5, cop_ai_call_strip 0x4039c0): a cop
    // chasing a racer whose heat (chases lost + cops on it + tickets +
    // time / 2048) is over 6 radios for a strip at its first call, and at
    // a later one (another cop's, or its own after one that only cleared
    // its turn) orders the free cop 51+ slices ahead to the roadside.
    AiCarData data;
    data.accel.fill(4);
    data.maxBrake = 10;
    data.maxVelocity = 70;
    AiCar racer;
    racer.slice = 100;
    racer.halfWidth = 1;
    racer.offset = racer.target = 2;
    racer.speed = racer.wanted = 40;
    racer.recordOpen = true;
    racer.chasesLost = 6;
    AiCar chaser;
    chaser.cop = true;
    chaser.racer = false;
    chaser.slice = 98;
    chaser.halfWidth = 1;
    chaser.offset = chaser.target = 2;
    chaser.copState = 5;
    chaser.chasing = 0;
    chaser.siren = true;
    chaser.speed = 40;
    AiCar free = chaser;
    free.slice = 170;
    free.copState = 1;
    free.chasing = -1;
    free.siren = false;
    free.phase = 1;
    free.index = 1;
    std::vector<AiCar> cars{racer, chaser, free};
    aiCopStep(cars, 1, col, data, 400, 1, false);
    CHECK(cars[1].copState == 5 && cars[1].radio == 0x40b910 && (cars[0].calls & 2) && cars[0].caller == 1 &&
          cars[0].callAt == 400 && cars[2].orderKind == -1);
    aiCopStep(cars, 1, col, data, 720, 1, false); // within 320 steps: nothing
    CHECK(cars[0].callAt == 400);
    aiCopStep(cars, 1, col, data, 728, 1, false); // its own turn: only cleared
    CHECK(cars[0].callAt == 728 && cars[0].caller == -1 && cars[2].orderKind == -1);
    aiCopStep(cars, 1, col, data, 1056, 1, false);
    CHECK(cars[2].orderKind == 0 && cars[2].stripWanted && cars[2].watching == 0 && cars[2].orderFrom == 164 &&
          cars[0].stripCop == 2);
    // Cool, it asks nothing: heat 0 + 1 + 0 - 3 + 0 is not over 3.
    std::vector<AiCar> cool{racer, chaser, free};
    cool[0].chasesLost = 0;
    aiCopStep(cool, 1, col, data, 400, 1, false);
    CHECK(cool[0].calls == 0 && cool[1].radio != 0x40b910);

    // The ticket (0x401ff0, cop_after_ticket 0x4040e0): every cop on the
    // racer or watching it lets it go, no chase lost, and patrols.
    std::vector<AiCar> stop{racer, chaser, free};
    stop[1].copState = 6;
    stop[0].recordStep = 10;
    stop[2].watching = 0;
    stop[2].stripWanted = stop[2].stripOn = true;
    stop[0].stripCop = 2;
    aiCopStep(stop, 1, col, data, 400, 1, false);
    CHECK(stop[0].tickets == 1 && stop[0].chasesLost == 0 && stop[0].overspeed == 0 && !stop[0].recordOpen &&
          stop[1].chasing == -1 && stop[1].copState == 1 && !stop[2].stripOn && stop[2].orderKind == 2 &&
          stop[0].stripCop == -1);

    // The model across the road (aiSteerStep: car_ai_model 0x408600).
    // ai_ctl_rails (0x4076b0), lined up at 30 m/s on the lanes: the side
    // speed wanted -5 x (0 - 4) = 20, held to half the speed (15), against
    // none along the right 2 slices on; force 4 x 1.045 x 15, kept to
    // 38 x 1.045; 1/32 s of it.
    AiCar steer;
    steer.slice = 20;
    steer.speed = 30;
    steer.offset = 4;
    steer.halfWidth = 1;
    steer.sideKept = true;
    steer.velSlice = 20;
    aiSteerStep(steer, col, 0);
    CHECK(steer.steer == AiCar::Rails && std::fabs(steer.sideSpeed - 38 * 1.045f / 32) < 1e-4f &&
          std::fabs(steer.side - steer.sideSpeed / 64) < 1e-5f);
    for (int k = 1; k < 192; ++k)
        aiSteerStep(steer, col, k);
    CHECK(std::fabs(steer.side - 4) < 0.1f && std::fabs(steer.sideSpeed) < 0.5f); // settled in 3 s
    // Stopped, it doesn't move across (ai_ctl_slide, nothing wanted at 0).
    AiCar still = steer;
    still.speed = 0;
    still.offset = -4;
    for (int k = 0; k < 128; ++k)
        aiSteerStep(still, col, k);
    CHECK(still.steer == AiCar::Slide && std::fabs(still.side - steer.side) < 0.05f);
    // Slow (ai_ctl_slow under 13.41 m/s), it turns towards the point and
    // goes there, not past the wall (10, less its half width).
    AiCar slow = steer;
    slow.speed = 8;
    slow.offset = 7;
    slow.heading = slow.yawRate = slow.carSide = slow.sideSpeed = 0;
    float most = 0;
    for (int k = 0; k < 64 * 8; ++k) {
        aiSteerStep(slow, col, k);
        most = std::max(most, slow.side);
    }
    CHECK(slow.side > 6 && most <= 9.0f + 1e-4f && slow.heading != 0);
    // A car pushed at the wall stops there.
    AiCar wall = steer;
    wall.offset = 30;
    for (int k = 0; k < 256; ++k)
        aiSteerStep(wall, col, k);
    CHECK(std::fabs(wall.lateral - 9) < 1e-4f);
    // car_ai_model's test for +0x7B8 off the slice's lanes (0x408703-
    // 0x40876f) compares the lanes x width bytes x 0x8000 (16.16) as an
    // integer with the side in metres: past the lanes' edge (8 m) a lined
    // up car stays on the rails; only a side with no lanes counts.
    {
        AiCar edge = steer;
        edge.offset = 9.5f;
        for (int k = 0; k < 128; ++k)
            aiSteerStep(edge, col, k);
        CHECK(edge.side > 8.5f && edge.steer == AiCar::Rails);
        TrackCollision oneSided = straightRoad(200);
        for (auto &q : oneSided.road)
            q.extra[0] = 0x20; // two lanes left, none right
        AiCar right = steer;
        aiSteerStep(right, oneSided, 0);
        CHECK(right.side > 0 && right.steer == AiCar::Slide);
    }
    // The controllers steer by the spin about the body's up (+0x4FC: the
    // spins +0xE4, +0xE8, +0xEC along the up row, car_ai_physics
    // 0x4088f0), and their yaw force goes into the yaw x the up's y
    // (ai_integrate 0x4071a0, through the body's rows).
    {
        AiCar tilt = steer;
        tilt.yawRate = 0.1f;
        tilt.roll = 0.03f; // a twelfth of a radian or so, rolled on the flat road
        AiCar after = tilt;
        aiTilt(after, 0, col);
        std::array<float, 3> f{};
        std::array<float, 3> r{};
        std::array<float, 3> u{};
        aiTiltedAxes(col, after, f, r, u);
        CHECK(after.spinX != 0 || after.spinZ != 0);
        const float spinUp = after.spinX * u[0] + 0.1f * u[1] + after.spinZ * u[2];
        aiSteerStep(tilt, col, 0);
        const float wanted = std::clamp(-tilt.headingError * 2.5f, -0.8f, 0.8f);
        const float yaw = 0.1f + std::clamp(-(spinUp - wanted) * 5.0f, -0.8f, 0.8f) * u[1] / 32;
        CHECK(tilt.steer == AiCar::Rails && std::fabs(tilt.yawRate - yaw) < 1e-6f && u[1] < 0.99f);
    }
    // On a bend the velocity is kept as the slice is left: running straight
    // on, the car is carried wide of the turned road and pushed back.
    TrackCollision bent = straightRoad(60);
    for (size_t i = 30; i < bent.road.size(); ++i) {
        const float a = 0.05f * static_cast<float>(i - 29);
        bent.road[i].forward = {std::sin(a), 0, std::cos(a)};
        bent.road[i].right = {std::cos(a), 0, -std::sin(a)};
    }
    // The game keeps the velocity in the world (+0xA4, car_move 0x4a2c60):
    // on to slice 30 it is the same vector, seen along the new right.
    AiCar turn;
    turn.slice = 29;
    turn.along = 0.99f;
    turn.halfWidth = 1;
    turn.sideKept = true;
    aiSetWorld(turn, bent, {0, 0, 30}, {0, 0, 1});
    for (int k = 1; k < 64 && turn.slice == 29; k += 2)
        aiSteerStep(turn, bent, k); // odd steps: moved only
    CHECK(turn.slice == 30 && std::fabs(turn.sideSpeed - 30 * std::sin(-0.05f)) < 1e-3f &&
          std::fabs(turn.heading + 0.05f) < 1e-3f && aiVelocity(bent, turn)[2] == 30);
    // car_move (0x4a2c60): the place moves by the world velocity / 64 each
    // step, whatever the slices; the speed (+0x7C0, 0x42cdd0) is the
    // velocity along the road's forward.
    {
        AiCar m;
        m.slice = 10;
        m.along = 0.5f;
        m.lateral = 1;
        m.halfWidth = 1;
        m.sideKept = true;
        aiSetWorld(m, col, {2, 0, 32}, {0, 0, 1});
        CHECK(m.speed == 32 && m.sideSpeed == 2);
        aiAdvance(m, col);
        CHECK(m.slice == 10 && std::fabs(m.along - (0.5f + 0.5f / 4)) < 1e-5f &&
              std::fabs(m.lateral - (1 + 2.0f / 64)) < 1e-5f && std::fabs(m.side - m.lateral) < 1e-5f);
        // A view written (a stop) sets the world velocity from it.
        m.speed = 0;
        m.sideSpeed = 0;
        CHECK(aiVelocity(col, m) == (std::array<float, 3>{0, 0, 0}));
    }
    // The gap between two slices' runs on the outside of a bend: each run
    // is its point + lateral x its right + along x the chord, so with the
    // right turned 0.1 a place 10 out past slice 30's run is before slice
    // 31's start; it stays on 30 with along over 1, at the same place.
    {
        TrackCollision kink = straightRoad(60);
        for (size_t i = 31; i < kink.road.size(); ++i) {
            kink.road[i].right = {std::cos(0.1f), 0, std::sin(0.1f)}; // turned left
            kink.road[i].forward = {-std::sin(0.1f), 0, std::cos(0.1f)};
        }
        AiCar m;
        m.slice = 30;
        m.along = 0.9f;
        m.lateral = 9;
        m.halfWidth = 0.5f;
        m.sideKept = true;
        const std::array<float, 3> at{9, 0, 4 * 31.0f + 0.2f};
        aiLocate(m, kink, at);
        const auto &a = kink.road[30];
        const auto &b = kink.road[31];
        CHECK(m.slice == 30 && m.along > 1 && std::fabs(m.lateral - 9) < 1e-4f &&
              std::fabs(a.position[2] + (b.position[2] - a.position[2]) * m.along - at[2]) < 1e-4f);
        aiLocate(m, kink, {9, 0, 4 * 31.0f + 2});
        CHECK(m.slice == 31 && m.along >= 0 && m.along < 1 && m.lateral > 9);
    }
    // The heading error (0x4070c0) takes the heading as +0x210, the body's
    // yaw x 1024 chopped (0x42ce70): turned a third of a 1024th to the
    // right the car still counts as straight.
    {
        AiCar m = steer;
        m.slice = 20;
        m.along = 0;
        m.lateral = m.side = m.offset = 0;
        aiSetWorld(m, col, {0, 0, 30}, {std::sin(0.002f), 0, std::cos(0.002f)});
        aiSteerStep(m, col, 0);
        CHECK(m.headingError == 0);
    }
}

} // namespace

static const reluva::test::Suite
    kSuite("nfs.unit.computer",
           {
               {"computer gears", [](const stdfs::path &) { testComputerGears(); }},
               {"computer air", [](const stdfs::path &) { testComputerAir(); }},
               {"computer driving", [](const stdfs::path &) { testComputerDriving(); }},
               {"computer catch up and cops", [](const stdfs::path &) { testComputerCatchUpAndCops(); }},
               {"computer pursuits", [](const stdfs::path &) { testComputerPursuits(); }},
               {"car hits", [](const stdfs::path &) { testCarHits(); }},
               {"cop calls", [](const stdfs::path &) { testCopCalls(); }},
           });
