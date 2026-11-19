// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

//   reluva-tests nfs.unit.track

#include "tests/engines/nfs/fixtures.h"

using namespace reluva::nfs::testing;

namespace {

// Lights as nfs3.exe's table has them (frd.md, Lights).
// The cars' reflection on the sky's sphere map (fce.md, flags).
void testReflectionUv() {
    auto near = [](std::array<float, 2> a, float u, float v) {
        return std::fabs(a[0] - u) < 1e-4f && std::fabs(a[1] - v) < 1e-4f;
    };
    CHECK(near(reflectionUv(0, 0, 1), 0.5f, 0.5f) && near(reflectionUv(0, 0, -1), 0.5f, 0.5f));
    CHECK(near(reflectionUv(1, 0, 0), 1.0f, 0.5f) && near(reflectionUv(0, -1, 0), 0.5f, 0.0f));
    const float d = std::sqrt(0.5f); // 45 degrees: 0.5 + 0.7071 * 0.7071 / sqrt(2 * 1.7071)
    CHECK(near(reflectionUv(d, 0, d), 0.5f + 0.5f / std::sqrt(2 * (1 + d)), 0.5f));
}

void testPolygonAnimation() {
    Track::Polygon p;
    p.texture = 7;
    CHECK(p.textureAt(1000) == 7); // not animated
    // Animated from entry 7: 10 ticks a frame, 4 frames (84, as on the
    // tracks).
    p.flags = Track::Polygon::kAnimated | Track::Polygon::kTwoSided;
    p.animationBase = 7;
    p.animation = 84;
    CHECK(p.frameDelay() == 10 && p.frameCount() == 4);
    CHECK(p.textureAt(0) == 7 && p.textureAt(9) == 7 && p.textureAt(10) == 8 && p.textureAt(39) == 10 &&
          p.textureAt(40) == 7);
    p.animation = 0xf9; // 31 ticks, 1 frame: still
    CHECK(p.textureAt(0) == 7 && p.textureAt(500) == 7);
}

// Placed sounds as the game plays them (frd.md, Sounds).
void testPlacedSounds() {
    // 2^18 / distance^2, at most 127: full within 45 units.
    CHECK(placedSoundVolume(0) == 127 && placedSoundVolume(40) == 127);
    CHECK(placedSoundVolume(64) == 64 && placedSoundVolume(128) == 16 && placedSoundVolume(256) == 4);
    // A one-shot fires in the eighth of 64 frames picked, then picks again.
    struct Fixed {
        int value;
        int next() const { return value; }
    } random{5};
    OneShotTimer t; // picks 0 first
    CHECK(t.fires(64, random) && t.pick == 5);
    CHECK(!t.fires(65, random) && !t.fires(39, random) && t.fires(40, random) && t.pick == 5);
    CHECK(t.fires(47, random) && !t.fires(48, random));
}

void testLightLook() {
    // Dimmed: k = 0.5 * 65536 >> 8 = 128, each byte * k >> 8; 1 and above unchanged.
    CHECK(dimLight(0xffdf1616, 0.5f) == 0x7f6f0b0b && dimLight(0xffdf1616, 1) == 0xffdf1616);
    CHECK(dimLight(0xffffffff, 0.999999f) == 0xfefefefe); // k at most 0xffff >> 8 = 255
    CHECK(lightLook(0, 0).color == 0xffdeeaeb && lightLook(0, 0).size == 5 && lightLook(0, 12345).shown);
    CHECK(lightLook(16 + 7, 0).color == 0xffff0000); // the type is taken & 15
    // Type 5: off while bit 6 of the tick is set, a second on and off.
    CHECK(lightLook(5, 63).shown && !lightLook(5, 64).shown && lightLook(5, 128).shown);
    // Type 6: bit 5, 27 ticks ahead.
    CHECK(lightLook(6, 4).shown && !lightLook(6, 5).shown && lightLook(6, 37).shown);
    CHECK(lightLook(10, 0).color >> 24 == 0xba && lightLook(10, 0).size == 2.5f);
}

// The weather's rain and snow (col.md, Rain and snow), from 0x4dcdf0 and
// the functions it sets up for.
void testWeather() {
    CHECK(weatherKind(0) == nullptr && weatherKind(3) == nullptr);
    CHECK(weatherKind(1) && weatherKind(4) == weatherKind(1) && weatherKind(2) && weatherKind(5) == weatherKind(2));
    // Hometown's rain: 400 drops in a box 25 either side, 15 up, 5 down;
    // a fall of 1.2 an update and a wind of 0.3 towards -z (180 degrees).
    const WeatherSetup rain = weatherSetup(1, 0, 0, 0, 3840, 4800, 5120, 6080);
    CHECK(rain.kind == 1 && rain.count == 400 && rain.splashes == 20);
    CHECK(rain.half == 25 && rain.top == 15 && rain.bottom == -5);
    CHECK(std::fabs(rain.velocity[1] + 1.2f) < 1e-4f && std::fabs(rain.velocity[2] + 0.3f) < 1e-4f &&
          std::fabs(rain.velocity[0]) < 1e-4f);
    CHECK(rain.colour == 0x7e7e7e7eu && rain.splashLife == 16 && rain.splashGrey == 128);
    CHECK(rain.times[0] == 3840 && rain.times[1] == 8640 && rain.times[2] == 13760 && rain.times[3] == 19840);
    // The Summit's snow: 800 flakes, 20 either side, 20 up, falling 0.2.
    const WeatherSetup snow = weatherSetup(2, 0, 0, 0, 0, 0, 0, 0);
    CHECK(snow.kind == 2 && snow.count == 800 && snow.half == 20 && snow.top == 20 && snow.bottom == -5);
    CHECK(std::fabs(snow.velocity[1] + 0.2f) < 1e-4f && snow.velocity[0] == 0 && snow.colour == 0xc8c8c8c8u);
    CHECK(weatherSetup(0, 0, 0, 0, 1, 1, 1, 1).kind == 0);

    // By the road: everywhere without a stretch; trk002's 340 to 240,
    // past the line, fading over 15 slices.
    CHECK(weatherAtSlice(rain, 123, 600) == 400);
    const WeatherSetup wrap = weatherSetup(1, 340, 240, 15, 0, 0, 0, 0);
    CHECK(weatherAtSlice(wrap, 300, 600) == 0 && weatherAtSlice(wrap, 400, 600) == 400);
    CHECK(weatherAtSlice(wrap, 100, 600) == 400 && weatherAtSlice(wrap, 250, 600) == 0);
    CHECK(weatherAtSlice(wrap, 345, 600) == 400 - 400 * 10 / 15);
    CHECK(weatherAtSlice(wrap, 235, 600) == 400 - 400 * 10 / 15);
    // By the time: on, fading off, off, fading on, round again.
    CHECK(weatherAtTime(rain, 400, 0) == 400 && weatherAtTime(rain, 400, 3839) == 400);
    CHECK(weatherAtTime(rain, 400, 3840 + 2400) == 200 && weatherAtTime(rain, 400, 10000) == 0);
    CHECK(weatherAtTime(rain, 400, 13760 + 3040) == 200);
    bool restart = false;
    CHECK(weatherAtTime(rain, 400, 19840, &restart) == 400 && restart);
    CHECK(weatherAtTime(snow, 800, 100000) == 800); // no times: always
    CHECK(weatherForEffects(400, 0) == 400 && weatherForEffects(400, 1) == 200 && weatherForEffects(400, 2) == 100);
    CHECK(weatherForEffects(7, 2) == 1);

    // A flake's side: 12 pixels at the camera, 1 at 20 and beyond.
    CHECK(std::fabs(snowflakeSide(snow, 0) - 12) < 1e-5f && std::fabs(snowflakeSide(snow, 10) - 6) < 1e-5f);
    CHECK(snowflakeSide(snow, 20) == 1 && snowflakeSide(snow, 30) == 1);
    // The game's table sine: 1024 steps a turn of 16.16.
    CHECK(fixedSin(0) == 0 && fixedSin(0x4000) == 65536 && fixedCos(0) == 65536 && fixedSin(0x8000) == 0);
    CHECK(fixedSin(64) == 402 && fixedSin(0xC000) == -65536);
    CHECK(std::abs(fixedSin(32) - 201) <= 1 && std::abs(fixedCos(0x4000)) <= 1);

    // The 120-degree screen the pass draws through.
    const ViewScreen wide = viewScreen(0, 0, 640, 480, 120);
    CHECK(std::fabs(wide.sx - 184.75f) < 0.01f && std::fabs(wide.sy + 216.10f) < 0.01f);

    // A drop moves every other tick, and one out of the box comes back at
    // its other side.
    Weather w;
    ParticleRandom random;
    w.start(rain, {0, 0, 0}, 100, random);
    for (int i = 0; i < rain.count; ++i) {
        const ParticleVec &p = w.drops()[size_t(i)].position;
        CHECK(std::fabs(p[0]) <= 25 && std::fabs(p[2]) <= 25 && p[1] >= -5 && p[1] <= 15);
    }
    CHECK(w.drops()[400].position == (ParticleVec{0, 0, 0}));
    w.drops()[0].position = {0, 10, 0};
    w.update(101, 400, {0, 0, 0}, false, random); // too soon after the start
    CHECK(w.drops()[0].position[1] == 10);
    w.update(102, 400, {0, 0, 0}, false, random);
    CHECK(std::fabs(w.drops()[0].position[1] - 8.8f) < 1e-4f && std::fabs(w.drops()[0].position[2] + 0.3f) < 1e-4f);
    w.update(103, 400, {0, 0, 0}, false, random); // the tick after: nothing
    CHECK(std::fabs(w.drops()[0].position[1] - 8.8f) < 1e-4f);
    w.drops()[0].position = {0, -4.5f, 0};
    w.update(104, 400, {0, 0, 0}, true, random); // falls below: back at the top, hidden under cover
    CHECK(w.drops()[0].position[1] == 15 && w.drops()[0].hidden);
    w.drops()[1].position = {25.5f, 0, 0};
    w.drops()[1].hidden = false;
    random.script = {0x8000, 0x8000}; // y and z at the middle
    w.update(106, 2, {0, 0, 0}, false, random);
    CHECK(w.drops()[1].position[0] == -25 && w.drops()[1].position[1] == 5 && w.drops()[1].position[2] == 0);

    // Drawing: looking along +z from the origin, a drop 10 ahead is a
    // streak; drawn once it is a point, then it has length.
    WeatherView view;
    view.right = {1, 0, 0};
    view.up = {0, 1, 0};
    view.forward = {0, 0, 1};
    Weather d;
    d.start(rain, {0, 0, 0}, 0, random);
    for (auto &drop : d.drops())
        drop.position = {0, 100, -100}; // out of sight
    d.drops()[0].position = {1, 2, 10};
    std::vector<WeatherPoly> polys;
    d.draw(view, 400, true, random, polys);
    CHECK(polys.size() == 1 && polys[0].corners == 3 && polys[0].depth == 10 && polys[0].colour == 0x7e7e7e7eu);
    CHECK(std::fabs(polys[0].at[1][0] - (320 + view.sx * 0.1f)) < 1e-3f &&
          std::fabs(polys[0].at[1][1] - (240 + view.sy * 0.2f)) < 1e-3f);
    CHECK(polys[0].at[0] == polys[0].at[1] && polys[0].at[2][0] == polys[0].at[1][0] + 2);
    d.drops()[0].position = {1, 0, 10};
    polys.clear();
    d.draw(view, 400, true, random, polys);
    CHECK(polys.size() == 1 && std::fabs(polys[0].at[0][1] - (240 + view.sy * 0.1f)) < 1e-3f); // halfway
    // Behind the camera, a little: back at the top. Farther than the box's
    // half width: not drawn.
    d.drops()[0].position = {0, 0, -1};
    polys.clear();
    d.draw(view, 400, true, random, polys);
    CHECK(polys.empty() && d.drops()[0].position[1] == 15);
    d.drops()[0].position = {0, 0, 26};
    d.draw(view, 400, true, random, polys);
    CHECK(polys.empty());
    // Within 3: a splash on the screen instead, where the drop was.
    d.drops()[0].position = {0.5f, 0, 2};
    random.script = {5, 0xffff, 0x4000}; // life 16 - 5 % 4, size all, angle a quarter
    d.draw(view, 400, true, random, polys);
    CHECK(polys.size() == 1 && polys[0].splash && polys[0].corners == 4);
    const Weather::Splash &sp = d.splashes()[0];
    CHECK(sp.life == 15 && std::fabs(sp.x - 0.25f) < 1e-6f && sp.y == 0 &&
          std::fabs(sp.size - 0.2f * 0xffff / 65536) < 1e-5f);
    CHECK(polys[0].colour == 0x78787878u); // 128 x 15 / 16
    CHECK(d.drops()[0].position[1] == 15); // the drop back at the top
    // The rain's splash plays a sound (0x414f90), once.
    CHECK(d.takeSplashSounds() == 1 && d.takeSplashSounds() == 0);
    // rain.bnk's sound random & 3, volume 90 + random % 30, azimuth random % 0xFFFF.
    random.script = {5, 31, 70000};
    const SplashSound ss = splashSound(random);
    CHECK(ss.sound == 1 && ss.volume == 91 && ss.azimuth == 4465);
    // Under Direct3D at half intensity (0x4de700, 0x4ddb40): the colours
    // dimmed, byte x 128 >> 8.
    polys.clear();
    d.drops()[0].position = {1, 2, 10};
    d.draw(view, 400, true, random, polys, 0.5f);
    CHECK(polys.size() == 2 && polys[0].colour == 0x3f3f3f3fu);
    // 0x4dd510: the tails forgotten when the camera goes into or out of
    // the car or is cut, not otherwise; the hidden marks kept.
    d.drops()[1].tailSet = d.drops()[1].hidden = true;
    d.tails(false, false);
    CHECK(d.drops()[1].tailSet);
    d.tails(true, false);
    CHECK(!d.drops()[1].tailSet && d.drops()[1].hidden);
    d.drops()[1].tailSet = true;
    d.tails(true, false);
    CHECK(d.drops()[1].tailSet);
    d.tails(true, true);
    CHECK(!d.drops()[1].tailSet);
    // 0x4dd230: a cut puts every drop back round the camera, unless it is
    // under cover; the splashes go either way.
    d.drops()[1].position = {500, 500, 500};
    d.cut({100, 0, 100}, true, random);
    CHECK(d.drops()[1].position[0] == 500 && d.splashes()[0].life == 0);
    d.cut({100, 0, 100}, false, random);
    CHECK(std::fabs(d.drops()[1].position[0] - 100) <= 25 && std::fabs(d.drops()[1].position[2] - 100) <= 25 &&
          d.drops()[1].position[1] >= -5 && d.drops()[1].position[1] <= 15);
    // Snow: a square from the point, its side by the depth.
    Weather s;
    s.start(snow, {0, 0, 0}, 0, random);
    for (auto &flake : s.drops())
        flake.position = {0, 100, -100};
    s.drops()[0].position = {0, 0, 10};
    polys.clear();
    s.draw(view, 800, false, random, polys);
    CHECK(polys.size() == 1 && polys[0].corners == 4 && polys[0].colour == 0xc8c8c8c8u);
    CHECK(polys[0].at[0][0] == 320 && polys[0].at[1][0] == 326 && polys[0].at[2][1] == 246);
}

// The cars' particles beyond the tyre smoke: the wheels' spray (0x42a280,
// 0x4cb010, the surface table 0x553080), car hits (0x42ac40 type 5,
// car_smoke_tick 0x42b480, 0x4caea0) and their sounds, and the Direct3D
// dimming.
void testCarEffects() {
    // The surface table: in rain or snow the road throws kind 10, the
    // verges 11; dry, grass and dirt their own, the tarmac nothing.
    CHECK(surfaceEffect(1, 1).kind == 10 && surfaceEffect(1, 2).kind == 10 && surfaceEffect(2, 1).kind == 11 &&
          surfaceEffect(13, 2).kind == 11 && surfaceEffect(9, 1).kind == 1);
    CHECK(surfaceEffect(1, 0).kind == 0 && surfaceEffect(5, 0).kind == 2 && surfaceEffect(6, 0).kind == 10 &&
          surfaceEffect(0x14, 0).kind == 12 && surfaceEffect(4, 0).level == 10);
    CHECK(surfaceEffect(1, 0).smoke && !surfaceEffect(1, 1).smoke && surfaceEffect(1, 1).speed == 5 &&
          surfaceEffect(1, 1).range == 20);

    ParticleSystem sys;
    ParticleRandom random;
    // A right rear wheel on the wet road at 10: not skidding, so thrown
    // for the speed: kind 10 at scale 10 / 50, and the skid level becomes
    // (10 - 5) / 20 x 5.
    random.script.assign(6, 0x8000);
    float level = wheelSpray(sys, 2, 0, 1, 1, {0, 0, 0}, {10, 0, 0}, {0, 0, 1}, 10, 0, random);
    CHECK(level == 1.25f && sys.live() == 1);
    {
        const Particle &q = sys.particles()[0];
        CHECK(q.kind == 10 && q.life == 32 && q.colour == 0x7fffffff);
        CHECK(std::fabs(q.velocity[0] - (0.4f + 0.9f * 5)) < 1e-5f && std::fabs(q.velocity[1] - 0.62f) < 1e-5f &&
              std::fabs(q.velocity[2] - 0.7f) < 1e-5f);
    }
    // At 5 or under: nothing; skidding: thrown whatever the speed, the
    // level kept; the dry tarmac (kind 0): nothing.
    sys.clear();
    CHECK(wheelSpray(sys, 2, 0, 1, 1, {}, {5, 0, 0}, {0, 0, 1}, 5, 0, random) == 0 && sys.live() == 0);
    random.script.assign(6, 0x8000);
    CHECK(wheelSpray(sys, 0, 3, 2, 2, {}, {1, 0, 0}, {0, 0, 1}, 1, 0, random) == 3 && sys.live() == 1 &&
          sys.particles()[0].kind == 11);
    sys.clear();
    CHECK(wheelSpray(sys, 2, 9, 1, 0, {}, {30, 0, 0}, {0, 0, 1}, 30, 0, random) == 9 && sys.live() == 0);
    // Kind 10 isn't made with a software renderer.
    wheelParticles(sys, 10, 2, {}, {10, 0, 0}, {0, 0, 1}, 10, false, 0, random, 0, true);
    CHECK(sys.live() == 0);

    // A car hit over 5: a burst at twice the strength and a dust puff.
    sys.clear();
    carHitParticles(sys, {0, 1, 0}, {1, 1, 0}, {0, 0, 10}, 5, 0, random);
    CHECK(sys.live() == 0);
    carHitParticles(sys, {0, 1, 0}, {1, 1, 0}, {0, 0, 10}, 10, 0, random);
    int sparks = 0;
    int dust = 0;
    for (const Particle &q : sys.particles())
        if (q.life != 0)
            (q.kind == 3 ? sparks : dust) += q.kind == 3 || q.kind == 2 ? 1 : 0;
    CHECK(sparks == 30 && dust == 1);
    // Its smoke: over 30, or over 5 for traffic, at |round(strength x 0.15)|,
    // the point in the car's frame x 0.6; not while one runs.
    CarSmoke smoke;
    const ParticleVec right{1, 0, 0};
    const ParticleVec up{0, 1, 0};
    const ParticleVec forward{0, 0, 1};
    CHECK(!startCarSmoke(smoke, 20, false, {1, 2, 3}, {}, right, up, forward));
    CHECK(startCarSmoke(smoke, 20, true, {1, 2, 3}, {}, right, up, forward) && smoke.timer == 3);
    CHECK(std::fabs(smoke.point[0] - 0.6f) < 1e-6f && std::fabs(smoke.point[2] - 1.8f) < 1e-6f);
    CHECK(!startCarSmoke(smoke, 40, false, {1, 2, 3}, {}, right, up, forward));
    // Counting up: white smoke (6) to 16, black (9) to 64, then the fire.
    sys.clear();
    carSmokeTick(sys, smoke, {}, right, up, forward, {}, true, 1, 0, 0, 0, random);
    CHECK(smoke.timer == 4 && sys.live() == 1 && sys.particles()[0].kind == 6 &&
          std::fabs(sys.particles()[0].position[1] - 1.2f) < 1e-6f);
    smoke.timer = 16;
    carSmokeTick(sys, smoke, {}, right, up, forward, {}, true, 1, 0, 0, 6, random);
    CHECK(smoke.timer == 17 && sys.particles()[1].kind == 9);
    // It stops once the car is down, upright and hardly spinning.
    carSmokeTick(sys, smoke, {}, right, up, forward, {}, false, 1, 0.3f, 0, 8, random);
    CHECK(smoke.timer == 18);
    carSmokeTick(sys, smoke, {}, right, up, forward, {}, false, 1, 0.1f, -0.1f, 12, random);
    CHECK(smoke.timer == 0);
    // The fire: kinds 9 and 7 rising, and seven times in eight a kind 8.
    sys.clear();
    carFire(sys, {}, {}, {0, 0, 4}, 0, random);
    CHECK(sys.particles()[0].kind == 9 && sys.particles()[1].kind == 7 && sys.live() >= 2);

    // The hit's sound: Gen.bnk's slot by the loudness, never the same twice
    // but for 33.
    int last = 0;
    CHECK(carHitSoundSlot(100, last, random) == 33 && carHitSoundSlot(100, last, random) == 33);
    CHECK(carHitSoundSlot(80, last, random) == 32);
    random.script = {1};
    CHECK(carHitSoundSlot(80, last, random) == 37 && last == 37);
    CHECK(carHitSoundSlot(10, last, random) == 30 && carHitSoundSlot(10, last, random) == 34);
    CHECK(carHitSoundSlot(30, last, random) != 34);
    last = 0;
    CHECK(carHitSoundSlot(60, last, random) == 37);
    CHECK(hitSoundVolume(0) == 127 && hitSoundVolume(50) == 14 && hitSoundVolume(53) == 0 && hitSoundVolume(54) == 0);
    CHECK(soundPan(0) == 64 && soundPan(0x4000) == 127 && soundPan(0x8000) == 63 && soundPan(0xc000) == 0 &&
          soundPan(0xffff) == 63);
    CHECK(soundDistance({10, 1, 4}) == 11 && soundDistance({3, 8, 0}) == 8 && soundDistance({-4, 0, -10}) == 11);

    // Kind 12: a random part of 0x7FFF off its colour at birth (alpha
    // first: 0, 0, then green 64 and blue 128 at 0.5), and its spin turned
    // back past a quarter turn.
    sys.clear();
    random.script.assign(10, 0x8000);
    Particle *p = sys.spawn(12, {}, 1, {1, 0, 0}, {0, 0, 1}, {}, 0, random);
    CHECK(p && p->colour == 0xffffbf7f);
    p->angle = 0.3f;
    p->spin = 0.1f;
    sys.step(1, random);
    CHECK(p->spin < 0 && std::fabs(p->angle - 0.2f) < 1e-6f);

    // The Direct3D dimming: the lights' way (k = round(f x 65536) >> 8)
    // and the particles' (byte x round(f x 65536) >> 16).
    CHECK(dimByIntensity(0xffffffff, 0.5f) == 0x7f7f7f7f && dimByIntensity(0x12345678, 1) == 0x12345678);
    CHECK(dimByIntensity(0x80808080, 0.25f) == 0x20202020);
    CHECK(dimParticle(0xffffffff, 0.5f) == 0x7f7f7f7f && dimParticle(0x80402010, 0.25f) == 0x20100804 &&
          dimParticle(0xabcdef01, 1.5f) == 0xabcdef01);

    // Splats on the lens (0x4ca500): only kinds 1, 4, 10 and 11, not in
    // the car. Mud (11, two of them, jitter 0.5): the first at (x, y) / z
    // (the jitter's randoms 0.5 move it by 0), life 1024 less 10, half
    // side 0.2 turned by 0; the second at the same place is covered.
    SplatScreen lens;
    CHECK(!lens.hit(3, 0.2f, 0.1f, 2, false, random) && !lens.hit(11, 0.2f, 0.1f, 2, true, random));
    random.script = {0x8000, 0x8000, 10, 0, 0, 0x8000, 0x8000};
    CHECK(lens.hit(11, 0.2f, 0.1f, 2, false, random) && lens.live() == 1);
    const Splat &mud = lens.splats()[0];
    CHECK(mud.kind == 11 && mud.life == 1014 && std::fabs(mud.x - 0.1f) < 1e-6f && std::fabs(mud.y - 0.05f) < 1e-6f &&
          std::fabs(mud.size - 0.2f) < 1e-6f && std::fabs(mud.corners[0][1] - 0.25f) < 1e-6f &&
          std::fabs(mud.corners[1][0] - 0.3f) < 1e-6f && mud.colour == 0x7fffffff && mud.flags == 0);
    // Rain (10, five) on it: each halves the mud's life; the first is made
    // (half side 0.2 less 0.2 x 0.5 x 0.5), the other four covered by it.
    random.script = {0x8000, 0x8000, 0, 0x8000, 0};
    for (int i = 0; i < 4; ++i)
        random.script.insert(random.script.end(), {0x8000, 0x8000});
    CHECK(lens.hit(10, 0.2f, 0.1f, 2, false, random) && lens.live() == 2);
    CHECK(mud.life == 1014 >> 5 && lens.splats()[1].life == 128 && std::fabs(lens.splats()[1].size - 0.15f) < 1e-6f &&
          lens.splats()[1].flags == 1);
    // Aging (0x4cb220): alpha the template's x life / its life, then - 1.
    lens.step();
    CHECK(mud.colour == 0x03ffffff && mud.life == 30 && lens.splats()[1].colour == 0xffececff &&
          lens.splats()[1].life == 127);
    for (int i = 0; i < 40; ++i)
        lens.step();
    CHECK(mud.life == 0 && lens.live() == 1);
    // Kind 4: made one time in a thousand, but the particle is taken anyway.
    random.script = {0xffff};
    CHECK(lens.hit(4, 0, 0, 1, false, random) && lens.live() == 1);
    CHECK(lens.takeSounds() == 0);
    // When it is made, its sound (0x415010: Gen's slot 0xF) once.
    random.script = {0, 0, 0, 0};
    CHECK(lens.hit(4, 0.5f, 0.5f, 1, false, random) && lens.live() == 2);
    CHECK(lens.takeSounds() == 1 && lens.takeSounds() == 0 && SplatScreen::kSoundSlot == 15);
    // No free slot: not taken.
    for (Splat &s : lens.splats())
        s.life = 5;
    CHECK(!lens.hit(1, 0, 0, 1, false, random));
}

void testParticles() {
    // The side codes' kinds (0x553740) and the burst's count (0x4cac20).
    CHECK(wallParticleKind(1) == 3 && wallParticleKind(2) == 3 && wallParticleKind(4) == 3);
    CHECK(wallParticleKind(3) == 2 && wallParticleKind(5) == 2 && wallParticleKind(7) == 2);
    CHECK(wallParticleKind(0) == 0 && wallParticleKind(6) == 0 && wallParticleKind(9) == 0);
    CHECK(burstCount(20) == 30 && burstCount(30) == 30 && burstCount(11) == 16 && burstCount(15) == 22);
    // Sparks: white, to orange and clear (bytes rounded as fistp, halves to even).
    const ParticleTemplate &sparks = *particleTemplate(3);
    CHECK(particleColour(sparks, 0) == 0xffffffff && particleColour(sparks, 1) == 0x00ff3900);
    CHECK(particleColour(sparks, 0.5f) == 0x7fff9c7f);
    CHECK(particleColour(*particleTemplate(2), 1) == 0x00dfdfdf);
    CHECK(!particleTemplate(0) && !particleTemplate(14) && particleTemplate(1)->life == 64 &&
          particleTemplate(6)->life == 256);
    // Kind 7 as 0x4cb670 sets it for Direct3D.
    CHECK(particleTemplate(7)->colour == 0xffffffff && particleTemplate(7, true)->colour == 0x7f7f7f7f &&
          particleTemplate(7, true)->fade == 0x7f003f7f && particleTemplate(9, true)->fade == 0xff000000);

    // One dust puff (0x4cae00): random 0.5 throughout, so the template's own
    // speeds; it rises, spins (the sign from the first number's bit 0).
    ParticleSystem sys;
    ParticleRandom random;
    random.script = {1, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    sys.spawnOne(2, {10, 20, 30}, 0, random);
    CHECK(sys.live() == 1);
    const Particle *p = &sys.particles()[0];
    CHECK(p->kind == 2 && p->life == 128 && p->lifeMax == 128 && p->colour == 0x7fdfdfdf);
    CHECK(p->velocity[0] == 0 && std::fabs(p->velocity[1] - 3.1f) < 1e-6f && p->velocity[2] == 0);
    CHECK(p->size == 0.9375f && p->spin == 0.5f && p->growth == 3.0f && p->angle == 0.5f);
    // The throttle: dust every other pair of ticks, smoke one in four; none
    // with Horizon off, half at Low.
    sys.clear();
    CHECK(!sys.spawn(2, {}, 1, {}, {}, {}, 2, random) && sys.spawn(2, {}, 1, {}, {}, {}, 4, random));
    CHECK(sys.spawn(6, {}, 1, {}, {}, {}, 1, random) && !sys.spawn(6, {}, 1, {}, {}, {}, 6, random) &&
          sys.spawn(6, {}, 1, {}, {}, {}, 8, random));
    CHECK(!sys.spawn(3, {}, 1, {}, {}, {}, 0, random, 2) && !sys.spawn(3, {}, 1, {}, {}, {}, 0, random, 1) &&
          sys.spawn(3, {}, 1, {}, {}, {}, 2, random, 1));
    // Nothing moving: not made.
    sys.clear();
    CHECK(sys.spawn(6, {}, 0, {}, {}, {}, 0, random) && sys.live() == 0);

    // Tyre smoke (0x42a480 -> 0x4cb010, kind 6). The skid levels: front x
    // 7.5 and rear as they are, held to 0..10; none in the air or tipped.
    CHECK(wheelSkidLevel(0, 0.5f, 0, false, 1) == 3.75f && wheelSkidLevel(1, 2, 0, false, 1) == 10 &&
          wheelSkidLevel(2, 0, 12, false, 1) == 10 && wheelSkidLevel(3, 0, -1, false, 1) == 0);
    CHECK(wheelSkidLevel(2, 0, 8, true, 1) == 0 && wheelSkidLevel(2, 0, 8, false, 0.85f) == 0);
    // Only rear wheels over 5, on the dry surfaces marked for skid marks
    // (0x553090: 1, 4, 7, 10, 11, 12), none in rain or snow.
    CHECK(wheelSmokes(2, 10, 1, 0) && wheelSmokes(3, 5.5f, 12, 0) && !wheelSmokes(3, 5, 1, 0));
    CHECK(!wheelSmokes(0, 10, 1, 0) && !wheelSmokes(2, 10, 2, 0) && !wheelSmokes(2, 10, 1, 1) &&
          !wheelSmokes(2, 10, 14, 0));
    CHECK(skidMarkSurface(4, 0) && skidMarkSurface(0x41, 0) && !skidMarkSurface(5, 0) && !skidMarkSurface(1, 2));
    // A left rear wheel's puff: along the velocity made a unit vector, out
    // to the left (against the right row), half the velocity inherited
    // (x 0.5), spinning the other way.
    sys.clear();
    random.script = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    tyreSmoke(sys, 3, {1303, 0, 950}, {4, 0, 0}, {0, 0, 1}, 0, random);
    CHECK(sys.live() == 1);
    {
        const Particle &q = sys.particles()[0];
        CHECK(q.kind == 6 && q.life == 256 && q.position[0] == 1303 && q.colour == 0x7fffffff);
        CHECK(std::fabs(q.velocity[0] - 1.5f) < 1e-6f && std::fabs(q.velocity[1] - 1.5f) < 1e-6f &&
              std::fabs(q.velocity[2] + 0.5f) < 1e-6f && std::fabs(q.spin + 0.1f) < 1e-6f);
    }
    // A right one goes out to the right; a held car (no velocity) still
    // gets the up and across speeds.
    sys.clear();
    random.script = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    tyreSmoke(sys, 2, {0, 0, 0}, {0, 0, 0}, {0, 0, 1}, 8, random);
    CHECK(sys.live() == 1 && sys.particles()[0].velocity[2] == 0.5f && sys.particles()[0].velocity[0] == 0 &&
          std::fabs(sys.particles()[0].spin - 0.1f) < 1e-6f);
    // Its throttle (one in four pairs of ticks): nothing at tick 2.
    sys.clear();
    tyreSmoke(sys, 2, {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 2, random);
    CHECK(sys.live() == 0);

    // A spark: along × a + across × b + up, then falling and moving 1/64 of
    // its velocity a game tick, the rest every other tick.
    sys.clear();
    random.script = {0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000};
    Particle *s = sys.spawn(3, {0, 0, 0}, 1, {0, 0, 1}, {1, 0, 0}, {0, 0, 0}, 0, random);
    CHECK(s && s->velocity[0] == 2 && s->velocity[1] == 2 && s->velocity[2] == 5);
    sys.step(0, random); // even: moves only
    CHECK(s->life == 16 && s->position[2] == 5 / 64.0f && s->velocity[1] == 2);
    sys.step(1, random);
    CHECK(s->life == 15 && s->velocity[1] == 1.75f && s->colour == 0xffffffff);
    sys.step(2, random);
    sys.step(3, random); // odd with bit 1: the colour, at 1/16 of its life
    CHECK(s->life == 14 && s->colour == particleColour(sparks, 1 / 16.0f));
    // Its end: two more sparks of half the life from where it was.
    s->children = 2;
    random.script.clear();
    for (uint32_t tick = 4; sys.particles()[0].children != 0 && tick < 64; ++tick)
        sys.step(tick, random);
    // The second, in a later slot, is stepped in the same pass (as the game).
    CHECK(sys.particles()[0].life == 8 && sys.particles()[1].life == 7 && sys.live() == 2);
    CHECK(sys.particles()[0].children == 0 && sys.particles()[0].lifeMax == 16);

    // When all 600 are taken, the one with the least life goes.
    sys.clear();
    for (int i = 0; i < ParticleSystem::kSlots; ++i)
        sys.particles()[size_t(i)].life = 100 + (i == 321 ? -50 : i);
    CHECK(&sys.slot() == &sys.particles()[321]);

    // Wall hits (0x42ac40): fast on a sparks code, one spark and a burst of
    // 30; fast on dust, two puffs; slow, the code's particle and smoke.
    auto hitCount = [&](int code, float speed, float rough = 0) {
        ParticleSystem hits;
        WallHit hit;
        hit.code = code;
        hit.speed = speed;
        hit.impact = 10;
        hit.rough = rough;
        hit.contact = {1, 0, 0};
        hit.carVelocity = {0, 0, speed};
        wallHitParticles(hits, hit, 0, random);
        return hits.live();
    };
    CHECK(hitCount(4, 40) == 31 && hitCount(1, 22) == 1 + 16 && hitCount(5, 40) == 2);
    CHECK(hitCount(5, 18) == 2 && hitCount(0, 18) == 1 && hitCount(0, 40) == 0 && hitCount(4, 10) == 1);
    CHECK(hitCount(4, 40, 3) == 2);
    // The burst's sparks live 9-16 ticks and may each leave 0-2 more.
    {
        ParticleSystem hits;
        hits.burst({0, 0, 0}, {-1, 0, 0}, {0, 0, 40}, 20, 0, random);
        bool ok = hits.live() == 30;
        for (const Particle &q : hits.particles())
            if (q.life)
                ok = ok && q.life >= 9 && q.life <= 16 && q.children >= 0 && q.children <= 2 && q.velocity[2] > 10;
        CHECK(ok);
    }

    // Drawing: a square turned by the angle, its corners `half` away; a
    // streak from where the spark was drawn, the tail then halfway.
    Particle d;
    d.position = {5, 5, 5};
    const ParticleVec right{1, 0, 0};
    const ParticleVec up{0, 1, 0};
    auto corners = pictureCorners(d, right, up, 2);
    CHECK(corners[0] == (ParticleVec{5, 3, 5}) && corners[1] == (ParticleVec{7, 5, 5}) &&
          corners[2] == (ParticleVec{5, 7, 5}) && corners[3] == (ParticleVec{3, 5, 5}));
    // A quarter turn: the picture's first corner to the right (the sine's
    // part against the screen's y, as the game's list has it), the second
    // above; with the game's aspect the sine's part is longer.
    d.angle = 0.25f;
    corners = pictureCorners(d, right, up, 2);
    auto close3 = [](const ParticleVec &a, const ParticleVec &b) {
        return std::fabs(a[0] - b[0]) < 1e-5f && std::fabs(a[1] - b[1]) < 1e-5f && std::fabs(a[2] - b[2]) < 1e-5f;
    };
    CHECK(close3(corners[0], ParticleVec{7, 5, 5}) && close3(corners[1], ParticleVec{5, 7, 5}) &&
          close3(corners[2], ParticleVec{3, 5, 5}) && close3(corners[3], ParticleVec{5, 3, 5}));
    corners = pictureCorners(d, right, up, 2, 330.332f / 320);
    CHECK(close3(corners[0], ParticleVec{5 + 2 * 330.332f / 320, 5, 5}) && close3(corners[1], ParticleVec{5, 7, 5}));
    d.angle = 0;
    corners = streakCorners(d, right, up, 0.5f, 0.1f); // held to half a pixel
    CHECK(d.tailSet && d.tail == d.position && corners[0] == d.position &&
          corners[2] == (ParticleVec{5.25f, 4.75f, 5}));
    d.position = {9, 5, 5};
    corners = streakCorners(d, right, up, 0.5f, 3); // held to a pixel
    CHECK(corners[0] == (ParticleVec{5, 5, 5}) && corners[1] == corners[0] &&
          corners[2] == (ParticleVec{9.5f, 4.5f, 5}) && corners[3] == (ParticleVec{8.5f, 5.5f, 5}) &&
          d.tail == (ParticleVec{7, 5, 5}));
}

void testViewAndRoadFrame() {
    // The screen (0x4dbce0, 0x4bf260): 90 degrees across, 36 up for each
    // half, at 640 x 480: 320 and -330.33; 75 degrees: 37 and 30.
    const ViewScreen s = viewScreen(0, 0, 640, 480);
    CHECK(std::fabs(s.sx - 320) < 1e-3f && std::fabs(s.sy + 330.332f) < 1e-2f && s.cx == 320 && s.cy == 240);
    CHECK(s.right == 640 && s.bottom == 480);
    const ViewScreen narrow = viewScreen(0, 0, 640, 480, 75, true);
    CHECK(std::fabs(narrow.sx + 320 / std::tan(37 * 0.0174532925f)) < 1e-2f);
    CHECK(std::fabs(narrow.sy + 240 / std::tan(30 * 0.0174532925f)) < 1e-2f);

    // The road's frame on a polygon: the virtual road vector its record's
    // byte 0 picks (up, forward as s16 / 32768), right up x forward, the
    // point its vertex 1.
    Track::Block b;
    b.vertices = {{0, 5, 0}, {10, 5, 0}, {10, 5, 10}, {0, 5, 10}};
    Track::Polygon p;
    p.vertices = {0, 1, 2, 3};
    b.polygons[4] = {p};
    b.polygonRoad = {1, 0, 0, 0, 0, 0, 0, 0};
    auto put = [&](int16_t v) {
        b.virtualRoad.push_back(static_cast<uint8_t>(v));
        b.virtualRoad.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) >> 8));
    };
    for (int16_t v : {0, 0, 0, 0, 0, 0}) // entry 0, unused
        put(v);
    // Entry 1: tilted up across (a camber of 0.1 / 1), forward along z.
    const float tilt = 0.1f / std::sqrt(1.01f);
    for (int16_t v : {static_cast<int16_t>(-tilt * 32768), static_cast<int16_t>(32768 / std::sqrt(1.01f) - 1),
                      int16_t(0), int16_t(0), int16_t(0), int16_t(32767)})
        put(v);
    RoadFrame f;
    CHECK(polygonRoadFrame(b, 0, f));
    CHECK(f.point == (std::array<float, 3>{10, 5, 0}));
    CHECK(std::fabs(f.right[0] - 0.995f) < 1e-3f && std::fabs(f.right[1] - 0.0995f) < 1e-3f);
    // On the plane: 5 m to the right of the point it's 0.5 up; a point
    // above it is taken along the up (so a little to the left), not down.
    CHECK(std::fabs(roadFrameHeight(f, {15, 5.5f, 0}) - 5.5f) < 1e-3f);
    const float above = roadFrameHeight(f, {15, 7.5f, 0});
    CHECK(above > 5.5f && above < 5.7f);
    CHECK(std::fabs(roadFrameHeight(f, {10, 5, 30}) - 5) < 1e-3f); // along the forward
    // The right is up x forward in 16.16 (0x4203eb-0x42045b), each product
    // rounded half up: words x 2 are (-6520, 65208, 0) and (0, 0, 65534),
    // so right x = 65208 x 65534 / 65536 = 65206.01 -> 65206, right y =
    // 0 - (-6520 x 65534 / 65536 = -6519.8 -> -6520), each / 65536.
    CHECK(f.right[0] == 65206 / 65536.0f && f.right[1] == 6520 / 65536.0f && f.right[2] == 0);
    CHECK(f.up[1] == 65208 / 65536.0f && f.forward[2] == 65534 / 65536.0f);
    b.virtualRoad.resize(12);
    CHECK(!polygonRoadFrame(b, 0, f));
}

void testTrack() {
    const Bytes file = makeTrack();
    Track t;
    CHECK(!parseTrack(file, t));
    CHECK(t.blocks.size() == 1 && t.textures.size() == 2 && t.extraObjects.size() == 5);
    if (t.blocks.size() == 1) {
        const Track::Block &b = t.blocks[0];
        CHECK(b.centre[2] == 20.f && b.corners[3][2] == 11.f);
        CHECK(b.vertices.size() == 5 && b.vertices[3][2] == -3.f && b.colours[1] == 0xff102031u);
        CHECK(b.objectVertices == 1 && b.highResVertices == 4);
        CHECK(b.neighbours == (std::vector<int16_t>{0}));
        CHECK(b.startPosition == 7 && b.positions.size() == 8 && b.sounds.size() == 16 && b.lights.size() == 16);
        CHECK(b.polygons[4].size() == 2 && b.polygons[6].size() == 1 && b.polygons[0].empty());
        if (b.polygons[4].size() == 2) {
            CHECK(b.polygons[4][1].vertices == (std::array<uint16_t, 4>{1, 3, 4, 4}));
            CHECK(b.polygons[4][1].texture == 1 && b.polygons[4][1].flags == 0x10 &&
                  b.polygons[4][1].animation == 0xf9);
        }
        CHECK(b.objectLayers[0].size() == 2 && b.objectLayers[1].empty());
        if (b.objectLayers[0].size() == 2)
            CHECK(b.objectLayers[0][0].polygons.size() == 1 && b.objectLayers[0][1].type == 4 &&
                  b.objectLayers[0][1].polygons.empty());
    }
    if (t.extraObjects.size() == 5 && t.extraObjects[0].size() == 2) {
        const auto &still = t.extraObjects[0][0];
        CHECK(still.type == 4 && still.reference[2] == 300.f && still.vertices.size() == 3 &&
              still.polygons.size() == 1);
        const auto &moving = t.extraObjects[0][1];
        CHECK(moving.type == 3 && moving.keys.size() == 2 && moving.keys[1].position[0] == 65536 &&
              moving.keys[1].rotation[2] == 100 && moving.vertices.size() == 1);
    }
    if (t.textures.size() == 2) {
        CHECK(t.textures[0].width == 64 && t.textures[0].height == 32 && t.textures[0].corners[7] == 0.875f);
        CHECK(!t.textures[0].lane && t.textures[1].lane && t.textures[1].picture == 41);
    }

    // Damage is caught: a byte too many, a polygon past the vertices, a
    // texture past the table, counts that disagree, an unknown object type.
    Bytes extra = file;
    extra.push_back(0);
    CHECK(parseTrack(extra, t));
    CHECK(parseTrack(Bytes(file.begin(), file.end() - 1), t));
    const size_t polys = 28 + 4 + 84 + 16 * 5 + 1200 + 32 + 8 + 8 + 12 + 16 + 16 + 4 * 4; // lists 0-3: one count each
    Bytes outside = file;
    outside[polys + 8] = 9; // the first road polygon's first vertex
    CHECK(parseTrack(outside, t));
    Bytes badTexture = file;
    badTexture[polys + 8 + 8] = 2;
    CHECK(parseTrack(badTexture, t));
    Bytes disagree = file;
    disagree[polys + 4] = 3;
    CHECK(parseTrack(disagree, t));

    // The order the game draws a polygon's vertices in (0x41b300, 0x41b640):
    // the file's for the block's lists, 1, 0, 3, 2 for placed objects.
    for (int k = 0; k < 4; ++k)
        CHECK(polygonSlot(k, false) == k);
    CHECK(polygonSlot(0, true) == 1 && polygonSlot(1, true) == 0 && polygonSlot(2, true) == 3 &&
          polygonSlot(3, true) == 2);
    // Hometown's first 50 MPH sign (a placed object, one-sided, texture
    // corners (0, 0), (1, 0), (1, 1), (0, 1)), on the left of the road
    // where the race heads to -x, -z: drawn in the game's order its front,
    // (b - a) x (c - a) as for the Taco Den sign's block polygon that reads
    // the right way round from the start, faces the oncoming traffic
    // (-x, -z), and corner 0 (u = 0) is vertex 1.
    const Track::Vec3 sign[4] = {
        {884.5f, 25.6f, -123.3f}, {883.1f, 25.6f, -122.2f}, {883.1f, 23.2f, -122.2f}, {884.5f, 23.2f, -123.3f}};
    const Track::Vec3 &a = sign[polygonSlot(0, true)];
    const Track::Vec3 &b = sign[polygonSlot(1, true)];
    const Track::Vec3 &c = sign[polygonSlot(2, true)];
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float nx = e1[1] * e2[2] - e1[2] * e2[1];
    const float nz = e1[0] * e2[1] - e1[1] * e2[0];
    CHECK(nx < 0 && nz < 0 && &a == &sign[1]);

    // The blocks drawn (0x41cdc0, 0x41d620): within 500 by day, high detail
    // within 110 and medium within 150; by night all × 0.75; the lower view
    // levels × 0.55 and 0.4 (night 0.41, 0.3); view level 0, all high.
    CHECK(blockDetailAt(0, false) == 2 && blockDetailAt(110, false) == 2 && blockDetailAt(111, false) == 1);
    CHECK(blockDetailAt(150, false) == 1 && blockDetailAt(151, false) == 0 && blockDetailAt(500, false) == 0);
    CHECK(blockDetailAt(501, false) == -1 && blockDetailAt(376, true) == -1 && blockDetailAt(374, true) == 0);
    CHECK(blockDetailAt(83, true) == 1 && blockDetailAt(82, true) == 2);
    CHECK(blockDetailAt(276, false, 2) == -1 && blockDetailAt(274, false, 2) == 0 &&
          blockDetailAt(201, false, 3) == -1);
    CHECK(blockDetailAt(499, false, 0) == 2 && blockDetailAt(501, false, 0) == -1);
}

void testHeadlights() {
    // The cut-off of far polygons (0x41b3bf): 500 ahead, day or night.
    CHECK(polygonInReach(500) && !polygonInReach(500.5f) && polygonInReach(-20));
    CHECK(scaleArgb(0xFF804020u, 128) == 0x7F402010u && scaleArgb(0xB0B0B0B0u, 255) == 0xAFAFAFAFu);

    // Low beams, projected (0x42d253): 10 to 40 in 0xB0B0B0B0, tilted down
    // 1/16 of a turn; heading 0 looks along +z, its right +x.
    const float kTilt = 0.39269908f; // 22.5 degrees
    const Headlight low = carHeadlight(1, true, {0, 1, 0}, 0);
    CHECK(low.fadeFrom == 10 && low.reach == 40 && low.colour == 0xB0B0B0B0u && low.nearPlane == 1);
    CHECK(std::fabs(low.forward[1] + std::sin(kTilt)) < 1e-5f && std::fabs(low.forward[2] - std::cos(kTilt)) < 1e-5f);
    CHECK(std::fabs(low.up[1] - std::cos(kTilt)) < 1e-5f && std::fabs(low.up[2] - std::sin(kTilt)) < 1e-5f);
    CHECK(low.right[0] == 1 && low.right[2] == 0);
    auto along = [&](const Headlight &h, float z, float x = 0) {
        return std::array<float, 3>{h.at[0] + h.forward[0] * z + h.right[0] * x, h.at[1] + h.forward[1] * z,
                                    h.at[2] + h.forward[2] * z + h.right[2] * x};
    };
    // On the axis: the picture's middle; full colour before the fade, two
    // thirds at 20 (0x41a3e0), nothing past the reach or behind.
    HeadlightPoint p = headlightAt(low, along(low, 5));
    CHECK(std::fabs(p.u - 0.5f) < 1e-4f && std::fabs(p.v - 0.5f) < 1e-4f && p.outside == 0 && p.colour == 0xAFAFAFAFu);
    p = headlightAt(low, along(low, 20));
    CHECK(p.colour == 0x74747474u && p.outside == 0);
    p = headlightAt(low, along(low, 45));
    CHECK(p.colour == 0 && (p.outside & 0x20));
    p = headlightAt(low, along(low, -3));
    CHECK(p.colour == 0 && p.outside == 0x10);
    p = headlightAt(low, along(low, 0.5f));
    CHECK((p.outside & 0x10) && p.colour != 0);
    // On the frustum's right edge (x = z): u 1, half the colour; past it,
    // outcode 2, the colour as at the edge.
    p = headlightAt(low, along(low, 5, 5));
    CHECK(std::fabs(p.u - 1) < 1e-4f && p.colour == 0x58585858u && p.outside == 0);
    const HeadlightPoint right = headlightAt(low, along(low, 5, 7));
    CHECK(right.outside == 2 && right.colour == 0x58585858u);
    // Triangles: drawn unless all outside one side, one past the reach, or
    // all nearly transparent.
    const HeadlightPoint a = headlightAt(low, along(low, 5));
    const HeadlightPoint b = headlightAt(low, along(low, 10, 2));
    const HeadlightPoint c = headlightAt(low, along(low, 12, -2));
    const HeadlightPoint far = headlightAt(low, along(low, 41));
    CHECK(headlightTriangle(a, b, c) && !headlightTriangle(a, b, far) && !headlightTriangle(right, right, right));
    CHECK(headlightTriangle(a, b, right));
    // High beams, and without projection; a later car's dimmer reach.
    const Headlight high = carHeadlight(2, true, {0, 0, 0}, 0);
    CHECK(high.fadeFrom == 20 && high.reach == 60 && high.colour == 0xE0E0E0E0u);
    CHECK(std::fabs(high.forward[1] + std::sin(kTilt / 2)) < 1e-5f);
    const Headlight plain = carHeadlight(1, false, {0, 0, 0}, 0);
    CHECK(plain.fadeFrom == 20 && plain.reach == 40 && plain.colour == 0xFFFFFFFFu);
    const Headlight dim = carHeadlight(1, true, {0, 0, 0}, 0, 0.25f);
    CHECK(dim.fadeFrom == 2.5f && dim.reach == 10);
    // Each further car the pass lights for a block gets less (0x41c670):
    // 1 - lit / cars x 0.75.
    CHECK(headlightIntensity(0, 8) == 1 && headlightIntensity(4, 8) == 0.625f && headlightIntensity(8, 8) == 0.25f);
    CHECK(headlightIntensity(0, 0) == 1);
    const Headlight east = carHeadlight(1, true, {0, 0, 0}, 1.5707963f);
    CHECK(std::fabs(east.right[2] + 1) < 1e-5f && std::fabs(east.forward[0] - std::cos(kTilt)) < 1e-5f);
    // The light's place (0x42d160): up by +0x10C, a quarter of the half
    // length along the light's forward (tilted down).
    const std::array<float, 3> at = headlightPlace(low, {10, 2, 30}, 2.4f);
    CHECK(std::fabs(at[0] - 10) < 1e-5f && std::fabs(at[2] - (30 + 0.6f * std::cos(kTilt))) < 1e-5f);
    CHECK(std::fabs(at[1] - (3 - 0.6f * std::sin(kTilt))) < 1e-5f);
    // With the car's rows: the same light for an upright car; on a banked
    // car the place goes up its own up row.
    const Headlight rows = carHeadlight(1, true, {0, 1, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    CHECK(std::fabs(rows.forward[1] - low.forward[1]) < 1e-6f && rows.reach == low.reach && rows.backColour == 0);
    const std::array<float, 3> banked = headlightPlace(low, {0, 0, 0}, {0.6f, 0.8f, 0}, 2.4f);
    CHECK(std::fabs(banked[0] - 0.6f) < 1e-5f && std::fabs(banked[1] - (0.8f - 0.6f * std::sin(kTilt))) < 1e-5f);

    // The lights a race starts with (0x42bcb0): a human's high beams by
    // night, the computer's low; low for all in the weather by day.
    CHECK(startingBeams(true, false, true) == 2 && startingBeams(true, false, false) == 1);
    CHECK(startingBeams(false, true, true) == 1 && startingBeams(false, false, false) == 0);
    CHECK(startingBeams(true, true, false) == 1);

    // A roof light (0x42d020): a unit up, turned a step's 1/32 of a turn
    // about the up; blue ahead, red behind (0x41a550 negates the point).
    const Headlight roof = sirenLight({5, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 8); // a quarter turn
    CHECK(roof.at[1] == 1 && roof.nearPlane == 3.5f && roof.fadeFrom == 15 && roof.reach == 30);
    CHECK(std::fabs(roof.forward[0] - 1) < 1e-5f && std::fabs(roof.right[2] + 1) < 1e-5f);
    CHECK(roof.colour == 0xFF8080FFu && roof.backColour == 0xFFFF8080u);
    p = headlightAt(roof, {15, 1, 0}); // 10 ahead
    CHECK(p.outside == 0 && p.colour == 0xFE7F7FFEu);
    p = headlightAt(roof, {-5, 1, 0}); // 10 behind: red
    CHECK(p.outside == 0 && p.colour == 0xFEFE7F7Fu);
    p = headlightAt(roof, {7, 1, 0}); // 2 ahead: within the near plane
    CHECK((p.outside & 0x10) != 0);
    CHECK(sirenLight({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 40).forward[0] ==
          sirenLight({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 8).forward[0]);

    // The block test (0x41bf30): a block ahead is kept; one whose corners
    // are all wider than deep and off one side is left out; one all
    // behind too, unless the light shines both ways (0x41bc90).
    const Headlight flat = carHeadlight(1, true, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    auto square = [](float x, float z, float half) {
        return std::array<std::array<float, 3>, 4>{std::array<float, 3>{x - half, 0, z - half},
                                                   {x + half, 0, z - half},
                                                   {x + half, 0, z + half},
                                                   {x - half, 0, z + half}};
    };
    CHECK(!lightMissesBlock(flat, square(0, 30, 10)));
    CHECK(lightMissesBlock(flat, square(60, 20, 10)) && lightMissesBlock(flat, square(-60, 20, 10)));
    CHECK(!lightMissesBlock(flat, square(30, 20, 15))); // one corner within
    CHECK(lightMissesBlock(flat, square(0, -40, 10)));
    CHECK(!lightMissesBlock(roof, square(-20, 0, 5), 1, true) && lightMissesBlock(roof, square(0, 60, 5), 1, true));
    // Straddling both sides: no outcode shared, so kept.
    CHECK(!lightMissesBlock(flat, {std::array<float, 3>{-40, 0, 5}, {40, 0, 5}, {40, 0, 6}, {-40, 0, 6}}));

    // Other cars' lights on a car (0x4b97b0): (1 - |x| / z)(1 - z / reach)
    // of a headlight's colour, the lights' mean added.
    CarLights behind;
    behind.headlight = carHeadlight(2, false, {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    behind.headlightOn = true;
    const std::array<float, 3> ahead{0, -0.8f, 30}; // in the beam's frame roughly 30 along
    const uint32_t lit = carLitBy(0xff101010u, ahead, {behind});
    CHECK(lit == 0xff919191u);                                         // 0x10 + 255 x (1 - 29.6 / 60)
    CHECK(carLitBy(0xff101010u, {0, 0, -5}, {behind}) == 0xff101010u); // behind it
    CarLights cop;
    cop.siren = roof;
    cop.sirenOn = true;
    cop.headlight = behind.headlight;
    cop.headlightOn = true;
    // In the roof light's beam (10 ahead of it): 1 - 10/30 of its blue,
    // and then the headlights' share too; out of it, not even those.
    const uint32_t blue = carLitBy(0xff000000u, {15, 1, 0}, {cop});
    CHECK((blue & 0xff) > ((blue >> 16) & 0xff));
    CHECK(carLitBy(0xff000000u, {5, 1, 30}, {cop}) == 0xff000000u);

    // The track's lights on a near car (0x4b9550): 0x496550's distance on
    // 16.16 integers (the largest of |dx|, |dy| plus a quarter of the
    // other, then the same with |dz|); within 16, the colour x (1 - d / 16),
    // the mean of the lights in the seven slices' blocks added.
    CHECK(lightDistance({0, 0, 0}, {0x40000, 0x10000, 0x20000}) == 0x40000 + 0x4000 + 0x8000);
    CHECK(lightDistance({0, 0, 0}, {0x10000, 0, 0x80000}) == 0x80000 + 0x4000);
    Track lights;
    lights.blocks.resize(3);
    Track::Block::Light red;
    red.position = {0, 0, 8}; // 8 from the car: half its colour
    red.type = 9;             // 0xffdf1616, steady
    lights.blocks[1].lightList.push_back(red);
    // Slice 12 (block 1): slices 9-15 are all in block 1, so the light
    // counts seven times, its mean the same.
    CHECK(carLitByTrackLights(0xff101010u, {0, 0, 0}, 12, 24, lights, 0, false) == 0xff7f1b1bu);
    CHECK(carLitByTrackLights(0xff101010u, {0, 0, 0}, 12, 24, lights, 0, true) == 0xff101010u);   // effects off
    CHECK(carLitByTrackLights(0xff101010u, {0, 0, 24}, 12, 24, lights, 0, false) == 0xff101010u); // 16 away
    Track::Block::Light white = red;
    white.type = 2; // 0xffffffd2
    white.position = {0, 0, -8};
    lights.blocks[0].lightList.push_back(white);
    // Slice 8: slices 5-7 in block 0 (the white light, 3 times) and 8-11
    // in block 1 (the red, 4 times): (3 x 127 + 4 x 111) / 7 red.
    const uint32_t both = carLitByTrackLights(0xff000000u, {0, 0, 0}, 8, 24, lights, 0, false);
    CHECK(((both >> 16) & 0xff) == (3 * 127 + 4 * 111) / 7 && ((both >> 8) & 0xff) == (3 * 127 + 4 * 11) / 7);
    Track::Block::Light blink = red;
    blink.type = 7; // blinks on bit 6 of the step
    lights.blocks[2].lightList.push_back(blink);
    CHECK(carLitByTrackLights(0xff000000u, {0, 0, 0}, 20, 24, lights, 0, false) != 0xff000000u &&
          carLitByTrackLights(0xff000000u, {0, 0, 0}, 20, 24, lights, 64, false) == 0xff000000u);
    // The detail a car is drawn at (0x4bb5d0): the nearest only with the
    // car detail option 0, its distances x 0.75 by night.
    CHECK(carDetailLevel(25, 0, false) == 0 && carDetailLevel(25, 0, true) == 1 && carDetailLevel(25, 1, false) == 1 &&
          carDetailLevel(10, 2, false) == 1 && carDetailLevel(250, 0, true) == 3 &&
          carDetailLevel(301, 0, false) == -1);

    // The speed tables: a byte per two slices from the file's first byte
    // (0x40e080; Hometown's speedsF.bin is 1,439 bytes, 480 speeds and 959
    // lanes), 64 without one.
    std::vector<uint8_t> speeds{20, 35};
    CHECK(aiSlicesSpeed(speeds, 1) == 20 && aiSlicesSpeed(speeds, 2) == 35 && aiSlicesSpeed(speeds, 9) == 64);
    CHECK(aiSlicesSpeed({}, 0) == 64);
}

void testCarLighting() {
    // 0x49d8f0: base a third of red and green, half of blue; top twice,
    // at most 255. 0x49da30: base + (top - base) x n . L when lit.
    const CarShade s = carShade(0xff906030u);
    CHECK(s.base[0] == 48 && s.base[1] == 32 && s.base[2] == 24);
    CHECK(s.top[0] == 255 && s.top[1] == 192 && s.top[2] == 96);
    CHECK(carVertexColour(s, -0.5f) == 0xff302018u && carVertexColour(s, 0) == 0xff302018u);
    CHECK(carVertexColour(s, 1) == 0xffffc060u);
    CHECK(carVertexColour(s, 0.5f) == ((0xffu << 24) | (151u << 16) | (112u << 8) | 60u));
    // The light's direction from the horizon's rotation (0x478340).
    const auto north = carLightDirection(0);
    const auto east = carLightDirection(90);
    CHECK(std::fabs(north[2] - 2 / std::sqrt(5.0f)) < 1e-5f && std::fabs(north[1] - 1 / std::sqrt(5.0f)) < 1e-5f);
    CHECK(std::fabs(east[0] - 2 / std::sqrt(5.0f)) < 1e-4f && std::fabs(east[2]) < 1e-4f);
    // The shading under the car (0x4206f0) and the ambient light (0x419660).
    CHECK(carShadeColour({0xff102030u, 0xff102030u, 0xff203040u, 0xff203041u}) == 0xff182838u);
    CHECK(ambientScaled(0x80ff8040u, {50, 100, 33}) == 0x807f8015u);
    // The glows' brightness (0x4b87a0): 1 - 0.3 x sum / 765, 0.7 at least.
    CHECK(carGlowBrightness(0xff000000u) == 1 && carGlowBrightness(0xffffffffu) == 0.7f);
    CHECK(std::fabs(carGlowBrightness(0xff7f7f7fu) - (1 - 0.3f * 381 / 765)) < 1e-5f);

    // The glows (0x4b8740). A tail light seen from behind: red, 128 or
    // 198 braking; only with the lights on or braking.
    CarGlow g = carLightGlow("TRLN", -1, 1, false, false, 0, 1);
    CHECK(g.shown && g.colour == 0x80ff0000u && std::fabs(g.size - 0.8f) < 1e-6f && g.nearer == 0);
    CHECK(carLightGlow("TRLN", -1, 0, true, false, 0, 1).colour == 0xc6ff0000u);
    CHECK(!carLightGlow("TRLN", -1, 0, false, false, 0, 1).shown &&
          !carLightGlow("TRLN", 1, 1, false, false, 0, 1).shown);
    // Seen from the side: dimmer under a quarter, the size kept at 0.25 of it.
    g = carLightGlow("TRRN", -0.1f, 1, false, false, 0, 0.7f);
    CHECK(g.colour == ((uint32_t(std::lrint(128 * 0.4f * 0.7f)) << 24) | 0xff0000u));
    CHECK(std::fabs(g.size - 0.25f * 0.8f * 0.7f) < 1e-6f);
    // A headlight from ahead: white, half as large again on high beams.
    g = carLightGlow("HFLO", 1, 2, false, false, 0, 1);
    CHECK(g.shown && g.colour == 0xffffffffu && std::fabs(g.size - 1.2f) < 1e-6f);
    // A police car's: flashing in turn with the siren on, lights off.
    CHECK(carLightGlow("HFLO", 1, 0, false, true, 4, 1).shown && !carLightGlow("HFRE", 1, 0, false, true, 4, 1).shown);
    CHECK(!carLightGlow("HFLO", 1, 0, false, true, 20, 1).shown &&
          carLightGlow("HFRE", 1, 0, false, true, 20, 1).shown);
    // The roof lights: blue left, red right, rising and falling in turn.
    g = carLightGlow("SMLN", 0, 0, false, true, 8, 1); // phase 1/4: share 1/2
    CHECK(g.shown && g.colour == 0x808080ffu && std::fabs(g.size - 0.75f) < 1e-6f && g.nearer == 0.75f);
    g = carLightGlow("SMRN", 0, 0, false, true, 16, 1); // share 1: red gone
    CHECK(g.colour == 0x00ff8080u && std::fabs(g.size - 0.375f) < 1e-6f);
    CHECK(!carLightGlow("SMLN", 0, 1, false, false, 8, 1).shown);
}

void testCarReflection() {
    // 0x49c880 and 0x49ca50: integer HSV, the hue a turn in 256.
    CHECK((rgbToHsv({255, 0, 0}) == std::array<int, 3>{0, 255, 255}));
    CHECK((rgbToHsv({0, 255, 0}) == std::array<int, 3>{85, 255, 255}));
    CHECK((rgbToHsv({128, 128, 128}) == std::array<int, 3>{0, 0, 128}));
    CHECK((rgbToHsv({100, 150, 200}) == std::array<int, 3>{149, 128, 200}));
    CHECK((hsvToRgb({0, 255, 255}) == std::array<int, 3>{255, 0, 0}));
    CHECK((hsvToRgb({85, 255, 255}) == std::array<int, 3>{2, 255, 0}));
    CHECK((hsvToRgb({40, 0, 90}) == std::array<int, 3>{90, 90, 90}) &&
          (hsvToRgb({40, 200, 0}) == std::array<int, 3>{0, 0, 0}));
    // 0x4ba350: the sky's hue; value 128 + 200 x 127 / 512 = 177, less a
    // quarter of the paint's value (112) for the normal one; all x 255 / 256.
    CarReflection r = carReflection(0xff7f7f7fu, {100, 150, 200}, 112, 0);
    CHECK((r.normal >> 24) == 149 * 255 / 256 && (r.strong >> 24) == 177 * 255 / 256);
    CHECK((r.normal & 0xff) > ((r.normal >> 16) & 0xff) && (r.strong & 0xff) > ((r.strong >> 16) & 0xff));
    // The strong one keeps half the sky's saturation, the normal 96 / 256.
    CHECK((r.strong & 0xff) - ((r.strong >> 16) & 0xff) > (r.normal & 0xff) - ((r.normal >> 16) & 0xff));
    // Deep under cover the sky is the car's own grey, both dimmed to half.
    r = carReflection(0xff7f7f7fu, {100, 150, 200}, 0, 8);
    CHECK((r.strong >> 24) == (128 + 127 * 127 / 512) * 128 / 256 && ((r.strong >> 16) & 0xff) == (r.strong & 0xff));
    // The frame (0x49d780): from a camera 5 ahead along z, x is the
    // track's -x and z points from the camera to the car.
    auto f = reflectionFrame({0, 0, 5});
    CHECK(std::fabs(f[0][0] + 1) < 1e-5f && std::fabs(f[1][1] - 1) < 1e-5f && std::fabs(f[2][2] + 1) < 1e-5f);
    f = reflectionFrame({0, 5, 0}); // from straight above
    CHECK(std::fabs(f[2][1] + 1) < 1e-5f);
}

void testLightEffects() {
    // 0x491190: glw0 fading with the depth, glw3 at full colour; none
    // nearer than 1 or past 256, nor blinking off.
    const LightLook white{0xffffffffu, 4, true};
    CHECK(!lightQuads(white, 0.5f).shown && !lightQuads(white, 300).shown &&
          !lightQuads({0xffffffffu, 4, false}, 10).shown);
    LightQuads q = lightQuads(white, 1);
    CHECK(q.shown && q.glowColour == 0xfefefefeu && q.flareColour == 0xffffffffu);
    CHECK(std::fabs(q.screenHalf - 0.1f) < 1e-6f && std::fabs(q.worldHalf - 1.05f) < 1e-6f);
    CHECK(lightQuads(white, 128).glowColour == 0x7f7f7f7fu && lightQuads(white, 256).glowColour == 0);
    // 0x491bc0: a headlight 10 ahead facing the camera: 1 either side,
    // 7.5 long towards the camera, 166 / 256 of the glow's colour.
    RainFlare f = rainFlare({0, 1, 10}, {0, 2, 0}, {0, 0, -1}, 2, 0xffffffffu);
    CHECK(f.shown && f.colour == 0xa5a5a5a5u && std::fabs(f.depth - 10) < 1e-5f);
    CHECK(std::fabs(f.corners[0][0] - 1) < 1e-5f && std::fabs(f.corners[0][1] - 10) < 1e-5f);
    CHECK(std::fabs(f.corners[1][0] + 1) < 1e-5f && std::fabs(f.corners[2][1] - 2.5f) < 1e-5f);
    // Far away it is 30 long at most; seen side on, as long as half wide.
    CHECK(std::fabs(rainFlare({0, 1, 100}, {0, 2, 0}, {0, 0, -1}, 2, 0).corners[3][1] - 70) < 1e-4f);
    CHECK(std::fabs(rainFlare({0, 1, 10}, {0, 2, 0}, {1, 0, 0}, 2, 0).corners[3][1] - 9) < 1e-5f);
    CHECK(!rainFlare({0, 1, 0}, {0, 5, 0}, {0, 0, 1}, 2, 0).shown);
    CHECK(wetSurface(1) && wetSurface(10) && !wetSurface(14) && !wetSurface(0) && !wetSurface(2));
    // 0x4b81a0: the shadow's corners pushed along their edges away from
    // the light by the half height, all 0.125 up; flat without the cast.
    {
        const std::array<std::array<float, 3>, 4> box{{{-1, 0, 2}, {1, 0, 2}, {1, 0, -2}, {-1, 0, -2}}};
        const auto lit = carShadowQuad(box, {1, 0, 0}, 0.5f, true);
        CHECK(std::fabs(lit[0][0] + 1.5f) < 1e-6f && std::fabs(lit[3][0] + 1.5f) < 1e-6f);
        CHECK(lit[1][0] == 1 && lit[2][0] == 1 && lit[0][2] == 2 && lit[3][2] == -2);
        CHECK(lit[0][1] == 0.125f && lit[2][1] == 0.125f);
        // From high in front and to the right: the left and back corners
        // pushed by their share; the front right one stays.
        const float k = 1 / std::sqrt(3.0f);
        const auto high = carShadowQuad(box, {k, k, k}, 0.6f, true);
        CHECK(high[1][0] == 1 && high[1][2] == 2);
        CHECK(std::fabs(high[3][0] + 1 + k * 0.6f) < 1e-5f && std::fabs(high[3][2] + 2 + k * 0.6f) < 1e-5f);
        CHECK(std::fabs(high[0][0] + 1 + k * 0.6f) < 1e-5f && high[0][2] == 2);
        const auto flat = carShadowQuad(box, {1, 0, 0}, 0.5f, false);
        CHECK(flat[0][0] == -1 && flat[0][1] == 0.125f && kCarShadowColour == 0x6fffffffu);
    }
    // 0x4b8740 and 0x492980: the beam's colour over 30 % fog, and its cone.
    CHECK(fogBeamColour(30, 2) == 0 && fogBeamColour(60, 2) == 0xe4ffffffu && fogBeamColour(45, 1) == 0x39ffffffu);
    const std::array<float, 3> right{1, 0, 0};
    const std::array<float, 3> up{0, 1, 0};
    const std::array<float, 3> ahead{0, 0, 1};
    FogBeam b = fogBeam({0, 0, 0}, right, up, ahead, {0, 0, -50}, 0x80ffffffu);
    CHECK(b.shown && b.nearRing.size() == 7 && b.farRing.size() == 7 && b.colour == 0x80ffffffu);
    CHECK(std::fabs(b.farRing[0][2] - 6) < 1e-5f &&
          std::fabs(std::hypot(b.farRing[2][0], b.farRing[2][1]) - 0.75f) < 1e-5f);
    CHECK(std::fabs(std::hypot(b.nearRing[3][0], b.nearRing[3][1]) - 0.1f) < 1e-5f && b.nearRing[3][2] == 0);
    CHECK(fogBeam({0, 0, 0}, right, up, ahead, {0, 0, 98}, 0x80ffffffu).nearRing.size() == 4); // 3 quads at least
    CHECK(fogBeam({0, 0, 0}, right, up, ahead, {0, 0, 30}, 0x80ffffffu).colour == 0);          // looking into it
    CHECK(!fogBeam({0, 0, 0}, right, up, ahead, {0, 0, -150}, 0x80ffffffu).shown);
}

void testSkinPaint() {
    // The loader's recolouring (0x49cd00, 0x49cc50): alpha under 128 the
    // paint, 128-254 its secondary colour, 255 kept, 0 transparent.
    Model::Colour paint{{44, 12, 128, 128}};
    Model::Colour second{{126, 48, 38, 128}};
    const auto painted = skinTexel({105, 105, 106, 50}, &paint, &second);
    // t = 104 / 127, the value trunc(128 t) = 104, at hue 44 and saturation
    // 12, to 5 bits.
    const auto expect = hsvToRgb({44, 12, 104});
    CHECK(painted[0] == ((expect[0] >> 3) << 3) && painted[1] == ((expect[1] >> 3) << 3) &&
          painted[2] == ((expect[2] >> 3) << 3) && painted[3] == 255);
    const auto window = skinTexel({120, 117, 116, 200}, &paint, &second);
    const auto expectWindow = hsvToRgb({126, 48, 33}); // trunc(38 x 112 / 127)
    CHECK(window[2] == ((expectWindow[2] >> 3) << 3) && window[0] == ((expectWindow[0] >> 3) << 3));
    CHECK((skinTexel({10, 20, 30, 255}, &paint, &second) == std::array<uint8_t, 4>{10, 20, 30, 255}));
    CHECK(skinTexel({10, 20, 30, 0}, &paint, &second)[3] == 0);
    CHECK((skinTexel({10, 20, 30, 50}, nullptr, nullptr) == std::array<uint8_t, 4>{10, 20, 30, 255}));
    // The rows as stored (0x49cd00): a TGA stored from the bottom (bit 5 of
    // byte 17 clear) is turned back over after decoding; one from the top
    // is left.
    image::Image two;
    two.w = 1;
    two.h = 2;
    two.rgba = {1, 1, 1, 1, 2, 2, 2, 2};
    std::vector<uint8_t> header(18, 0);
    skinRowsAsStored(header, two);
    CHECK(two.rgba[0] == 2 && two.rgba[4] == 1);
    header[17] = 0x28;
    skinRowsAsStored(header, two);
    CHECK(two.rgba[0] == 2);
    // The colour a paint goes with (0x49d240), by RGB distance.
    Model m;
    m.primaryColours = {Model::Colour{{0, 255, 99, 108}}, Model::Colour{{44, 12, 128, 128}}};
    m.secondaryColours = {Model::Colour{{1, 1, 1, 1}}, Model::Colour{{2, 2, 2, 2}}};
    CHECK(primaryFor(m, {43, 12, 128}) == 1 && secondaryFor(m, {0, 250, 100}) == 0);
}

} // namespace

static const reluva::test::Suite
    kSuite("nfs.unit.track", {
                                 {"track", [](const stdfs::path &) { testTrack(); }},
                                 {"light look", [](const stdfs::path &) { testLightLook(); }},
                                 {"particles", [](const stdfs::path &) { testParticles(); }},
                                 {"car effects", [](const stdfs::path &) { testCarEffects(); }},
                                 {"weather", [](const stdfs::path &) { testWeather(); }},
                                 {"polygon animation", [](const stdfs::path &) { testPolygonAnimation(); }},
                                 {"reflection uv", [](const stdfs::path &) { testReflectionUv(); }},
                                 {"placed sounds", [](const stdfs::path &) { testPlacedSounds(); }},
                                 {"headlights", [](const stdfs::path &) { testHeadlights(); }},
                                 {"view and road frame", [](const stdfs::path &) { testViewAndRoadFrame(); }},
                                 {"car lighting", [](const stdfs::path &) { testCarLighting(); }},
                                 {"car reflection", [](const stdfs::path &) { testCarReflection(); }},
                                 {"light effects", [](const stdfs::path &) { testLightEffects(); }},
                                 {"skin paint", [](const stdfs::path &) { testSkinPaint(); }},
                             });
