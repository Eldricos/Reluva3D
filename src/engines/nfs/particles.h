// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs {

// The race's particles (sparks, dust, smoke...) as nfs3.exe makes, moves
// and draws them; docs/formats/nfs/col.md, "Particles". Positions and
// velocities are in the track's coordinates (y up), velocities in units a
// second; a particle's life is counted in its own ticks, every other game
// tick (32 a second).
using ParticleVec = std::array<float, 3>;

// A kind's template, the 128 bytes at 0x560b64 + kind × 128 (only the
// fields the code reads; the offsets are the template's).
struct ParticleTemplate {
    int count = 0;    // +0x00 a burst's count (0x4cac20)
    int life = 0;     // +0x04 ticks
    int throttle = 0; // +0x08 made only when (game tick / 2) % (throttle + 1) is 0
    float up = 0, along = 0, across = 0; // +0x10, +0x14, +0x18 speeds (below)
    float spread = 0;                    // +0x1c each speed × 1 ± spread/2 at random
    float wobbleTurn = 0, wobble = 0, wobbleGrowth = 0; // +0x20, +0x24, +0x28 a sideways push
    float gravity = 0;  // +0x2c added to the upward speed each tick
    float spin = 0;     // +0x30 turns a tick, × the scale
    float inherit = 0;  // +0x34 of the given velocity (a car's)
    float drag = 1;     // +0x38 speeds, spin and wobble × this each tick
    float size = 0;     // +0x40 half the picture's diagonal, in units
    float growth = 0;   // +0x44 added to the size over the life, × the scale
    float sizeSpread = 0; // +0x48 size × (1 + random × this)
    uint32_t colour = 0;  // +0x4c ARGB at birth
    uint32_t fade = 0;    // +0x50 ARGB taken off, byte by byte, over the life
    uint16_t flags = 0;   // +0x58 to the polygon (bit 0: added, inferred)
    bool streak = false;  // +0x5c drawn as a streak, not a picture
    float reach = 0;      // +0x60 not drawn farther than this from the camera
};

// The templates of the kinds wall hits make: 2 (dust), 3 (sparks) and 6
// (smoke); nullptr for the others.
const ParticleTemplate* particleTemplate(int kind);

// The particle kind of a wall's side code (0x553740): 3 sparks, 2 dust,
// 0 none.
int wallParticleKind(int code);

// The game's random numbers come from 0x4e0d88, a generator not
// reproduced here; this one stands in. unit() is as 0x4eb230 makes it,
// the low 16 bits / 65536. Values put in `script` come out first (tests).
struct ParticleRandom {
    uint32_t seed = 1;
    std::vector<uint32_t> script;
    uint32_t next();
    float unit() { return static_cast<float>(next() & 0xffff) / 65536.0f; }
};

// One of the 600 slots at 0x7ddfd0 (128 bytes each; the offsets are its).
struct Particle {
    int life = 0;     // +0x00 0: free
    int kind = 0;     // +0x04
    int lifeMax = 0;  // +0x08
    ParticleVec position{}, velocity{}; // +0x0c, +0x18
    float size = 0, baseSize = 0, growth = 0; // +0x24, +0x28, +0x2c
    float angle = 0, spin = 0;                // +0x30 turns, +0x34
    float wobbleAngle = 0, wobbleTurn = 0, wobble = 0, wobbleGrowth = 0; // +0x40-+0x4c
    uint32_t colour = 0; // +0x54 ARGB
    int children = 0;    // +0x7e sparks made when it dies (0x4cab60)
    bool halved = false; // +0x7d not made, or not drawn, at Horizon Low
    // The streak's tail (the game keeps it on the screen, +0x58, per view,
    // and whether it was drawn last frame, +0x78); here in the world.
    ParticleVec tail{};
    bool tailSet = false;
};

class ParticleSystem {
public:
    static constexpr int kSlots = 600;
    // 0x4ca850: a particle of `kind` at `at`; velocity
    //   scale × (along' × a + across' × b) + scale × up' on y + inherit × c,
    // each speed' the template's × 1 ± spread/2. `tick` is the game's
    // (0x7d3684), `horizon` its Horizon option (0 High, 1 Low, 2 Off).
    // nullptr when it isn't made.
    Particle* spawn(int kind, const ParticleVec& at, float scale, const ParticleVec& a, const ParticleVec& b,
                    const ParticleVec& c, uint32_t tick, ParticleRandom& random, int horizon = 0);
    // 0x4cae00: one particle rising from `at`, spinning either way.
    void spawnOne(int kind, const ParticleVec& at, uint32_t tick, ParticleRandom& random, int horizon = 0);
    // 0x4cac20: a burst of sparks (kind 3) thrown from a car's contact
    // point. `speed` is what the caller passes (the car's speed / 2).
    void burst(const ParticleVec& at, const ParticleVec& carPosition, const ParticleVec& carVelocity, float speed,
               uint32_t tick, ParticleRandom& random, int horizon = 0);
    // 0x4cb2c0, run once a game tick (64 a second).
    void step(uint32_t tick, ParticleRandom& random, int horizon = 0);
    // A free slot, else the one with the least life (0x4ca4b0).
    Particle& slot();
    std::array<Particle, kSlots>& particles() { return slots_; }
    const std::array<Particle, kSlots>& particles() const { return slots_; }
    int live() const;
    void clear() { slots_ = {}; }

private:
    std::array<Particle, kSlots> slots_{};
};

// The burst's count (0x4cac20): the template's 30, scaled down below 20.
int burstCount(float speed);
// A particle's colour at a part of its life (0-1), as 0x4cb2c0 sets it:
// each byte of the template's colour less its fade byte × part, rounded.
uint32_t particleColour(const ParticleTemplate& t, float part);

// A car's wall hit, as 0x42ac40 makes its particles (col.md, Wall hits):
// `code` the side's code, `speed` the car's speed (+0xb8), `impact`
// (+0x168), `rough` its +0x15c.
struct WallHit {
    int code = 0;
    float speed = 0, impact = 0, rough = 0;
    ParticleVec contact{}, carPosition{}, carVelocity{};
};
void wallHitParticles(ParticleSystem& system, const WallHit& hit, uint32_t tick, ParticleRandom& random,
                      int horizon = 0);

// The streak's corners as 0x4cbc90 draws it (0x4cc0a6), here in the
// world: the game works in pixels, from the tail it keeps on the screen;
// `right` and `up` are the camera's, `pixel` the size of a pixel at the
// particle's depth, `half` its half size there in pixels. Drawn the frame
// before (tailSet): the tail twice, then the head ± (w, -h) (w, h the
// half size held to 0.5-1 pixel), and the tail moves halfway to the head;
// else a dot from the head, which becomes the tail.
std::array<ParticleVec, 4> streakCorners(Particle& p, const ParticleVec& right, const ParticleVec& up, float pixel,
                                         float half);
// A picture's corners (0x4cc235): a square turned by the particle's angle
// (turns), its corners `half` from the centre along the camera's right
// and up.
std::array<ParticleVec, 4> pictureCorners(const Particle& p, const ParticleVec& right, const ParticleVec& up,
                                          float half);

} // namespace reluva::nfs
