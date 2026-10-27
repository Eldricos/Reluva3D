// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's characters outdoors: the footprints they leave in the
// snow (MODL footprints, in OUTD scenes: 0x43fda0, 0x43fd00) and their
// breath in the cold (MODL breath: a BRTH particle system at the upper
// lip, 0x425f50). See docs/formats/awe/characters.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

namespace {

// A footprint lasts 5 seconds (0x43ff73): faded in over the first half
// second, out over the rest (0x43fd00).
constexpr float kFootprintLife = 5;

// The stride: the right foot down 0.45 s in, the left at 0.9 s, again
// from 1.16666 s (0x43fda0).
constexpr float kRightFoot = 0.45f, kLeftFoot = 0.9f, kStride = 1.16666f;

// The breath's particles, as BRTH's constructor (0x406840) sets them over
// the particles' own defaults: one at a time every 0.3 s, thrown with no
// upward speed or gravity, 100 units wide, living 1.5 s with 0.75 s
// fades, blended; on for 2 s and off for 2 s, a breath at a time.
ParticleSettings breathSettings() {
    ParticleSettings s;
    s.texture = "Elements\\Particles\\breath01.tga";
    s.alphaBlend = true;
    s.velocity[0] = 50, s.velocity[1] = 0, s.velocity[2] = 50;
    s.burst = 1;
    s.interval = 0.3f;
    s.copies = 1;
    s.spread = 1;
    s.bounce = 0;
    s.size = 100;
    s.life = 1.5f;
    s.mode = 0;
    s.blink = true;
    s.onTime = 2, s.offTime = 2;
    s.sway = 15, s.swaySpeed = 0;
    s.gravity = 0;
    s.fadeIn = 0.75f, s.fadeOut = 0.75f;
    return s;
}

// Where a bone of the character is in the world, now; false if it has no
// such bone (0x42aae0: the world pose's translation).
bool bonePosition(const Model& model, const Pose& pose, const gfx::Mat4& placement, const char* name, float out[3]) {
    for (size_t b = 0; b < model.bones.size() && b < pose.world.size(); b++) {
        if (fs::toLower(model.bones[b].name) != fs::toLower(name))
            continue;
        const gfx::Mat4 m = gfx::multiply(placement, pose.world[b]);
        out[0] = m[12], out[1] = m[13], out[2] = m[14];
        return true;
    }
    return false;
}

} // namespace

void ScenePage::updateOutdoors(Character& c, float dt, bool walking) {
    const Ac2Record* modl = c.record ? c.record->part("MODL") : nullptr;
    if (!modl || !c.model || !c.actor)
        return;
    Pose pose;
    bool posed = false;
    auto posedNow = [&]() -> const Pose& {
        if (!posed)
            c.actor->animator().pose(pose), posed = true;
        return pose;
    };
    // Footprints, only in a scene marked outdoors (OUTD, 0x43ffcd).
    bool outdoors = false;
    for (const Ac2Record& r : scene_.records)
        if (r.tag == "OUTD")
            outdoors = r.i("value") != 0;
    if (modl->i("footprints") && outdoors) {
        for (Character::Footprint& f : c.footprints)
            f.age += dt;
        c.footprints.erase(std::remove_if(c.footprints.begin(), c.footprints.end(),
                                          [](const Character::Footprint& f) { return f.age >= kFootprintLife; }),
                           c.footprints.end());
        if (walking) {
            // A foot down at its time in the stride: where its bone is, on
            // the floor, turned as the character (0x43fda0). Snowshoe
            // prints once they're on (the tag snowshoeson).
            c.stride += dt;
            bool down = (c.stride >= kRightFoot && c.foot == 0) || (c.stride >= kLeftFoot && c.foot == 1);
            if (c.stride >= kStride && c.foot == 2)
                c.stride -= kStride, c.foot = 0;
            float at[3];
            if (down && bonePosition(c.model->model(), posedNow(), c.placement,
                                     c.foot == 0 ? "Bip01 R Foot" : "Bip01 L Foot", at)) {
                Character::Footprint f;
                f.model = propModel(state_.tags.count("snowshoeson") ? "models/footprint/snowprint_model.gr2"
                                                                     : "models/footprint/footprint_model.gr2");
                f.x = at[0], f.z = at[2], f.rotY = c.rotY;
                f.serial = footprintSerial_++;
                if (f.model)
                    c.footprints.push_back(std::move(f));
            }
            if (down)
                c.foot++;
        }
    }
    // Breath: while it's set, from the upper lip, blown 50 units a second
    // along the way the character faces (0x425fa9).
    if (!modl->i("breath")) {
        c.breath.reset();
        return;
    }
    if (!c.breath)
        c.breath = std::make_unique<ParticleSystem>(breathSettings());
    float lip[3];
    if (bonePosition(c.model->model(), posedNow(), c.placement, "Upper Lip_R", lip)) {
        const float velocity[3] = {50 * std::sin(c.rotY), 0, 50 * std::cos(c.rotY)};
        c.breath->setEmitter(lip, velocity);
    }
    c.breath->update(dt);
}

void ScenePage::drawFootprint(const Character::Footprint& f, const SceneLighting& lighting, float brightness) {
    // A model like any other (FOOT, 0x40dda0: no shadow; the models'
    // ambient 0.5), its diffuse alpha its fade (+0x98), blended.
    if (!f.model)
        return;
    Material m;
    m.ambient = {0.5f, 0.5f, 0.5f};
    m.blend = true;
    m.alpha = f.age <= 0.5f ? f.age * 2 : (kFootprintLife - f.age) * 0.2222222f;
    const Pose pose = poseAt(f.model->model(), nullptr, 0);
    f.model->draw(pose, characterMatrix(f.x, 0, f.z, f.rotY), false, frameMeshes_, brightness, &lighting, m);
}

} // namespace reluva::awe
