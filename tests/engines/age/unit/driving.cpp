// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Angel Game Engine's unit tests: the cars: collision, the simulation, engine sounds, their files, a game controller, texture quality.
// Every file is built in the test.

#include "common/headless_system.h"
#include "tests/engines/age/support.h"

using namespace reluva;
using namespace reluva::age;
using namespace reluva::age::test_support;

namespace {

void testCollision() {
    CollisionWorld w;
    // A floor at y 0 over -100..100 and a wall at x 10.
    w.add({-100, 0, -100}, {-100, 0, 100}, {100, 0, 100});
    w.add({-100, 0, -100}, {100, 0, 100}, {100, 0, -100});
    w.add({10, 0, -5}, {10, 5, -5}, {10, 5, 5});
    w.add({10, 0, -5}, {10, 5, 5}, {10, 0, 5});
    w.build();
    CollisionWorld::RayHit hit;
    CHECK(w.raycast({3, 2, 4}, {0, -1, 0}, 5, hit) && std::fabs(hit.distance - 2) < 1e-4f && hit.normal[1] == 1);
    CHECK(!w.raycast({3, 2, 4}, {0, -1, 0}, 1.5f, hit));
    CHECK(w.raycast({0, 1, 0}, {1, 0, 0}, 20, hit) && std::fabs(hit.distance - 10) < 1e-4f && hit.normal[0] == -1);
    CollisionWorld::Contact c;
    CHECK(w.sphereContact({9.8f, 1, 0}, 0.5f, c) && c.normal[0] < -0.99f && std::fabs(c.depth - 0.3f) < 1e-4f);
    CHECK(w.sphereContact({0, 0.1f, 0}, 0.25f, c) && c.normal[1] > 0.99f);
    CHECK(!w.sphereContact({0, 2, 0}, 0.5f, c));
}

void testCarSim() {
    CollisionWorld flat;
    flat.add({-3000, 0, -3000}, {-3000, 0, 3000}, {3000, 0, 3000});
    flat.add({-3000, 0, -3000}, {3000, 0, 3000}, {3000, 0, -3000});
    flat.build(64);
    const CarSetup setup = testCar();
    CHECK(setup.gearTopSpeed(1) == setup.lowSpeed &&
          std::fabs(setup.gearTopSpeed(setup.gears) - setup.highSpeed) < 1e-3f);
    CHECK(setup.power(setup.optRpm) > setup.power(setup.idleRpm) && setup.power(setup.maxRpm + 1) == 0);

    CarSim car;
    car.reset(setup, {0, 0, 0}, 0);
    CarControls none;
    car.step(none, flat, 0.1f);
    for (int i = 0; i < 30; ++i)
        car.step(none, flat, 0.1f);
    // Settled on its wheels.
    CHECK(length(car.velocity) < 0.05f);
    for (const auto &w : car.wheels)
        CHECK(w.contact && w.compression > 0.02f && w.compression < 0.29f);
    CHECK(car.orientation.y[1] > 0.999f);

    // Full throttle: off along -Z, up through the gears.
    CarControls go;
    go.throttle = 1;
    for (int i = 0; i < 100; ++i)
        car.step(go, flat, 0.1f);
    CHECK(car.forwardSpeed() > 15 && car.position[2] < -50 && std::fabs(car.position[0]) < 2 && car.gear > 1);
    for (int i = 0; i < 300; ++i) // 30 s more, not quite to the plane's edge
        car.step(go, flat, 0.1f);
    // Topped out at the top gear's speed (at the redline).
    CHECK(car.gear == setup.gears && car.forwardSpeed() > setup.highSpeed * 0.95f &&
          car.forwardSpeed() < setup.highSpeed * 1.05f);

    // Steering right at speed turns it right (towards +X when facing -Z).
    car.reset(setup, {0, 0, 0}, 0);
    for (int i = 0; i < 50; ++i)
        car.step(go, flat, 0.1f);
    CarControls right = go;
    right.throttle = 0.3f;
    right.steer = 1;
    const float x0 = car.position[0];
    for (int i = 0; i < 20; ++i)
        car.step(right, flat, 0.1f);
    CHECK(car.position[0] > x0 + 5 && car.orientation.y[1] > 0.95f);

    // Braking stops it; holding the brake then backs up.
    CarControls stop;
    stop.brake = 1;
    for (int i = 0; i < 100; ++i)
        car.step(stop, flat, 0.1f);
    CHECK(car.gear == -1 && car.forwardSpeed() < -1);

    // Manual gears: no change by itself; up and down as asked; reverse from
    // first only when stopped, the throttle then backing up.
    car.reset(setup, {0, 0, 0}, 0);
    car.manual = true;
    for (int i = 0; i < 60; ++i)
        car.step(go, flat, 0.1f);
    CHECK(car.gear == 1 && car.rpm > setup.maxRpm * 0.9f);
    CarControls up = go;
    up.shift = 1;
    car.step(up, flat, 0.1f);
    car.step(go, flat, 0.1f);
    CHECK(car.gear == 2);
    CarControls down;
    down.shift = -1;
    car.step(down, flat, 0.1f);
    car.step(down, flat, 0.1f); // first, but moving: not reverse
    CHECK(car.gear == 1);
    for (int i = 0; i < 100; ++i)
        car.step(stop, flat, 0.1f);
    car.step(down, flat, 0.1f);
    CHECK(car.gear == -1);
    for (int i = 0; i < 20; ++i)
        car.step(go, flat, 0.1f);
    CHECK(car.forwardSpeed() < -1);

    // On its roof and stuck: set upright, the way it faced, after
    // stuckTime.
    car.reset(setup, {0, 2, 0}, 0.5f);
    car.manual = false;
    car.orientation = Mat3::rotation({0, 0, 1}, 3.0f) * car.orientation;
    for (int i = 0; i < 10; ++i)
        car.step(none, flat, 0.1f);
    CHECK(car.orientation.y[1] < 0);
    for (int i = 0; i < 40; ++i)
        car.step(none, flat, 0.1f);
    CHECK(car.orientation.y[1] > 0.95f);
}

void testEngineSound() {
    EngineSoundSetup e;
    CHECK(!readEngineSound("Horn wave name,Horn volume,flags,Num Engine Samples,clutch wave name,clutch volume,,,,,\r\n"
                           "VWHORN,0.95,0,2,REVERSE,0.93,,,,,\r\n"
                           "Engine wave name,Min Volume,Max Volume,...\r\n"
                           "VWIDLE,0.55,0.835,1,800,2500,7000,0.85,2,1,7000\r\n"
                           "VWHIGH,0.55,0.91,3000,8000,15000,15000,0.65,2.25,3000,12000\r\n",
                           e));
    CHECK(e.horn == "VWHORN" && e.samples.size() == 2 && e.samples[1].wave == "VWHIGH");
    CHECK(e.hornVolume == 0.95f && e.clutch == "REVERSE" && e.clutchVolume == 0.93f);
    CHECK(e.samples[0].fadeOutStart == 2500 && e.samples[1].pitchEnd == 12000);
    float volume, pitch;
    // Idle: faded in by 800 RPM, out from 2500 to 7000; pitched 0.85 to 2 over 1-7000.
    engineSampleMix(e.samples[0], 800, 0, volume, pitch);
    CHECK(std::fabs(volume - 0.55f) < 1e-4f && pitch > 0.85f && pitch < 1.0f);
    engineSampleMix(e.samples[0], 800, 1, volume, pitch);
    CHECK(std::fabs(volume - 0.835f) < 1e-4f);
    engineSampleMix(e.samples[0], 7000, 1, volume, pitch);
    CHECK(volume == 0 && std::fabs(pitch - 2) < 1e-4f);
    engineSampleMix(e.samples[1], 2000, 1, volume, pitch); // not faded in yet
    CHECK(volume == 0);
    CHECK(readEngineSound("a\nb\n", e));
}

void testCarFiles() {
    // city/materials.mtl and .csv.
    PhysicsMaterials m;
    CHECK(!m.parse("mtl deepwater {\n\telasticity: 0.5\n\tfriction: 0.65\n\tdrag: 0.5\n\tdepth: 100.0\n}\n"
                   "mtl _default {\n\tfriction: 0.9\n\tsound: 0\n}\n"
                   "mtl grass {\n\tfriction: 0.45\n\tsound: 2\n\twidth: 0.45\n\theight: 0.04\n"
                   "\tptxindex: 1 2\n\tptxthreshold: 0.25 0.5\n}\n",
                   "texture,physics\r\nS_THAMES,deepwater\r\nlawn,grass\r\nroad,none\r\n"));
    CHECK(m.forTexture("s_thames-0009").name == "deepwater" && m.forTexture("s_thames").depth == 100);
    CHECK(m.forTexture("LAWN").name == "grass" && m.forTexture("road").name == "_default");
    CHECK(m.byName("nothing").name == "_default");
    const CollisionSurface grass = m.surface(m.byName("grass"));
    CHECK(std::fabs(grass.friction - CollisionSurface{}.friction * 0.5f) < 1e-4f && grass.sound == 2);
    CHECK(grass.particles[0] == 1 && grass.particles[1] == 2 && grass.particleSlip[1] == 0.5f &&
          grass.spread == 0.45f && grass.rise == 0.04f);
    CHECK(m.surface(m.byName("deepwater")).particles[0] == -1);
    CHECK(m.surface(m.byName("deepwater")).drag == 0.5f);
    CHECK(m.parse("mtl open {\nfriction: 1\n", ""));

    // The tyres' and knocks' sound tables.
    SurfaceSounds surfaces;
    CHECK(!readSurfaceSounds("Tunnel sound index,,,\r\n3,,,\r\n"
                             "surface wave,max speed,min surface volume,max surface volume,min surface pitch,max "
                             "surface pitch,min skid volume,max skid volume,num skid samples\r\n"
                             "NOSOUND,125,0,0,0,0,0.5,0.88,2\r\n"
                             "skid wave,min slippage,max slippage\r\n"
                             "tireskid1,0.55,0.65\r\ntireskid2,0.65,1\r\n"
                             "surface wave,max speed\r\nGRAVEL,40,0.2,0.9,0.8,1.2,0.4,0.8,0\r\n",
                             surfaces));
    CHECK(surfaces.tunnel == 3 && surfaces.surfaces.size() == 2);
    if (surfaces.surfaces.size() == 2) {
        CHECK(surfaces.surfaces[0].wave == "NOSOUND" && surfaces.surfaces[0].skids.size() == 2 &&
              surfaces.surfaces[0].skids[1].wave == "tireskid2" && surfaces.surfaces[0].skids[1].maxSlip == 1);
        CHECK(surfaces.surfaces[1].wave == "GRAVEL" && surfaces.surfaces[1].maxSpeed == 40 &&
              surfaces.surfaces[1].skids.empty());
    }
    std::vector<ImpactSounds> impacts;
    CHECK(!readImpactSounds("***\r\nBanger name,Num samples,ID\r\nWALL,2,0\r\n"
                            "sample name,min volume,max volume,min force,max force,frequency\r\n"
                            "soft,0.9,0.93,1000,8000,1\r\nhard,0.94,1,8000,999999,1\r\n"
                            "***\r\nBanger name,Num samples,ID\r\nENDOFDATA\r\n",
                            impacts));
    CHECK(impacts.size() == 1 && impacts[0].name == "WALL" && impacts[0].samples.size() == 2 &&
          impacts[0].samples[1].minForce == 8000 && impacts[0].samples[0].maxVolume == 0.93f);

    // .vehCarDamage: knocks over the threshold count what they're over it.
    DamageSetup d;
    CHECK(!parseDamageSetup("type: a\nvehCarDamage {\n  MaxDamage 10000\n  MedDamage 4000\n  ImpactThreshold 1500\n"
                            "  RegenerateRate 100\n  SmokeOffset -0.13 0.6 -1.65\n  Gravity 15\n"
                            "  TexFrameStart 2\n  Color -167772161\n}\n",
                            d));
    CHECK(d.maxDamage == 10000 && d.smokeOffset[2] == -1.65f && d.smokeOffset2[2] == -1.65f && d.smoke.frame == 2 &&
          d.smoke.frameEnd == 2);
    {
        // tune/effects/*.asBirthRule: the same rule.
        TuneNode root;
        CHECK(!parseTune("type: a\nasBirthRule {\n  PositionVar 0.5 0 0.5\n  Drag 0.2\n  DAlpha -5\n"
                         "  InitialBlast 32\n  SpewRate 8\n  SpewTimeLimit 10\n  TexFrameStart 16\n"
                         "  TexFrameEnd 21\n}\n",
                         root));
        const BirthRule r = parseBirthRule(root.children.front());
        CHECK(r.positionVar[2] == 0.5f && r.drag == 0.2f && r.dAlpha == -5 && r.initialBlast == 32 && r.spewRate == 8 &&
              r.spewTime == 10 && r.frame == 16 && r.frameEnd == 21);
    }
    CHECK(std::fabs(d.smoke.color[3] - 0xf5 / 255.0f) < 1e-4f && d.smoke.color[0] == 1);
    CarDamage damage;
    CHECK(damage.knock(d, 1400) == 0 && damage.knock(d, 3500) == 2000 && !damage.smoking(d));
    damage.knock(d, 4000);
    CHECK(damage.smoking(d) && !damage.wrecked(d));
    damage.update(d, 1);
    CHECK(damage.damage() == 4400);
    damage.knock(d, 50000);
    CHECK(damage.wrecked(d) && damage.fraction(d) == 1);

    // tune/<city>.mmHudMap: two-word names come with their second word.
    HudMapSetup h;
    CHECK(!parseHudMapSetup("type: a\nmmHudMap {\n  Size 0.21 0.25\n  Pos 0.78 0.75\n  ZoomIn 0\n"
                            "  Approach Rate 1.2\n  ZoomInDist 577\n  ZoomOutDistFS 1581\n"
                            "  Ocean Color 0.084  0.7 0.94\n}\n",
                            h));
    CHECK(h.width == 0.21f && h.top == 0.75f && !h.zoomIn && h.approachRate == 1.2f && h.zoomInDist == 577 &&
          h.zoomOutDistFS == 1581 && h.ocean[0] == 0.084f && h.ocean[2] == 0.94f);
}

// A headless System with a game controller in a given state, keeping the
// forces it's asked to give.
class PadSystem : public HeadlessSystem {
public:
    GamepadState pad;
    std::vector<GamepadForce> forces;
    bool readGamepad(GamepadState &out) override {
        out = pad;
        return true;
    }
    void setGamepadForce(const GamepadForce &force) override { forces.push_back(force); }
};

void testPadInput() {
    // The sticks with their dead zones, added to the keys.
    PadDrive pad;
    pad.axis(0, 0.1f);
    CHECK(pad.steer == 0 && pad.idle());
    pad.axis(0, -1);
    pad.axis(5, 0.5f);
    CHECK(pad.steer == -1 && pad.throttle > 0.45f && pad.throttle < 0.5f);
    CarControls c;
    c.brake = 1;
    pad.apply(c);
    CHECK(c.steer == -1 && c.throttle == pad.throttle && c.brake == 1);
    // Buttons: the controls' keys while driving, the menus' keys otherwise.
    GameOptions o;
    CHECK(padKey(kPadSouth, true, o) == o.keys[size_t(Control::kHandbrake)][0]);
    CHECK(padKey(kPadRightShoulder, true, o) == o.keys[size_t(Control::kShiftUp)][0]);
    CHECK(padKey(kPadStart, true, o) == key::kEscape && padKey(kPadSouth, false, o) == key::kReturn);
    CHECK(padKey(kPadDown, false, o) == key::kDown && padKey(kPadGuide, true, o) == 0);
    // Polled: the axes read, a button pressed then let go as key events.
    PadSystem sys;
    PadDrive polled;
    std::vector<Event> keys;
    sys.pad.axes[0] = 0.5f;
    sys.pad.axes[4] = 1;
    sys.pad.buttons = 1u << kPadEast;
    pollPad(sys, polled, false, true, o, keys);
    CHECK(polled.steer > 0.4f && polled.brake == 1 && keys.size() == 1 && keys[0].type == EventType::kKeyDown &&
          keys[0].key == key::kEscape);
    keys.clear();
    sys.pad.buttons = 0;
    pollPad(sys, polled, false, false, o, keys); // the axes left as they were (a replay's)
    CHECK(keys.size() == 1 && keys[0].type == EventType::kKeyUp && polled.brake == 1);
    // A knock shakes it for a fifth of a second.
    CHECK(padRumble(1) == 0 && padRumble(15) == 1 && padRumble(8) > 0);
    PadShaker shaker;
    shaker.knock(sys, 0.5f, 1000);
    shaker.update(sys, 1100);
    CHECK(sys.forces.size() == 1 && sys.forces[0].vibration == 0.5f);
    shaker.update(sys, 1200);
    CHECK(sys.forces.size() == 2 && sys.forces[1] == GamepadForce{});
}

void testTextureQuality(const std::filesystem::path &tmp) {
    // Two 64x64 textures, one flagged to keep its detail, and a 16x16.
    auto tex = [&](const char *name, int size, uint32_t flags) {
        Bytes b;
        b.bytes(texHeader(uint16_t(size), uint16_t(size), texformat::kRgb, 1, flags));
        for (int i = 0; i < size * size * 3; ++i)
            b.u8(uint8_t(i));
        test::writeFile(tmp / "texture" / (std::string(name) + ".tex"), b.v);
    };
    tex("plain", 64, 0);
    tex("near", 64, texflag::kFullDetail);
    tex("tiny", 16, 0);
    SearchSet files;
    auto loose = std::make_unique<DirectoryArchive>();
    CHECK(!loose->open(tmp));
    files.add(std::move(loose), 1);
    SizeRenderer renderer;
    TextureCache cache(renderer, files);
    cache.setHalvings(3); // Low
    bool translucent = false;
    CHECK(cache.get("plain", translucent) && cache.get("near", translucent) && cache.get("tiny", translucent));
    // Halved down to 16 at least; the flagged one and the small one kept.
    CHECK(renderer.sizes == (std::vector<std::pair<int, int>>{{16, 16}, {64, 64}, {16, 16}}));
    int w = 0, h = 0;
    CHECK(cache.size("plain", w, h) && w == 64 && h == 64); // its own size, as stored
}

} // namespace

static const reluva::test::Suite
    kSuite("age.unit.driving",
           {
               {"collision", [](const std::filesystem::path &) { testCollision(); }},
               {"car sim", [](const std::filesystem::path &) { testCarSim(); }},
               {"engine sound", [](const std::filesystem::path &) { testEngineSound(); }},
               {"car files", [](const std::filesystem::path &) { testCarFiles(); }},
               {"pad input", [](const std::filesystem::path &) { testPadInput(); }},
               {"texture quality", [](const std::filesystem::path &tmp) { testTextureQuality(tmp); }},
           });
