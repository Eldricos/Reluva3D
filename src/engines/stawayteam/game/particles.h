// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stawayteam/formats/text_object.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * A value that moves over time (a `CEnvelope`): from its Start Value
 * toward its End Value at its Speed a second; there it stops, starts again
 * or turns back, as its Ending Behavior says (0x4aa500). At a Speed of 0
 * it's its Start Value (docs/engines/stawayteam/systems/particles.md).
 */
struct Envelope {
    enum class Ending { kStop, kRestart, kReverse };

    float start = 0.0f;
    float end = 0.0f;
    float speed = 0.0f;
    Ending ending = Ending::kStop;
    float value = 0.0f;     // now
    float direction = 1.0f; // +1 or -1, toward the end
    bool toEnd = true;      // going toward its End Value, not back to its Start

    /** Reads it from its object (nullptr: 0 throughout), set going from its start. */
    static Envelope read(const TextObject *o);
    /** Back to its start, heading for its end (0x4aa5d0). */
    void reset();
    void update(float seconds);
    /** Its value now: its Start Value at a Speed of 0 (0x5258c0). */
    float now() const { return speed == 0.0f ? start : value; }
};

/**
 * A particle system (a `CGenericParticleSystemAI`): bursts of particles,
 * each kind's Num Particles at a time, at its Burst Frequency a second,
 * leaving from its start shape in a cone, at its speed, for its age
 * (0x524920). Particles are kept about its emitter point: across the
 * ground as if seen from above (`u` across, `v` down the map, before the
 * map's foreshortening) and `z` up.
 */
class ParticleSystem {
public:
    /** How a kind of particle moves (its CParticleInfo's AI). */
    enum class Motion {
        kFloat,   // straight on at its speed (CFloatParticleAI: the base particle's)
        kGravity, // falling by its Gravity, bouncing (CGravityParticleAI)
        kSwirl,   // turning about the emitter point (CSwirlParticleAI)
    };
    /** One of its Particles: a CParticleInfo. */
    struct Kind {
        std::string model; // of Cache/Models
        Motion motion = Motion::kFloat;
        float gravity = 0.0f;         // its AI's Gravity
        float angular = 0.0f;         // a swirl's Angular Velocity, radians a second
        float angularVariance = 0.0f; // and its Angular Velocity Variance
        bool animated = false;        // its frames played...
        bool obeyFps = false;         // ...at its sequence's rate, over and over, else once over its life
        int count = 0;                // Num Particles each burst
        int countVariance = 0;
        float bounceFactor = 0.0f; // its model's, for a falling one: kept of its fall as it bounces
        float bounceFriction = 0.0f;
    };
    /** One particle. */
    struct Particle {
        size_t kind = 0;
        float u = 0.0f; // where it is about the emitter point, unsquashed, and how high
        float v = 0.0f;
        float z = 0.0f;
        float vu = 0.0f; // its speed along each, a second
        float vv = 0.0f;
        float vz = 0.0f;
        float age = 0.0f;   // seconds it's been going
        float life = 0.0f;  // seconds it goes for
        float omega = 0.0f; // a swirl's turn, radians a second
    };

    /** The system from its object, its random numbers from `seed`. */
    ParticleSystem(const TextObject &ai, uint32_t seed);

    /**
     * Its envelopes moved on, its particles moved and aged, and the bursts
     * due made; `ownerAngle` the facing (radians) of what it's on, for one
     * that Orients To Parent's Rotation. Returns how many bursts it made.
     */
    int update(float seconds, float ownerAngle = 0.0f);

    const std::vector<Kind> &kinds() const { return _kinds; }
    Kind &kind(size_t i) { return _kinds[i]; }
    const std::vector<Particle> &particles() const { return _particles; }
    /** Its emitter point from what it's on: its X Offset, Y Offset (map pixels) and Height. */
    ///@{
    float offsetX() const { return _offsetX; }
    float offsetY() const { return _offsetY; }
    float height() const { return _height; }
    ///@}
    /** Its Sounds, one of which is played with each burst. */
    const std::vector<std::string> &sounds() const { return _sounds; }
    /**
     * Which of its Sounds its last burst plays, by `random` (any number)
     * or, with its Choose Sound Based On Burst, by how many particles it
     * made of the most it could (0x524fd2); -1 if it has none.
     */
    int burstSound(uint32_t random) const;
    /** Whether it still bursts: Enabled, and not past its Num Bursts To Stop After. */
    bool enabled() const { return _enabled; }
    /**
     * A "Particle System On" message: enabled, and started again, its
     * envelopes from their starts and its bursts counted afresh (0x525410,
     * 0x524870). Off, it makes no more.
     */
    void turn(bool on);

    /** The map's foreshortening, the ground's Y To X Ratio (0x616fbc). */
    static constexpr float kYToX = 0.71276593f;
    /** Gravity's units in pixels a second each second (0x627470). */
    static constexpr float kGravityScale = 20.749319869f;

private:
    enum class Shape { kPoint, kLine, kDisk, kCylinder };

    void restart();
    float random(float low, float high);
    int randomInt(int low, int high);
    void burst(float ownerAngle);

    std::vector<Kind> _kinds;
    std::vector<Particle> _particles;
    std::vector<std::string> _sounds;
    Envelope _outerCone;
    Envelope _innerCone;
    Envelope _faceAngle;
    Envelope _trajectory;
    Envelope _speed;
    Envelope _age;
    float _speedVariance = 0.0f;
    float _ageVariance = 0.0f;
    float _frequency = 0.0f;
    float _frequencyVariance = 0.0f;
    int _stopAfter = 0;
    bool _orient = false;
    float _offsetX = 0.0f;
    float _offsetY = 0.0f;
    float _height = 0.0f;
    Shape _shape = Shape::kPoint;
    Envelope _inside;  // a disk's and cylinder's radius, and a line's Center Length
    Envelope _outside; // a disk's and cylinder's
    Envelope _length;  // a cylinder's Height of Cylinder
    float _yxRatio = 1.0f;
    bool _useCone = false;
    bool _enabled = true;
    float _bursts = 0.0f; // bursts due, the whole ones made as they come
    float _rate = 0.0f;   // bursts a second now: its frequency with its variance
    int _made = 0;        // bursts made so far
    bool _soundByBurst = false;
    int _lastMade = 0; // the last burst's particles, of...
    int _lastMost = 0; // ...the most it could have made
    uint32_t _random = 1;
};

} // namespace reluva::stawayteam
