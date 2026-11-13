// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::nfs {

// The race's particles (sparks, dust, smoke...) as nfs3.exe makes, moves
// and draws them; docs/engines/nfs/games/nfs3/formats/col.md, "Particles". Positions and
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
    // +0x10, +0x14, +0x18 speeds (below)
    float up = 0;
    float along = 0;
    float across = 0;
    float spread = 0; // +0x1c each speed × 1 ± spread/2 at random
    // +0x20, +0x24, +0x28 a sideways push
    float wobbleTurn = 0;
    float wobble = 0;
    float wobbleGrowth = 0;
    float gravity = 0;         // +0x2c added to the upward speed each tick
    float spin = 0;            // +0x30 turns a tick, × the scale
    float inherit = 0;         // +0x34 of the given velocity (a car's)
    float drag = 1;            // +0x38 speeds, spin and wobble × this each tick
    float size = 0;            // +0x40 half the picture's diagonal, in units
    float growth = 0;          // +0x44 added to the size over the life, × the scale
    float sizeSpread = 0;      // +0x48 size × (1 + random × this)
    uint32_t colour = 0;       // +0x4c ARGB at birth
    uint32_t fade = 0;         // +0x50 ARGB taken off, byte by byte, over the life
    uint16_t flags = 0;        // +0x58 to the polygon (bit 0: added, inferred)
    bool streak = false;       // +0x5c drawn as a streak, not a picture
    float reach = 0;           // +0x60 not drawn farther than this from the camera
    float speedScale = 0;      // +0x0c a wheel's particles at scale |car speed| / this, at most 1 (0x4cb010)
    bool spinHeld = false;     // +0x3c the spin turned back past +-0.25 of a turn (0x4cb2c0)
    uint32_t colourRandom = 0; // +0x54 each byte, times a random part, taken off the colour at birth
};

// The templates of kinds 1-13 (0x560b64 + kind x 128, static data);
// nullptr for 0 and the rest. `direct3d`: kind 7 as 0x4cb670 sets it up
// for the Direct3D renderer (bit 0x40 of 0x7a3a58), colour 7F7F7F7F
// fading by 7F003F7F.
const ParticleTemplate *particleTemplate(int kind, bool direct3d = false);

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
    int life = 0;    // +0x00 0: free
    int kind = 0;    // +0x04
    int lifeMax = 0; // +0x08
    // +0x0c, +0x18
    ParticleVec position{};
    ParticleVec velocity{};
    // +0x24, +0x28, +0x2c
    float size = 0;
    float baseSize = 0;
    float growth = 0;
    // +0x30 turns, +0x34
    float angle = 0;
    float spin = 0;
    // +0x40-+0x4c
    float wobbleAngle = 0;
    float wobbleTurn = 0;
    float wobble = 0;
    float wobbleGrowth = 0;
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
    Particle *spawn(int kind, const ParticleVec &at, float scale, const ParticleVec &a, const ParticleVec &b,
                    const ParticleVec &c, uint32_t tick, ParticleRandom &random, int horizon = 0);
    // 0x4cae00: one particle rising from `at`, spinning either way.
    void spawnOne(int kind, const ParticleVec &at, uint32_t tick, ParticleRandom &random, int horizon = 0);
    // 0x4cac20: a burst of sparks (kind 3) thrown from a car's contact
    // point. `speed` is what the caller passes (the car's speed / 2).
    void burst(const ParticleVec &at, const ParticleVec &carPosition, const ParticleVec &carVelocity, float speed,
               uint32_t tick, ParticleRandom &random, int horizon = 0);
    // 0x4cb2c0, run once a game tick (64 a second).
    void step(uint32_t tick, ParticleRandom &random, int horizon = 0);
    // A free slot, else the one with the least life (0x4ca4b0).
    Particle &slot();
    std::array<Particle, kSlots> &particles() { return _slots; }
    const std::array<Particle, kSlots> &particles() const { return _slots; }
    int live() const;
    void clear() { _slots = {}; }
    // The renderer is Direct3D: kind 7's template as 0x4cb670 sets it.
    bool direct3d = false;

private:
    std::array<Particle, kSlots> _slots{};
};

// The effects intensity (SFX_INTENSITY, 0x6fbc38) as the Direct3D
// renderer applies it below 1: k = min(round(f x 65536), 0xFFFF) >> 8 and
// each byte of the ARGB colour x k >> 8 (the lights 0x492370, 0x49d800,
// the added clouds 0x47b7d0, the raindrops 0x4de700, the splashes
// 0x4ddb40, the snow 0x4de070). 1 or more: as it is.
uint32_t dimByIntensity(uint32_t colour, float intensity);
// The particles' own (0x4cbf2f for kinds with flag bit 0, the screen
// quads 0x4cb750): each byte x round(f x 65536) >> 16.
uint32_t dimParticle(uint32_t colour, float intensity);

// A row of the surface table at 0x553080 (0x24 bytes; 0x240 a weather,
// the car's +0x160: 0 dry, 1 rain, 2 snow), as 0x42a280 reads it for a
// wheel and 0x42ac40 for a landing.
struct SurfaceEffect {
    float level = 0;    // +0x00 thrown when the wheel's skid level is over this
    float speed = 0;    // +0x04 or, at or under it, when the car is faster than this (0: never)
    float range = 0;    // +0x08 the speed over `speed` that makes skid level 5
    int kind = 0;       // +0x0c the particle kind thrown (0 none)
    bool smoke = false; // +0x10 tyre smoke and skid marks (wheelSmokes, fce.h)
    int landing = 0;    // +0x14 the kind a landing throws
};
const SurfaceEffect &surfaceEffect(int surface, int weather);

// A car's particles at a wheel (0x4cb010): the kind's count (+0x00), each
// at scale min(|speed| / the kind's +0x0c, 1) (+0.5 for a rear wheel of a
// car with +0x594 set, but kind 12; 1 when +0x0c is 0), along the car's
// velocity made a unit vector, across its right row (against it for the
// left wheels 0 and 3, whose particles spin the other way), inheriting
// half its velocity. Kind 10 isn't made with a software renderer (bits 6
// of 0x7a3a58).
void wheelParticles(ParticleSystem &system, int kind, int wheel, const ParticleVec &at, const ParticleVec &velocity,
                    const ParticleVec &right, float speed, bool overspeed, uint32_t tick, ParticleRandom &random,
                    int horizon = 0, bool software = false);
// A wheel's spray (0x42a280, from car_spiked_effects 0x42a560 after the
// tyre smoke, each 64 Hz step, each wheel on the ground): the surface
// table's kind for the surface under the wheel and the car's weather;
// thrown when the wheel's skid level is over the row's level, or else
// when the row has a speed and the car (+0xB8) is faster: then the skid
// level becomes (speed - row speed) / range x 5, held to 0-5 (returned;
// otherwise `level` as it was). Nothing for kind 0.
float wheelSpray(ParticleSystem &system, int wheel, float level, int surface, int weather, const ParticleVec &at,
                 const ParticleVec &velocity, const ParticleVec &right, float speed, uint32_t tick,
                 ParticleRandom &random, int horizon = 0, bool software = false);
// The game's distance for sounds (0x410cb0): in whole units, the larger of
// |x| and |z| plus a quarter of the smaller, then that and |y| the same way.
int soundDistance(const ParticleVec &d);

// A hit between cars as the impacts task sees it (car_impacts_task
// 0x42b900 -> 0x42b780 -> 0x42ac40, hit type 5, +0x170 0x50005, set by
// 0x42f280): over strength 5 a burst of sparks (0x4cac20) at the point
// at the strength x 2 (0x5372d8), and one dust particle (kind 2,
// 0x4cae00) there. The hit's smoke timer (+0x8D4): a car hit harder than
// 30 (traffic, +0x200 bit 0x10, harder than 5) with no smoke running
// starts it at |round(strength x 0.15)| (0x5372d0), at the point taken
// into the car's frame x 0.6 (+0x8D8, `0x4e0050`).
struct CarSmoke {
    int timer = 0;       // +0x8D4
    ParticleVec point{}; // +0x8D8, in the car's frame (right, up, forward)
};
void carHitParticles(ParticleSystem &system, const ParticleVec &point, const ParticleVec &carPosition,
                     const ParticleVec &carVelocity, float strength, uint32_t tick, ParticleRandom &random,
                     int horizon = 0);
// The smoke's start (0x42ad15-0x42ad9b): true when it starts; `right`,
// `up` and `forward` are the car's rows (+0xC0, +0xCC, +0xD8).
bool startCarSmoke(CarSmoke &smoke, float strength, bool traffic, const ParticleVec &point,
                   const ParticleVec &carPosition, const ParticleVec &right, const ParticleVec &up,
                   const ParticleVec &forward);
// car_smoke_tick (0x42b480, each 64 Hz step): the timer counts up; while
// it runs a particle at the point (back in the world): kind 6 to 16, kind
// 9 to 64, then 0x4caea0's fire. It stops once the car is on the ground
// (+0x164 0) upright (+0x10C 0.5 or more) with its spins +0xE4 and +0xEC
// both under 0.2 (0x537310).
void carSmokeTick(ParticleSystem &system, CarSmoke &smoke, const ParticleVec &carPosition, const ParticleVec &right,
                  const ParticleVec &up, const ParticleVec &forward, const ParticleVec &carVelocity, bool airborne,
                  float upright, float spinE4, float spinEC, uint32_t tick, ParticleRandom &random, int horizon = 0);
// 0x4caea0, a car on fire: kinds 9 and 7 rising from the point, in the
// first 8 steps of each 48 a burst of sparks at speed 20 one time in two,
// and seven times in eight a kind 8 at scale (step % 48) / 48 inheriting
// half the car's velocity, its push's angle (+0x40) 0.
void carFire(ParticleSystem &system, const ParticleVec &at, const ParticleVec &carPosition,
             const ParticleVec &carVelocity, uint32_t tick, ParticleRandom &random, int horizon = 0);
// The Gen.bnk slot of a hit's sound (sound_impact_slot 0x4149e0), for
// category 5 and the material of a car hit (5, +0x170's low byte):
// by the loudness 0-127 (the queued loudness x 127 / 10, 0x416d90), 33
// over 90, 32 over 70, 37 over 50, 34 over 25, else 30; the same slot as
// the last one (`last`, 0x552d80) turns into another: 32 to 37 or 34 at
// random, 30 to 34, 34 to 30 or 37, 37 to 30 or 34; `last` is updated.
int carHitSoundSlot(int loudness, int &last, ParticleRandom &random);
// A one-shot hit's volume (0x416d90): 127 x (5625 - 2 d^2) / 5625 for the
// sound distance d (soundDistance), 0 from 53 units.
int hitSoundVolume(int distance);
// A sound's pan byte for a 16-bit azimuth (0x4182b0, stereo): 64 ahead,
// 127 at a quarter turn, 0 at three quarters.
int soundPan(uint32_t azimuth);

// The burst's count (0x4cac20): the template's 30, scaled down below 20.
int burstCount(float speed);
// A particle's colour at a part of its life (0-1), as 0x4cb2c0 sets it:
// each byte of the template's colour less its fade byte × part, rounded.
uint32_t particleColour(const ParticleTemplate &t, float part);

// A car's wall hit, as 0x42ac40 makes its particles (col.md, Wall hits):
// `code` the side's code, `speed` the car's speed (+0xb8), `impact`
// (+0x168), `rough` its +0x15c.
struct WallHit {
    int code = 0;
    float speed = 0;
    float impact = 0;
    float rough = 0;
    ParticleVec contact{};
    ParticleVec carPosition{};
    ParticleVec carVelocity{};
};
void wallHitParticles(ParticleSystem &system, const WallHit &hit, uint32_t tick, ParticleRandom &random,
                      int horizon = 0);

// The streak's corners as 0x4cbc90 draws it (0x4cc0a6), here in the
// world: the game works in pixels, from the tail it keeps on the screen;
// `right` and `up` are the camera's, `pixel` the size of a pixel at the
// particle's depth, `half` its half size there in pixels. Drawn the frame
// before (tailSet): the tail twice, then the head ± (w, -h) (w, h the
// half size held to 0.5-1 pixel), and the tail moves halfway to the head;
// else a dot from the head, which becomes the tail.
std::array<ParticleVec, 4> streakCorners(Particle &p, const ParticleVec &right, const ParticleVec &up, float pixel,
                                         float half);
// A picture's corners (0x4cc235): a square turned by the particle's angle
// (turns), its corners `half` from the centre along the camera's right
// and up, in the order the picture's corners (0, 0), (1, 0), (1, 1),
// (0, 1) take them: at angle a, (sin a x aspect, cos a / aspect) right
// and down, (cos a, -sin a), and the opposites. `aspect` is the view's
// |sy| / sx (330.33 / 320 for the game's): the game works on the screen
// with the sine by the y scale, which is negative (THRASH list of
// e6-runs/t8d, the smoke's quads against the dumped angles).
std::array<ParticleVec, 4> pictureCorners(const Particle &p, const ParticleVec &right, const ParticleVec &up,
                                          float half, float aspect = 1);

// The weather's rain or snow (docs/engines/nfs/games/nfs3/formats/col.md, "Rain and snow"):
// up to 800 drops in a box round the camera, moved every other game tick
// (0x4dd5e0), drawn on the screen through a 120-degree view as streaks
// (rain, weather.fsh DRP1) or squares (snow, DRP2) (0x4de700, 0x4de070),
// and a drop that comes within 3 of the camera on the screen turned into
// a splash held on the screen (0x4dd930; SPL1, SPL2, drawn by 0x4ddb40).
// A kind's 0x34 bytes at 0x564290 + kind x 0x34 (static data; the
// conversions are 0x4dcdf0's).
struct WeatherKind {
    int fall = 0;          // +0x00 16.16; / 10 is the fall each update
    float flake = 0;       // +0x04 a snowflake's half size at the camera, pixels
    int countPercent = 0;  // +0x08 of 800 drops
    int half = 0;          // +0x0c the box's half width, and the farthest drawn
    int top = 0;           // +0x10 the box above the camera
    int below = 0;         // +0x14 and below it
    int windDegrees = 0;   // +0x18 the wind's heading
    int windTenths = 0;    // +0x1c and its speed, tenths of a unit each update
    int grey = 0;          // +0x20 the drops' colour, in all four bytes
    int splashLife = 0;    // +0x24 updates
    float splashSize = 0;  // +0x28 the splash's half diagonal, of the view's scales
    int splashGrey = 0;    // +0x2c the splash's colour at birth, all four bytes
    int splashPercent = 0; // +0x30 of 20 splashes
};
// The kind of an .hrz weather type (type % 3: 1 rain, 2 snow); nullptr
// for 0, no weather.
const WeatherKind *weatherKind(int type);

// What 0x4dcdf0 makes of the .hrz's weather (0x478340 reads it into
// 0x8ca1e8-0x8ca1fc and makes the four times running sums).
struct WeatherSetup {
    int kind = 0;     // 0 none, 1 rain, 2 snow (0x8ca22c)
    int count = 0;    // drops (0x8ca234)
    int splashes = 0; // splash slots (0x8ca278)
    // the box round the camera (0x8ca25c, 0x8ca264, 0x8ca260)
    float half = 0;
    float top = 0;
    float bottom = 0;
    ParticleVec velocity{}; // each update (0x8ca26c)
    float flake = 0;        // 0x8ca230
    uint32_t colour = 0;    // ARGB (0x8ca238 in each byte)
    int splashLife = 0;     // 0x8ca27c
    float splashSize = 0;   // 0x8ca280
    int splashGrey = 0;     // 0x8ca284
    // the stretch of road (0x8ca23c, 0x8ca240)
    int startSlice = 0;
    int endSlice = 0;
    int fade = 0;
    std::array<int, 4> times{}; // ticks: fading off from, off from, fading on from, back on at (0x8ca1f0-0x8ca1fc)
};
WeatherSetup weatherSetup(int type, int startSlice, int endSlice, int fade, int stayOn, int fadeOff, int stayOff,
                          int fadeOn);
// The drops falling where the view's car is (0x4dd3d0): all of them with
// no stretch of road given, none outside it (it may wrap past the line),
// fewer within `fade` slices of its ends.
int weatherAtSlice(const WeatherSetup &s, int slice, int trackSlices);
// The drops falling `elapsed` ticks after the cycle began (0x4dd330):
// all, fading to none, none, fading back. With all four times set; at
// the cycle's end `restart` is set and all fall (the game takes the step
// as the cycle's new start).
int weatherAtTime(const WeatherSetup &s, int count, int elapsed, bool *restart = nullptr);
// The Special Effects option (0x6fbc30): half at Low, a quarter at Off
// (0x4dece0, signed division).
int weatherForEffects(int count, int sfxLevel);
// A snowflake's side on the screen, pixels, at depth w (0x4de070).
float snowflakeSide(const WeatherSetup &s, float w);
// The game's 16.16 sine and cosine of a 16-bit angle (0x4f2380, 0x4f23f0:
// a table of 1024 steps a turn, interpolated).
int fixedSin(uint32_t angle);
int fixedCos(uint32_t angle);

// The camera as the weather's pass has it: in the track's coordinates,
// the screen the game sets for it (0x4dbda0: 0x4bed20 with angle 0x78,
// 120 degrees, so sx 184.75 and sy -216.1 at 640 x 480).
struct WeatherView {
    ParticleVec eye{};
    ParticleVec right{};
    ParticleVec up{};
    ParticleVec forward{};
    float sx = 184.752f;
    float sy = -216.108f;
    float cx = 320;
    float cy = 240;
    float left = 0;
    float rightEdge = 640;
    float top = 0;
    float bottom = 480;
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
    static constexpr int kDrops = 800;
    static constexpr int kSplashes = 20;
    struct Drop {
        ParticleVec position{};      // 0x8c56e8 + 12 x i
        std::array<float, 2> tail{}; // 0x8c0be8: the streak's end on the screen
        bool hidden = false;         // +8: made while the camera was under cover
        bool tailSet = false;        // +9
    };
    struct Splash {
        // on the screen, before the view's scales (0x8c08c0)
        float x = 0;
        float y = 0;
        int life = 0;    // updates (0x8c08c8)
        float size = 0;  // 0x8c08cc
        float angle = 0; // turns (0x8c08d0)
    };
    // 0x4dcdf0, then 0x4dd230: every drop at random in the box round the
    // camera, the splashes cleared; `step` begins the cycle.
    void start(const WeatherSetup &setup, const ParticleVec &eye, int step, ParticleRandom &random);
    // How many fall this step: by the view's car's slice, the time and the
    // option (0x4dece0).
    int active(int slice, int trackSlices, int step, int sfxLevel);
    // 0x4dd5e0, called each game tick; works on every other one. `covered`
    // is 0x420d60 for the camera.
    void update(int step, int active, const ParticleVec &eye, bool covered, ParticleRandom &random);
    // 0x4de700 (rain) or 0x4de070 (snow), then 0x4ddb40 for the splashes
    // when `splashes` (the option not Off, 0x4dedf0). `intensity`: the
    // Direct3D renderer's effects intensity, which dims the drops' and the
    // splashes' colours below 1 (dimByIntensity); 1 for the 3dfx cards.
    void draw(const WeatherView &view, int active, bool splashes, ParticleRandom &random, std::vector<WeatherPoly> &out,
              float intensity = 1);
    // 0x4dd230, from the pass (0x4dece0) when the view's camera has just
    // changed (0x552ff4 set by the camera code, cleared once the view is
    // drawn, 0x4213b0): unless the camera is under cover, every drop back
    // at random in the box round it with its streak's tail forgotten; the
    // splashes cleared either way.
    void cut(const ParticleVec &eye, bool covered, ParticleRandom &random);
    // 0x4dd510, from the pass before the rain is drawn (not the snow): the
    // streaks' tails (+9) forgotten when the camera has gone into or out
    // of the car (modes 2-4 with 0x79c4a0 set, kept per view at
    // 0x8ca2b4) or has just changed. The hidden marks (+8) stay.
    void tails(bool inCar, bool cut);
    // The rain's splash sounds owed (0x4de700 calls 0x414f90 for each
    // splash it makes: rain.bnk, bank entry 4, sound 0-3 at random,
    // volume 90-119, a random azimuth): taken by the caller.
    int takeSplashSounds() {
        const int n = _splashSounds;
        _splashSounds = 0;
        return n;
    }
    const WeatherSetup &setup() const { return _setup; }
    std::array<Drop, kDrops> &drops() { return _drops; }
    std::array<Splash, kSplashes> &splashes() { return _splashes; }

private:
    void respawnTop(Drop &d, const ParticleVec &eye, ParticleRandom &random) const;
    bool splash(const WeatherView &view, const ParticleVec &at, ParticleRandom &random);
    WeatherSetup _setup;
    std::array<Drop, kDrops> _drops{};
    std::array<Splash, kSplashes> _splashes{};
    int _last = -2;
    int _cycleStart = 0;
    int _splashSounds = 0;
    bool _inCar = false; // 0x8ca2b4
};

// One of the rain's splash sounds (0x414f90 with -1): the sound (random &
// 3), the volume (90 + random % 30) and the azimuth (random % 0xFFFF), in
// the order the game draws them.
struct SplashSound {
    int sound = 0;
    int volume = 0;
    uint32_t azimuth = 0;
};
SplashSound splashSound(ParticleRandom &random);

// Mud and water on the lens (0x4ca500, 0x4cb220, 0x4cb750). The kinds
// with splat fields in their templates (+0x64-+0x7C of 0x560b64 + kind x
// 0x80): 1, 4, 10 and 11. Its pictures are sfx.fsh's SLnn (0x4cb590).
struct SplatTemplate {
    int count = 0;       // +0x64 splats made
    int life = 0;        // +0x68 64 Hz steps (aged on the odd ones)
    uint32_t flags = 0;  // +0x6C the quad's flags: bit 0 added
    float size = 0;      // +0x70 half side, in screen units (x / z)
    float jitter = 0;    // +0x74 the place moved by up to this, each way
    uint32_t colour = 0; // +0x78 ARGB
    float chance = 0;    // +0x7C 0: always; else made when random <= it
};
const SplatTemplate *splatTemplate(int kind);
// One of a view's 200 slots (0x7f0bd0 + view x 0x3200, 0x40 bytes each).
struct Splat {
    int life = 0;                                  // +0x00 0: free
    int kind = 0;                                  // +0x04
    std::array<std::array<float, 2>, 4> corners{}; // +0x08-+0x24, x / z and y / z
    uint32_t colour = 0;                           // +0x28
    uint32_t flags = 0;                            // +0x2C
    // +0x34, +0x38, +0x3C
    float x = 0;
    float y = 0;
    float size = 0;
};
class SplatScreen {
public:
    static constexpr int kSlots = 200;
    // 0x4ca500, for a particle of `kind` drawn in the main view at `x`,
    // `y` along the camera's right and up and `z` (under 3) ahead: true
    // when the particle is taken (freed) — for any kind with a count and
    // a life, unless `inCar` (0x79c4a0) or no slot is free. Each of the
    // count (made when the chance allows): at (x, y) / z moved by
    // random x 2 x jitter - jitter each way; not made where a live splat
    // is within half its size both ways, unless this is rain (10) on mud
    // (11), whose life is then halved; life the template's less random %
    // half of it; half side the size less random x half of it; a square
    // turned by a random angle. Each of kind 4's made also plays a sound
    // (0x415010), counted for takeSounds.
    bool hit(int kind, float x, float y, float z, bool inCar, ParticleRandom &random);
    // The sounds hit has asked for since the last call, then none: each
    // the race's bank entry 2 (0x57113c: Gen3d, or Gen without 3D sound),
    // slot 0xF, volume 0x7F, azimuth 0 (0x3FFF or 0xC000 by the view in
    // split screen), through sound_start (0x4182b0).
    int takeSounds() {
        const int n = _sounds;
        _sounds = 0;
        return n;
    }
    static constexpr int kSoundSlot = 0xf;
    static constexpr int kSoundVolume = 0x7f;
    // 0x4cb220, on the odd steps (0x4cb2c0): each live splat's alpha the
    // template's x its life / the template's life, then its life - 1;
    // freed once its alpha is 0.
    void step();
    std::array<Splat, kSlots> &splats() { return _slots; }
    const std::array<Splat, kSlots> &splats() const { return _slots; }
    int live() const;
    void clear() {
        _slots = {};
        _sounds = 0;
    }

private:
    std::array<Splat, kSlots> _slots{};
    int _sounds = 0;
};

} // namespace reluva::nfs
