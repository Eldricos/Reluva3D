// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A car: its setup from the game's files, and Reluva's simulation of it.
//
// The setup's numbers are the game's own (tune/vehicle/<car>.vehCarSim,
// the wheels' .mtx placements, bound/<car>_BOUND.bnd); how they're used is
// Reluva's model, worked out to drive like the game rather than taken
// from it (its program is encrypted). See docs/engines/age-driving.md for
// what each number does here and what's still to compare with the game.

#include "common/archive.h"
#include "engines/age/collision.h"
#include "engines/age/vecmath.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::age {

struct AxleSetup {
    float suspensionExtent = 0.15f; // metres the wheel travels each way
    float suspensionFactor = 1;     // spring stiffness, 1 = holds the car at mid travel
    float suspensionDamp = 0.1f;
    float steeringLimit = 0.4f; // radians at full lock
    float brakeCoef = 0.6f;
    float handbrakeCoef = 0;
    float staticFriction = 3, slidingFriction = 2.7f;
};

struct CarSetup {
    std::string name; // "vpbug"
    float mass = 1000;
    Vec3 inertiaBox{2, 2, 3}; // the box whose inertia the car's is: width, height, length
    Vec3 centerOfGravity{};
    float boundFriction = 0.5f, boundElasticity = 0.5f;
    int drivetrain = 0; // 0: rear wheels driven, 1: front, 2: all four
    float drag = 0.5f;

    float maxHorsePower = 250, idleRpm = 750, optRpm = 5800, maxRpm = 8500;
    int gears = 6;
    // Speeds (metres a second) at the top of first gear, top gear and
    // reverse: Trans's Low, High and Reverse, read as miles an hour.
    float lowSpeed = 9, highSpeed = 40, reverseSpeed = 13;
    float gearChangeTime = 0.8f;

    AxleSetup front, back;
    // Front left, front right, back left, back right: each wheel's centre
    // at rest and its radius.
    std::array<Vec3, 4> wheels{};
    std::array<float, 4> wheelRadius{0.33f, 0.33f, 0.33f, 0.33f};
    std::vector<Vec3> hull; // the body's collision shape's corners

    // Engine power (watts) at an RPM, full throttle.
    float power(float rpm) const;
    // The speed at the top of a gear (1..gears; -1 reverse).
    float gearTopSpeed(int gear) const;
};

// tune/vehicle/<car>.vehCarSim, geometry/<car>_WHL0-3.mtx and
// bound/<car>_BOUND.bnd.
Error loadCarSetup(const SearchSet& files, const std::string& car, CarSetup& out);

struct CarControls {
    float throttle = 0; // 0..1
    float brake = 0;    // 0..1; held when stopped, reverses
    float steer = 0;    // -1 (left) .. 1 (right)
    bool handbrake = false;
};

class CarSim {
public:
    struct Wheel {
        bool contact = false;
        float compression = 0; // metres up from fully extended
        float steer = 0;       // radians, to the left positive
        float spin = 0;        // radians turned, for drawing
        Vec3 groundNormal{0, 1, 0};
    };

    void reset(const CarSetup& setup, const Vec3& position, float yaw);
    // Advances by `dt` seconds in fixed steps.
    void step(const CarControls& controls, const CollisionWorld& world, float dt);

    const CarSetup& setup() const { return setup_; }
    Vec3 position{};          // of the body's origin
    Mat3 orientation;         // the body's axes: X right, Y up, Z back (cars face -Z)
    Vec3 velocity{}, angularVelocity{};
    float rpm = 0;
    int gear = 1; // -1 reverse, 1.. forward
    std::array<Wheel, 4> wheels{};

    // Along the car's nose, metres a second (negative backing up).
    float forwardSpeed() const { return -dot(velocity, orientation.z); }
    // Where a wheel's centre is now (its suspension and the body's place).
    Vec3 wheelCenter(int i) const;

private:
    void substep(const CarControls& controls, const CollisionWorld& world, float h);
    void collideBody(const CollisionWorld& world);
    Vec3 inverseInertiaTimes(const Vec3& torque) const;

    CarSetup setup_;
    float leftover_ = 0;     // time not stepped yet
    float shiftTimer_ = 0;   // no drive while changing gear
    float steerAngle_ = 0;   // eased towards the controls
};

} // namespace reluva::age
