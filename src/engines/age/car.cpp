// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Reluva's car simulation: a rigid body on four suspended wheels, with
// tyre forces that cancel a share of each wheel's sliding every step up
// to its grip, an engine and automatic gearbox, aerodynamic drag and
// collisions of the body's hull with the city. See
// docs/engines/age-driving.md.

#include "engines/age/car.h"

#include "engines/age/bound.h"
#include "engines/age/pkg.h"
#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>

namespace reluva::age {

namespace {

constexpr float kGravity = 9.8f;
constexpr float kStep = 1.0f / 120;        // seconds a step
constexpr float kWattsPerHorsePower = 745.7f;
constexpr float kMetresPerSecondPerMph = 0.44704f;
// The tuning's friction (3 for most tyres) over this is a friction
// coefficient: 1 on dry road, as a car's tyres have.
constexpr float kFrictionScale = 3;
constexpr float kHullRadius = 0.2f;     // spheres round the hull's corners
constexpr float kSteerRate = 3;         // full lock and back, per second
constexpr float kRolling = 0.015f;      // of a wheel's load
constexpr float kEngineBraking = 0.04f; // likewise

std::string textOf(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

void readAxle(const TuneNode* n, AxleSetup& a) {
    if (!n)
        return;
    a.suspensionExtent = std::max(0.02f, n->number("SuspensionExtent", 0, a.suspensionExtent));
    a.suspensionFactor = n->number("SuspensionFactor", 0, a.suspensionFactor);
    a.suspensionDamp = n->number("SuspensionDampCoef", 0, a.suspensionDamp);
    a.steeringLimit = n->number("SteeringLimit", 0, a.steeringLimit);
    a.brakeCoef = n->number("BrakeCoef", 0, a.brakeCoef);
    a.handbrakeCoef = n->number("HandbrakeCoef", 0, a.handbrakeCoef);
    a.staticFriction = n->number("StaticFric", 0, a.staticFriction);
    a.slidingFriction = n->number("SlidingFric", 0, a.slidingFriction);
}

} // namespace

float CarSetup::power(float r) const {
    // Rising from a third at idle to full at the optimum, falling to two
    // thirds at the redline, none past it.
    float f;
    if (r > maxRpm)
        f = 0;
    else if (r >= optRpm)
        f = 1 - (r - optRpm) / std::max(1.0f, maxRpm - optRpm) / 3;
    else
        f = 1.0f / 3 + 2.0f / 3 * std::clamp((r - idleRpm) / std::max(1.0f, optRpm - idleRpm), 0.0f, 1.0f);
    return maxHorsePower * kWattsPerHorsePower * f;
}

float CarSetup::gearTopSpeed(int gear) const {
    if (gear < 0)
        return reverseSpeed;
    if (gears <= 1)
        return highSpeed;
    // Spread evenly in ratio between first and top gear.
    const float t = float(std::clamp(gear, 1, gears) - 1) / float(gears - 1);
    return lowSpeed * std::pow(highSpeed / lowSpeed, t);
}

Error loadCarSetup(const SearchSet& files, const std::string& car, CarSetup& out) {
    out = CarSetup{};
    out.name = car;
    std::vector<uint8_t> data;
    const std::string simName = "tune/vehicle/" + car + ".vehCarSim";
    if (Error err = files.read(simName, data))
        return err;
    TuneNode root;
    if (Error err = parseTune(textOf(data), root))
        return Error::fail(simName + ": " + err.message);
    const TuneNode* sim = root.child("vehCarSim");
    if (!sim)
        return Error::fail(simName + ": no vehCarSim block");
    out.mass = std::max(100.0f, sim->number("Mass", 0, out.mass));
    for (size_t i = 0; i < 3; ++i) {
        out.inertiaBox[i] = std::max(0.5f, sim->number("InertiaBox", i, out.inertiaBox[i]));
        out.centerOfGravity[i] = sim->number("CenterOfGravity", i);
    }
    out.boundFriction = sim->number("BoundFriction", 0, out.boundFriction);
    out.boundElasticity = sim->number("BoundElasticity", 0, out.boundElasticity);
    out.drivetrain = int(sim->number("DrivetrainType", 0, 0));
    if (const TuneNode* aero = sim->child("Aero")) {
        out.drag = aero->number("Drag", 0, out.drag);
        out.down = aero->number("Down");
    }
    if (const TuneNode* e = sim->child("Engine")) {
        out.maxHorsePower = e->number("MaxHorsePower", 0, out.maxHorsePower);
        out.idleRpm = e->number("IdleRPM", 0, out.idleRpm);
        out.optRpm = e->number("OptRPM", 0, out.optRpm);
        out.maxRpm = std::max(out.optRpm + 100, e->number("MaxRPM", 0, out.maxRpm));
    }
    if (const TuneNode* t = sim->child("Trans")) {
        out.gears = std::max(1, int(t->number("AutoNumGears", 0, float(out.gears))));
        out.lowSpeed = std::max(1.0f, t->number("Low", 0, 20) * kMetresPerSecondPerMph);
        out.highSpeed = std::max(out.lowSpeed, t->number("High", 0, 90) * kMetresPerSecondPerMph);
        out.reverseSpeed = std::max(1.0f, t->number("Reverse", 0, 30) * kMetresPerSecondPerMph);
        out.gearChangeTime = t->number("GearChangeTime", 0, out.gearChangeTime);
    }
    readAxle(sim->child("WheelFront"), out.front);
    readAxle(sim->child("WheelBack"), out.back);
    TuneNode stuck;
    if (!files.read("tune/vehicle/" + car + ".vehStuck", data) && !parseTune(textOf(data), stuck))
        if (const TuneNode* s = stuck.child("vehStuck")) {
            out.stuckTime = s->number("TimeThresh", 0, out.stuckTime);
            out.stuckDistance = s->number("PosThresh", 0, out.stuckDistance);
            out.stuckSpeed = s->number("MoveThresh", 0, out.stuckSpeed);
        }

    // The wheels where their placements put them, their radius from their
    // bounds' height.
    for (int i = 0; i < 4; ++i) {
        Mtx mtx;
        if (files.read("geometry/" + car + "_WHL" + std::to_string(i) + ".mtx", data) || readMtx(data, mtx))
            return Error::fail("no placement for " + car + "'s wheel " + std::to_string(i));
        out.wheels[size_t(i)] = mtx.origin;
        out.wheelRadius[size_t(i)] = std::max(0.1f, (mtx.max[1] - mtx.min[1]) / 2);
    }
    Bound bound;
    if (!loadBound(files, car, bound))
        out.hull = bound.vertices;
    if (out.hull.empty()) // a box between the wheels
        for (float x : {-0.9f, 0.9f})
            for (float z : {out.wheels[0][2] - 0.5f, out.wheels[2][2] + 0.5f})
                for (float y : {0.3f, 1.3f})
                    out.hull.push_back({x, y, z});
    return Error::success();
}

void CarSim::reset(const CarSetup& setup, const Vec3& at, float yaw) {
    setup_ = setup;
    orientation = Mat3::rotationY(yaw);
    // The wheels' bottoms on the ground.
    position = at + Vec3{0, setup.wheelRadius[0] - setup.wheels[0][1] + 0.05f, 0};
    velocity = angularVelocity = {0, 0, 0};
    rpm = setup.idleRpm;
    gear = 1;
    wheels = {};
    leftover_ = shiftTimer_ = steerAngle_ = stuckFor_ = 0;
}

Vec3 CarSim::wheelCenter(int i) const {
    const AxleSetup& axle = i < 2 ? setup_.front : setup_.back;
    const Vec3 local = setup_.wheels[size_t(i)] + Vec3{0, wheels[size_t(i)].compression - axle.suspensionExtent, 0};
    return position + orientation * local;
}

Vec3 CarSim::inverseInertiaTimes(const Vec3& torque) const {
    const Vec3& b = setup_.inertiaBox;
    const float m = setup_.mass / 12;
    const Vec3 inertia{m * (b[1] * b[1] + b[2] * b[2]), m * (b[0] * b[0] + b[2] * b[2]),
                       m * (b[0] * b[0] + b[1] * b[1])};
    const Vec3 local = orientation.inverseTimes(torque);
    return orientation * Vec3{local[0] / inertia[0], local[1] / inertia[1], local[2] / inertia[2]};
}

void CarSim::step(const CarControls& controls, const CollisionWorld& world, float dt) {
    // A manual change, once.
    if (manual && controls.shift > 0 && gear < setup_.gears) {
        gear = gear < 0 ? 1 : gear + 1;
        shiftTimer_ = setup_.gearChangeTime / 2;
    } else if (manual && controls.shift < 0) {
        if (gear > 1) {
            --gear;
            shiftTimer_ = setup_.gearChangeTime / 4;
        } else if (gear == 1 && std::fabs(forwardSpeed()) < 1) {
            gear = -1;
        }
    }
    CarControls held = controls;
    held.shift = 0;
    leftover_ += std::min(dt, 0.1f);
    while (leftover_ >= kStep) {
        substep(held, world, kStep);
        leftover_ -= kStep;
    }

    // Turned over and stuck: set upright, facing the same way, a little
    // above where it lay.
    const bool over = orientation.y[1] < 0.5f; // tilted past 60 degrees
    if (!over || length(velocity) > setup_.stuckSpeed) {
        stuckFor_ = 0;
        return;
    }
    if (stuckFor_ == 0)
        stuckAt_ = position;
    stuckFor_ += dt;
    if (stuckFor_ >= setup_.stuckTime) {
        if (length(position - stuckAt_) <= setup_.stuckDistance) {
            const Vec3 nose = -orientation.z;
            const float yaw = std::atan2(-nose[0], -nose[2]);
            orientation = Mat3::rotationY(yaw);
            position[1] += 1;
            velocity = angularVelocity = {0, 0, 0};
        }
        stuckFor_ = 0;
    }
}

void CarSim::substep(const CarControls& controls, const CollisionWorld& world, float h) {
    const float m = setup_.mass;
    const Vec3 com = position + orientation * setup_.centerOfGravity;
    Vec3 force{0, -kGravity * m, 0}, torque{0, 0, 0};
    auto applyAt = [&](const Vec3& f, const Vec3& point) {
        force += f;
        torque += cross(point - com, f);
    };

    // Steering eases to the controls, less lock the faster the car goes.
    const float speed = forwardSpeed();
    const float lock = 1 / (1 + std::fabs(speed) / 25);
    const float wanted = -controls.steer * lock; // left positive
    steerAngle_ += std::clamp(wanted - steerAngle_, -kSteerRate * h, kSteerRate * h);

    // The gearbox: holding the brake when stopped backs up, the throttle
    // then brakes; the throttle when stopped in reverse goes forward again.
    if (!manual && gear > 0 && controls.brake > 0.1f && controls.throttle < 0.1f && speed < 0.5f)
        gear = -1;
    else if (!manual && gear < 0 && controls.throttle > 0.1f && controls.brake < 0.1f && speed > -0.5f)
        gear = 1;
    const bool backwards = gear < 0 && !manual; // the pedals swap
    const float drive = backwards ? controls.brake : controls.throttle;
    const float braking = backwards ? controls.throttle : controls.brake;

    // The engine turns with the wheels.
    const float wheelRpm = std::fabs(speed) / setup_.gearTopSpeed(gear) * setup_.maxRpm;
    const float targetRpm = std::max(setup_.idleRpm, wheelRpm);
    rpm += (targetRpm - rpm) * std::min(1.0f, 10 * h);
    shiftTimer_ = std::max(0.0f, shiftTimer_ - h);
    if (!manual && gear > 0 && shiftTimer_ <= 0) {
        if (rpm > setup_.maxRpm * 0.93f && gear < setup_.gears) {
            ++gear;
            shiftTimer_ = setup_.gearChangeTime / 2;
        } else if (gear > 1 && rpm < setup_.optRpm * 0.5f) {
            --gear;
            shiftTimer_ = setup_.gearChangeTime / 4;
        }
    }
    float driveForce = 0;
    if (shiftTimer_ <= 0 && rpm < setup_.maxRpm)
        driveForce = drive * setup_.power(rpm) / std::max(std::fabs(speed), 3.0f) * (gear < 0 ? -1.0f : 1.0f);

    // Aero: drag against the motion, down force pressing it to the road.
    // Its top speed is top gear's, at the redline.
    const float airSpeed = length(velocity);
    force -= velocity * (airSpeed * setup_.drag);
    force -= orientation.y * (setup_.down * airSpeed * airSpeed);

    // The wheels.
    const int driven = setup_.drivetrain == 2 ? 4 : 2;
    for (int i = 0; i < 4; ++i) {
        Wheel& w = wheels[size_t(i)];
        const bool frontWheel = i < 2;
        const AxleSetup& axle = frontWheel ? setup_.front : setup_.back;
        const float radius = setup_.wheelRadius[size_t(i)];
        const float travel = 2 * axle.suspensionExtent;
        const Vec3 top = position + orientation * (setup_.wheels[size_t(i)] + Vec3{0, axle.suspensionExtent, 0});
        const Vec3 down = -orientation.y;
        CollisionWorld::RayHit hit;
        const float before = w.compression;
        w.steer = frontWheel ? steerAngle_ * axle.steeringLimit : 0;
        if (!world.raycast(top, down, travel + radius, hit)) {
            w.contact = false;
            w.compression = 0;
            w.slip = w.bump = 0;
            continue;
        }
        w.contact = true;
        w.groundNormal = hit.normal;
        w.surface = hit.surface;
        w.compression = std::clamp(travel + radius - hit.distance, 0.0f, travel);
        w.bump = (w.compression - before) / h;

        // A spring that holds a quarter of the car at mid travel, damped.
        const float k = m * kGravity / 4 / axle.suspensionExtent * std::max(0.2f, axle.suspensionFactor);
        const float c = 2 * std::sqrt(k * m / 4) * std::clamp(0.3f + axle.suspensionDamp, 0.2f, 1.0f);
        const float load = std::max(0.0f, k * w.compression + c * (w.compression - before) / h);
        // Pushing out of the ground (the car's own up would push a pitched
        // car along).
        applyAt(hit.normal * load, hit.point);

        // The tyre, in the ground's plane.
        const Vec3 n = hit.normal;
        Vec3 heading = Mat3::rotation(orientation.y, w.steer) * (-orientation.z);
        heading = normalized(heading - n * dot(heading, n));
        const Vec3 side = normalized(cross(heading, n));
        const Vec3 vp = velocity + cross(angularVelocity, hit.point - com);
        const float along = dot(vp, heading), across = dot(vp, side);
        const float share = m / 4;
        w.spin += along / radius * h;

        // The surface's grip relative to a road's (city/materials.mtl).
        const float surfaceGrip = hit.surface.friction / CollisionSurface{}.friction;
        float grip = axle.staticFriction / kFrictionScale * surfaceGrip;
        float lateral = -across * share / h * 0.5f;
        // Rolling resistance (1.5% of the load, a road tyre's), and the
        // engine holding the car back off the throttle in gear.
        const float rolling = kRolling + (drive == 0 && gear != 0 ? kEngineBraking : 0.0f);
        float longitudinal = -std::clamp(along, -1.0f, 1.0f) * rolling * load;
        const bool drives = setup_.drivetrain == 2 || (setup_.drivetrain == 1) == frontWheel;
        if (drives)
            longitudinal += driveForce / float(driven);
        float brake = braking * axle.brakeCoef;
        // Left alone and nearly stopped, a car stays put (as if in gear).
        if (drive == 0 && braking == 0 && std::fabs(speed) < 0.5f)
            brake = std::max(brake, 0.3f);
        if (controls.handbrake && !frontWheel) {
            brake += axle.handbrakeCoef;
            grip *= 0.45f; // the back slides out
        }
        if (brake > 0) {
            const float most = brake * share * kGravity * 1.5f;
            longitudinal += std::clamp(-along * share / h * 0.5f, -most, most);
        }
        // The engine pushes no harder than the grip left beside the
        // cornering: past it the wheel spins up, held near its best slip
        // (heard as a skid), rather than sliding as a locked or sideways
        // tyre does.
        float limit = grip * load;
        float spin = 0;
        if (drives && driveForce != 0 && brake <= 0) {
            const float room = std::sqrt(std::max(0.0f, limit * limit - lateral * lateral));
            if (std::fabs(longitudinal) > room) {
                spin = std::fabs(longitudinal) / std::max(room, 1.0f) - 1;
                longitudinal = std::copysign(room, longitudinal);
            }
        }
        // Within the friction circle; past it the tyre slides, with less grip.
        const float total = std::sqrt(lateral * lateral + longitudinal * longitudinal);
        w.slip = std::clamp(spin / 4, 0.0f, 1.0f);
        if (total > limit * 1.0001f) {
            limit = axle.slidingFriction / kFrictionScale * load * surfaceGrip *
                    (controls.handbrake && !frontWheel ? 0.45f : 1);
            lateral *= limit / total;
            longitudinal *= limit / total;
            // Sliding: sideways, and along too when braking past the grip.
            const float sliding = std::sqrt(across * across + (brake > 0 ? along * along : 0.0f));
            w.slip = std::max(w.slip, std::clamp(sliding / 10, 0.0f, 1.0f));
        }
        // A surface's drag (water, mud) slows the wheel.
        longitudinal -= along * share * hit.surface.drag;
        applyAt(heading * longitudinal + side * lateral, hit.point);
    }

    velocity += force * (h / m);
    angularVelocity += inverseInertiaTimes(torque) * h;
    angularVelocity = angularVelocity * (1 / (1 + 0.5f * h));
    position += velocity * h;
    const float turn = length(angularVelocity) * h;
    if (turn > 1e-7f) {
        orientation = Mat3::rotation(normalized(angularVelocity), turn) * orientation;
        orientation.orthonormalize();
    }
    collideBody(world);
}

void CarSim::collideBody(const CollisionWorld& world) {
    const float m = setup_.mass;
    // The spheres inside the hull's box, touching its faces, so the body is
    // as big as its bound (and a low car's clear of the road).
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const Vec3& v : setup_.hull)
        for (size_t k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], v[k]);
            hi[k] = std::max(hi[k], v[k]);
        }
    // And the middles of the box's front, back and sides, low and halfway
    // up, so that nothing narrow (a bollard, a cone) gets between them.
    std::vector<Vec3> points = setup_.hull;
    const Vec3 mid = (lo + hi) * 0.5f;
    for (float y : {lo[1], mid[1]}) {
        points.push_back({mid[0], y, lo[2]});
        points.push_back({mid[0], y, hi[2]});
        points.push_back({lo[0], y, mid[2]});
        points.push_back({hi[0], y, mid[2]});
    }
    for (const Vec3& original : points) {
        Vec3 corner = original;
        for (size_t k = 0; k < 3; ++k)
            corner[k] = std::clamp(corner[k], std::min(lo[k] + kHullRadius, (lo[k] + hi[k]) / 2),
                                   std::max(hi[k] - kHullRadius, (lo[k] + hi[k]) / 2));
        const Vec3 p = position + orientation * corner;
        CollisionWorld::Contact c;
        if (!world.sphereContact(p, kHullRadius, c))
            continue;
        // A contact facing mostly up (the edge of a kerb or a step the
        // body's corner has reached) is taken as ground: lifted out
        // straight up, it stops only the motion down into it, rather than
        // throwing the car up off the edge.
        if (c.normal[1] > 0.5f) {
            c.depth *= c.normal[1];
            c.normal = {0, 1, 0};
        }
        position += c.normal * c.depth;
        const Vec3 com = position + orientation * setup_.centerOfGravity;
        const Vec3 r = p - com;
        const Vec3 vp = velocity + cross(angularVelocity, r);
        const float vn = dot(vp, c.normal);
        if (vn >= 0)
            continue;
        const float denom = 1 / m + dot(cross(inverseInertiaTimes(cross(r, c.normal)), r), c.normal);
        const float e = std::min(setup_.boundElasticity, c.surface.elasticity);
        float j = -(1 + e) * vn / denom;
        if (c.surface.breakImpulse > 0 && j > c.surface.breakImpulse) {
            // A prop knocked loose: not a wall. The car loses what the
            // prop takes (an inelastic knock with a body of its mass).
            if (std::find(broke.begin(), broke.end(), c.surface.prop) == broke.end())
                broke.push_back(c.surface.prop);
            j *= c.surface.propMass / (m + c.surface.propMass) / (1 + e);
        }
        knock(j, p);
        velocity += c.normal * (j / m);
        angularVelocity += inverseInertiaTimes(cross(r, c.normal * j));
        // Friction along the surface.
        const Vec3 vt = vp - c.normal * vn;
        const float slide = length(vt);
        if (slide > 1e-4f) {
            const float jt = std::min(setup_.boundFriction * c.surface.friction * j, slide / denom);
            const Vec3 t = vt * (1 / slide);
            velocity -= t * (jt / m);
            angularVelocity -= inverseInertiaTimes(cross(r, t * jt));
        }
    }
}

std::array<Vec3, 3> CarSim::bumpers(float& radius) const {
    // Along the body between its wheels, at mid height, as wide as it is.
    const float front = setup_.wheels[0][2] - 0.4f, back = setup_.wheels[2][2] + 0.4f;
    radius = std::max(0.6f, std::fabs(setup_.wheels[0][0]) + 0.2f);
    std::array<Vec3, 3> out;
    for (int i = 0; i < 3; ++i) {
        const float z = front + (back - front) * float(i) / 2;
        out[size_t(i)] = position + orientation * Vec3{0, 0.7f, z};
    }
    return out;
}

void collideCars(CarSim& a, CarSim& b) {
    float ra, rb;
    const auto sa = a.bumpers(ra), sb = b.bumpers(rb);
    const float ma = a.setup().mass, mb = b.setup().mass;
    for (const Vec3& pa : sa)
        for (const Vec3& pb : sb) {
            Vec3 d = pb - pa;
            d[1] = 0; // pushed apart sideways, not up
            const float dist = length(d);
            const float depth = ra + rb - dist;
            if (depth <= 0 || dist < 1e-4f)
                continue;
            const Vec3 n = d * (1 / dist);
            // Apart in proportion to the other's mass.
            a.position -= n * (depth * mb / (ma + mb));
            b.position += n * (depth * ma / (ma + mb));
            const float closing = dot(b.velocity - a.velocity, n);
            if (closing < 0) {
                const float j = -1.3f * closing / (1 / ma + 1 / mb); // a little bounce
                const Vec3 at = (pa + pb) * 0.5f;
                a.knock(j, at);
                b.knock(j, at);
                a.velocity -= n * (j / ma);
                b.velocity += n * (j / mb);
            }
            return; // one push a step is enough
        }
}

} // namespace reluva::age
