// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Particle systems: envelopes moving and ending each way, bursts at their
// frequency, one that stops after its bursts and is turned on again,
// particles rising, falling and bouncing, swirling, their start shapes;
// and on a map, a system turned on by its message and one made by an
// action, gone with its part.

#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/game/particles.h"
#include "engines/stawayteam/ui/map_screen.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <cmath>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

TextObject objectOf(const std::string &text) {
    TextObject o;
    CHECK(!readTextObject(text, o));
    return o;
}

std::string envelope(const std::string &key, float start, float end, float speed, const char *ending = "Stop at end") {
    return key + "=CEnvelope\n{\nStart Value=" + std::to_string(start) + "\nEnd Value=" + std::to_string(end) +
           "\nSpeed=" + std::to_string(speed) + "\nEnding Behavior=" + ending + "\n}\n";
}

// A system, its Particles one kind, of `ai` (its CParticleInfo's AI).
std::string system(const std::string &more, const std::string &ai, int count = 3, float age = 1.0f,
                   float speed = 10.0f) {
    return "CGenericParticleSystemAI\n{\n" + envelope("Outer Cone Angle", 0, 0, 0) +
           envelope("Inner Cone Angle", 0, 0, 0) + envelope("Face Angle", 0, 0, 0) +
           envelope("Cone Trajectory", 90, 0, 0) + envelope("Speed", speed, 0, 0) + envelope("Age", age, 0, 0) +
           "Burst Frequency=2\nEnabled=1\n" + more +
           "Particles=Array\n{\nItem Count=1\nParticles=CParticleInfo\n{\n"
           "Model=Particles/Spark\nAI=" +
           ai + "\nAnimated=1\nNum Particles=" + std::to_string(count) + "\n}\n}\n}\n";
}

// Runs it till its first burst: that comes a random part of the way
// through its first period.
void firstBurst(ParticleSystem &s) {
    for (int i = 0; i < 100000 && s.particles().empty(); ++i)
        s.update(0.0001f);
}

void testEnvelopes() {
    // 20 to 60 at 30 a second: there in 4/3 seconds, and then as each ends.
    TextObject o = objectOf(envelope("E", 20, 60, 30).substr(2));
    Envelope e = Envelope::read(&o);
    CHECK(e.now() == 20.0f);
    e.update(1.0f);
    CHECK(e.now() == 50.0f);
    e.update(1.0f);
    e.update(1.0f);
    CHECK(e.now() == 60.0f);
    o = objectOf(envelope("E", 20, 60, 30, "Reverse Direction").substr(2));
    e = Envelope::read(&o);
    e.update(2.0f); // to 60
    e.update(1.0f); // and back 30
    CHECK(e.now() == 30.0f);
    o = objectOf(envelope("E", 20, 60, 30, "Restart from beginning").substr(2));
    e = Envelope::read(&o);
    e.update(2.0f);
    e.update(0.5f); // from 20 again
    CHECK(e.now() == 35.0f);
    // At a speed of 0 it's its start, whatever its end.
    o = objectOf(envelope("E", 7, 90, 0).substr(2));
    e = Envelope::read(&o);
    e.update(5.0f);
    CHECK(e.now() == 7.0f);
    // Going down.
    o = objectOf(envelope("E", 10, 0, 4).substr(2));
    e = Envelope::read(&o);
    e.update(1.0f);
    CHECK(e.now() == 6.0f);
}

void testBursts() {
    // Two bursts a second of three, rising straight up at 10, gone at 1.
    const TextObject o = objectOf(system("Height=5\n", "CFloatParticleAI\n{\n}"));
    ParticleSystem s(o, 7);
    int bursts = 0;
    for (int i = 0; i < 100; ++i)
        bursts += s.update(0.01f);
    CHECK(bursts == 2 && s.particles().size() == 6);
    for (const ParticleSystem::Particle &p : s.particles())
        CHECK(std::abs(p.u) < 0.01f && std::abs(p.v) < 0.01f && p.z > 5.0f && p.z <= 15.0f && p.age < 1.0f);
    // Each goes at its age: none is older than 1 second.
    for (int i = 0; i < 300; ++i)
        s.update(0.01f);
    for (const ParticleSystem::Particle &p : s.particles())
        CHECK(p.age < 1.0f);
    CHECK(s.particles().size() <= 6);
    // One that stops after one burst; turned on again, it bursts once more.
    const TextObject once = objectOf(system("Num Bursts To Stop After=1\n", "CFloatParticleAI\n{\n}", 2));
    ParticleSystem t(once, 3);
    bursts = 0;
    for (int i = 0; i < 300; ++i)
        bursts += t.update(0.01f);
    CHECK(bursts == 1 && !t.enabled());
    t.turn(true);
    for (int i = 0; i < 300; ++i)
        bursts += t.update(0.01f);
    CHECK(bursts == 2);
    t.turn(false);
    for (int i = 0; i < 300; ++i)
        bursts += t.update(0.01f);
    CHECK(bursts == 2);

    // A burst's sound: by chance, or, Chosen Based On Burst, by how many
    // particles it made of the most it could: all 3 of 3, the last of 4
    // (0x524fd2).
    const std::string sounds = "Sounds=Array\n{\nItem Count=4\nSounds=A.wav\nSounds=B.wav\nSounds=C.wav\n"
                               "Sounds=D.wav\n}\n";
    const TextObject chance = objectOf(system(sounds, "CFloatParticleAI\n{\n}"));
    ParticleSystem c(chance, 3);
    firstBurst(c);
    CHECK(c.burstSound(6) == 2);
    const TextObject sized = objectOf(system(sounds + "Choose Sound Based On Burst=1\n", "CFloatParticleAI\n{\n}"));
    ParticleSystem z(sized, 3);
    firstBurst(z);
    CHECK(z.burstSound(6) == 3);
}

void testMotions() {
    // Falling by its Gravity from 50 up, it lands and bounces up by its
    // model's Bounce Factor.
    const TextObject fall = objectOf(system("Height=50\n", "CGravityParticleAI\n{\nGravity=10\n}", 1, 10.0f, 0.0f));
    ParticleSystem g(fall, 5);
    g.kind(0).bounceFactor = 0.5f;
    firstBurst(g);
    float lowest = 1e9f;
    bool bounced = false;
    for (int i = 0; i < 2000 && !g.particles().empty(); ++i) {
        g.update(0.001f);
        const ParticleSystem::Particle &p = g.particles()[0];
        lowest = std::min(lowest, p.z);
        bounced = bounced || (p.z < 1.0f && p.vz > 0.0f);
    }
    CHECK(lowest >= 0.0f && lowest < 1.0f && bounced);
    // In free fall, 207 pixels a second each second for a Gravity of 10.
    const TextObject drop = objectOf(system("Height=1000\n", "CGravityParticleAI\n{\nGravity=10\n}", 1, 10.0f, 0.0f));
    ParticleSystem d(drop, 5);
    firstBurst(d);
    for (int i = 0; i < 100; ++i)
        d.update(0.01f);
    CHECK(std::abs(d.particles()[0].vz + 207.49f) < 1.0f);
    // A swirl keeps its distance from the emitter point as it turns.
    const TextObject swirl =
        objectOf(system("Start Point Type=CDiskParticleStartPositionType\n{\n" + envelope("Inside Radius", 20, 0, 0) +
                            envelope("Outside Radius", 0, 0, 0) + "YX Ratio=1\n}\n",
                        "CSwirlParticleAI\n{\nAngular Velocity=2\n}", 1, 10.0f, 0.0f));
    ParticleSystem w(swirl, 9);
    firstBurst(w);
    const ParticleSystem::Particle first = w.particles()[0];
    for (int i = 0; i < 50; ++i)
        w.update(0.01f);
    const ParticleSystem::Particle &now = w.particles()[0];
    CHECK(std::abs(std::hypot(first.u, first.v) - 20.0f) < 0.01f);
    CHECK(std::abs(std::hypot(now.u, now.v) - 20.0f) < 0.01f);
    CHECK(std::abs(std::atan2(now.v, now.u) - std::atan2(first.v, first.u)) > 0.5f);
}

void testShapes() {
    // A cylinder up the axis (straight up): each on a ring of 10 to 15
    // about the emitter point, from the ground to 54 up.
    const TextObject o = objectOf(system("Start Point Type=CCylinderParticleStartPositionType\n{\n" +
                                             envelope("Inside Radius", 10, 0, 0) + envelope("Outside Radius", 5, 0, 0) +
                                             envelope("Height of Cylinder", 54, 0, 0) + "YX Ratio=1\n}\n",
                                         "CFloatParticleAI\n{\n}", 50, 1.0f, 0.0f));
    ParticleSystem s(o, 11);
    firstBurst(s);
    CHECK(s.particles().size() == 50);
    for (const ParticleSystem::Particle &p : s.particles()) {
        const float r = std::hypot(p.u, p.v);
        CHECK(r >= 9.99f && r <= 15.01f && p.z >= -0.01f && p.z <= 54.01f);
    }
    // A line, its Center Length either way across the axis.
    const TextObject line = objectOf(
        system("Start Point Type=CLineParticleStartPositionType\n{\n" + envelope("Center Length", 60, 0, 0) + "}\n",
               "CFloatParticleAI\n{\n}", 50, 1.0f, 0.0f));
    ParticleSystem l(line, 13);
    firstBurst(l);
    float widest = 0.0f;
    for (const ParticleSystem::Particle &p : l.particles())
        widest = std::max(widest, std::hypot(p.u, p.v, p.z));
    CHECK(widest <= 60.01f && widest > 30.0f);
}

void testOnMap() {
    // A relay whose particle system starts off; a message turns it on. An
    // action makes an entity with a system of its own, deleted a second on.
    const std::string off = system("Num Bursts To Stop After=1\n", "CFloatParticleAI\n{\n}", 3, 10.0f);
    std::string disabled = off;
    disabled.replace(disabled.find("Enabled=1"), 9, "Enabled=0");
    const std::string zax = "CLayerSaveData\n{\nWidth=1000\nHeight=1000\nView Position=500.000000,500.000000\n"
                            "Tree List=CSortList2D\n{\nLevel Part=CEntityBase\n{\nName=Sparks\nVisible=1\nActive=1\n"
                            "Activity=Array\n{\nItem Count=1\nActivity=" +
                            disabled + "}\nModel=Editor/Relay\nPosition X=300\nPosition Y=300\n}\n}\n}\n";
    MemoryArchive data;
    data.files["Levels/Test.zax"] = std::vector<uint8_t>(zax.begin(), zax.end());
    GameMap map;
    CHECK(!loadMap(data, "Levels/Test.zax", map));
    TextObject rules = objectOf("CRules\n{\nCustom Rules=CCustomRules\n{\nScreen Pos=0,0\nScreen Size=200,200\n}\n}\n");
    GameOptions options;
    MapScreen screen(data, rules, map, options, 0);
    uint64_t now = 0;
    const auto run = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 20)
            screen.update(now += 20);
    };
    run(1000);
    CHECK(screen.emitters().size() == 1 && screen.emitters()[0].system.particles().empty());
    TextObject on = objectOf("CSendMessageAction\n{\nMessage=particle system on\nTarget=Sparks\n}\n");
    screen.runAction(&on);
    run(1000);
    CHECK(screen.emitters()[0].system.particles().size() == 3 && !screen.emitters()[0].system.enabled());
    const TextObject make =
        objectOf("CCreateCustomEntityAction\n{\nNew Location=Sparks\nEntity=CEntityBase\n{\nName=Em Pulse\nVisible=0\n"
                 "Active=1\nActivity=Array\n{\nItem Count=2\nActivity=" +
                 system("", "CFloatParticleAI\n{\n}") +
                 "Activity=CActionAI\n{\nAction=CDelayAction\n{\nDelay=1\nNext Action=CDeleteAction\n{\n"
                 "Target Name=$trigger\n}\n}\n}\n}\n}\n}\n");
    screen.runAction(&make);
    CHECK(screen.emitters().size() == 2);
    const size_t part = screen.emitters()[1].part;
    CHECK(screen.map().parts[part].name == "Em Pulse" && screen.map().parts[part].x == 300.0f);
    run(500);
    CHECK(!screen.emitters()[1].system.particles().empty() && !screen.partState(part).deleted);
    run(1000);
    CHECK(screen.partState(part).deleted);
}

} // namespace

static const reluva::test::Suite kSuite("stawayteam.unit.particles",
                                        {
                                            {"envelopes", [](const std::filesystem::path &) { testEnvelopes(); }},
                                            {"bursts", [](const std::filesystem::path &) { testBursts(); }},
                                            {"motions", [](const std::filesystem::path &) { testMotions(); }},
                                            {"shapes", [](const std::filesystem::path &) { testShapes(); }},
                                            {"on a map", [](const std::filesystem::path &) { testOnMap(); }},
                                        });
