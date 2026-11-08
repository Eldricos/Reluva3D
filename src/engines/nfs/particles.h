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
// and up, in the order the picture's corners (0, 0), (1, 0), (1, 1),
// (0, 1) take them: at angle a, (sin a x aspect, cos a / aspect) right
// and down, (cos a, -sin a), and the opposites. `aspect` is the view's
// |sy| / sx (330.33 / 320 for the game's): the game works on the screen
// with the sine by the y scale, which is negative (THRASH list of
// e6-runs/t8d, the smoke's quads against the dumped angles).
std::array<ParticleVec, 4> pictureCorners(const Particle& p, const ParticleVec& right, const ParticleVec& up,
                                          float half, float aspect = 1);

// The weather's rain or snow (docs/formats/nfs/col.md, "Rain and snow"):
// up to 800 drops in a box round the camera, moved every other game tick
// (0x4dd5e0), drawn on the screen through a 120-degree view as streaks
// (rain, weather.fsh DRP1) or squares (snow, DRP2) (0x4de700, 0x4de070),
// and a drop that comes within 3 of the camera on the screen turned into
// a splash held on the screen (0x4dd930; SPL1, SPL2, drawn by 0x4ddb40).
// A kind's 0x34 bytes at 0x564290 + kind x 0x34 (static data; the
// conversions are 0x4dcdf0's).
struct WeatherKind {
    int fall = 0;         // +0x00 16.16; / 10 is the fall each update
    float flake = 0;      // +0x04 a snowflake's half size at the camera, pixels
    int countPercent = 0; // +0x08 of 800 drops
    int half = 0;         // +0x0c the box's half width, and the farthest drawn
    int top = 0;          // +0x10 the box above the camera
    int below = 0;        // +0x14 and below it
    int windDegrees = 0;  // +0x18 the wind's heading
    int windTenths = 0;   // +0x1c and its speed, tenths of a unit each update
    int grey = 0;         // +0x20 the drops' colour, in all four bytes
    int splashLife = 0;   // +0x24 updates
    float splashSize = 0; // +0x28 the splash's half diagonal, of the view's scales
    int splashGrey = 0;   // +0x2c the splash's colour at birth, all four bytes
    int splashPercent = 0; // +0x30 of 20 splashes
};
// The kind of an .hrz weather type (type % 3: 1 rain, 2 snow); nullptr
// for 0, no weather.
const WeatherKind* weatherKind(int type);

// What 0x4dcdf0 makes of the .hrz's weather (0x478340 reads it into
// 0x8ca1e8-0x8ca1fc and makes the four times running sums).
struct WeatherSetup {
    int kind = 0;           // 0 none, 1 rain, 2 snow (0x8ca22c)
    int count = 0;          // drops (0x8ca234)
    int splashes = 0;       // splash slots (0x8ca278)
    float half = 0, top = 0, bottom = 0; // the box round the camera (0x8ca25c, 0x8ca264, 0x8ca260)
    ParticleVec velocity{}; // each update (0x8ca26c)
    float flake = 0;        // 0x8ca230
    uint32_t colour = 0;    // ARGB (0x8ca238 in each byte)
    int splashLife = 0;     // 0x8ca27c
    float splashSize = 0;   // 0x8ca280
    int splashGrey = 0;     // 0x8ca284
    int startSlice = 0, endSlice = 0, fade = 0; // the stretch of road (0x8ca23c, 0x8ca240)
    std::array<int, 4> times{}; // ticks: fading off from, off from, fading on from, back on at (0x8ca1f0-0x8ca1fc)
};
WeatherSetup weatherSetup(int type, int startSlice, int endSlice, int fade, int stayOn, int fadeOff, int stayOff,
                          int fadeOn);
// The drops falling where the view's car is (0x4dd3d0): all of them with
// no stretch of road given, none outside it (it may wrap past the line),
// fewer within `fade` slices of its ends.
int weatherAtSlice(const WeatherSetup& s, int slice, int trackSlices);
// The drops falling `elapsed` ticks after the cycle began (0x4dd330):
// all, fading to none, none, fading back. With all four times set; at
// the cycle's end `restart` is set and all fall (the game takes the step
// as the cycle's new start).
int weatherAtTime(const WeatherSetup& s, int count, int elapsed, bool* restart = nullptr);
// The Special Effects option (0x6fbc30): half at Low, a quarter at Off
// (0x4dece0, signed division).
int weatherForEffects(int count, int sfxLevel);
// A snowflake's side on the screen, pixels, at depth w (0x4de070).
float snowflakeSide(const WeatherSetup& s, float w);
// The game's 16.16 sine and cosine of a 16-bit angle (0x4f2380, 0x4f23f0:
// a table of 1024 steps a turn, interpolated).
int fixedSin(uint32_t angle);
int fixedCos(uint32_t angle);

// The camera as the weather's pass has it: in the track's coordinates,
// the screen the game sets for it (0x4dbda0: 0x4bed20 with angle 0x78,
// 120 degrees, so sx 184.75 and sy -216.1 at 640 x 480).
struct WeatherView {
    ParticleVec eye{}, right{}, up{}, forward{};
    float sx = 184.752f, sy = -216.108f, cx = 320, cy = 240;
    float left = 0, rightEdge = 640, top = 0, bottom = 480;
};

// A polygon of the pass, on the screen (pixels, y down): a drop's
// triangle (3 corners), a flake's or a splash's square (4), at the depth
// its rhw gives (a splash's is 0.9, so 1 / 0.9), in its ARGB colour.
struct WeatherPoly {
    int corners = 0;
    std::array<std::array<float, 2>, 4> at{};
    float depth = 0;
    uint32_t colour = 0;
    bool splash = false;
};

class Weather {
public:
    static constexpr int kDrops = 800, kSplashes = 20;
    struct Drop {
        ParticleVec position{};             // 0x8c56e8 + 12 x i
        std::array<float, 2> tail{};        // 0x8c0be8: the streak's end on the screen
        bool hidden = false;                // +8: made while the camera was under cover
        bool tailSet = false;               // +9
    };
    struct Splash {
        float x = 0, y = 0;  // on the screen, before the view's scales (0x8c08c0)
        int life = 0;        // updates (0x8c08c8)
        float size = 0;      // 0x8c08cc
        float angle = 0;     // turns (0x8c08d0)
    };
    // 0x4dcdf0, then 0x4dd230: every drop at random in the box round the
    // camera, the splashes cleared; `step` begins the cycle.
    void start(const WeatherSetup& setup, const ParticleVec& eye, int step, ParticleRandom& random);
    // How many fall this step: by the view's car's slice, the time and the
    // option (0x4dece0).
    int active(int slice, int trackSlices, int step, int sfxLevel);
    // 0x4dd5e0, called each game tick; works on every other one. `covered`
    // is 0x420d60 for the camera.
    void update(int step, int active, const ParticleVec& eye, bool covered, ParticleRandom& random);
    // 0x4de700 (rain) or 0x4de070 (snow), then 0x4ddb40 for the splashes
    // when `splashes` (the option not Off, 0x4dedf0).
    void draw(const WeatherView& view, int active, bool splashes, ParticleRandom& random,
              std::vector<WeatherPoly>& out);
    const WeatherSetup& setup() const { return setup_; }
    std::array<Drop, kDrops>& drops() { return drops_; }
    std::array<Splash, kSplashes>& splashes() { return splashes_; }

private:
    void respawnTop(Drop& d, const ParticleVec& eye, ParticleRandom& random) const;
    bool splash(const WeatherView& view, const ParticleVec& at, ParticleRandom& random);
    WeatherSetup setup_;
    std::array<Drop, kDrops> drops_{};
    std::array<Splash, kSplashes> splashes_{};
    int last_ = -2, cycleStart_ = 0;
};

} // namespace reluva::nfs
