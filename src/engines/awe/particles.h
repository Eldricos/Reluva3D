// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ac2.h"

#include <cstdint>
#include <string>
#include <vector>

// The scenes' particles (PART, RAIN, SNOW objects): snow falling past the
// windows, rain, breath in the cold. All three are one class in the game
// (motoe.exe: update 0x42cc20, draw 0x42d470); they differ only in their
// defaults. Each emits bursts of particles that move by one of three rules
// and are drawn as Direct3D point sprites. See docs/formats/awe/ac2.md.

namespace reluva::awe {

struct ParticleSettings {
    std::string texture;
    float position[3] = {0, 0, 0}; // the emitter, in the world (y up)
    float rotation[3] = {0, 0, 0}; // rotX, rotY, rotZ: turn mode 0's motion
    float depth = 0;               // where it's drawn among the 2D layers (d0, else z)
    std::string condition;         // when it's there ("weather >0")
    bool alphaBlend = false;       // else added
    float colour[3] = {1, 1, 1};   // df0
    float velocity[3] = {10, 50, 10}; // dc8: mode 2's speed; mode 0's spread
    int burst = 5;                 // dc4: particles per burst
    float interval = 0.1f;         // dd4: seconds between bursts
    int copies = 1;                // dec: times each is drawn
    int spread = 1;                // dea: emitted within +-spread of the emitter (x, z)
    float bounce = 0.25f;          // de4: speed kept at a bounce
    int bounces = 3;               // de8
    float size = 4;                // ddc: point size, world units
    float life = 99;               // e00: seconds
    int mode = 0;                  // e04: 0 thrown, 1 still, 2 falling
    bool blink = false;            // e14: on for e18 s, off for e1c s
    float onTime = 3, offTime = 5;
    bool screenSpace = false;      // e28: drawn on the screen, not through the camera
    float sway = 0, swaySpeed = 0; // e0c, e10: side to side, sin(angle) * sway
    float gravity = 9.8f;          // e08
    float fadeIn = 0, fadeOut = 0; // e40, e44: seconds
    int signX = 0, signY = 1, signZ = 0; // e38, e3a, e3c: mode 0's directions

    // From a scene's object, over its class's defaults.
    static ParticleSettings fromRecord(const Ac2Record& o);
};

class ParticleSystem {
public:
    explicit ParticleSystem(ParticleSettings settings, uint32_t seed = 1);
    const ParticleSettings& settings() const { return s_; }
    // Moves the particles on by `dt` seconds, emitting new ones.
    void update(float dt);

    struct Point {
        float position[3];
        uint8_t rgba[4];
    };
    // What's drawn, each particle `copies` times over.
    void points(std::vector<Point>& out) const;
    size_t count() const { return particles_.size(); }

private:
    struct Particle {
        int bounces = 0;
        float position[3]{}, velocity[3]{}, origin[3]{}, start[3]{};
        float born = 0, swayAngle = 0;
        bool dead = false;
    };
    int random(); // as the C library's rand(): 0..32767
    float spreadRandom(float centre);
    void emit();

    ParticleSettings s_;
    std::vector<Particle> particles_;
    float time_ = 0, nextBurst_ = 0, blinkLeft_ = 0;
    bool on_ = true;
    uint32_t seed_;
};

} // namespace reluva::awe
