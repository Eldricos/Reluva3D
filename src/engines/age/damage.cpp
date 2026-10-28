// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The .vehCarDamage fields are read from the files; what they do is
// Reluva's reading. See docs/engines/age-driving.md.

#include "engines/age/damage.h"

#include "engines/age/tune.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::age {

namespace {

constexpr float kFrameRate = 30; // DRadius is taken as a frame's

} // namespace

Error parseDamageSetup(std::string_view text, DamageSetup& out) {
    out = DamageSetup{};
    TuneNode root;
    if (Error err = parseTune(text, root))
        return err;
    if (root.children.empty())
        return Error::fail("empty damage settings");
    const TuneNode& n = root.children.front();
    auto vec = [&](const char* name, const Vec3& fallback = {}) {
        return Vec3{n.number(name, 0, fallback[0]), n.number(name, 1, fallback[1]), n.number(name, 2, fallback[2])};
    };
    out.maxDamage = n.number("MaxDamage", 0, out.maxDamage);
    out.medDamage = n.number("MedDamage", 0, out.medDamage);
    out.impactThreshold = n.number("ImpactThreshold", 0, out.impactThreshold);
    out.regenerateRate = n.number("RegenerateRate");
    out.textureRadius = n.number("TextelDamageRadius", 0, out.textureRadius);
    out.smokeOffset = vec("SmokeOffset");
    out.smokeOffset2 = vec("SmokeOffset2", out.smokeOffset);
    SmokeRule& s = out.smoke;
    s.velocity = vec("Velocity", s.velocity);
    s.velocityVar = vec("VelocityVar");
    s.life = n.number("Life", 0, s.life);
    s.lifeVar = n.number("LifeVar");
    s.radius = n.number("Radius", 0, s.radius);
    s.radiusVar = n.number("RadiusVar");
    s.dRadius = n.number("DRadius");
    s.gravity = n.number("Gravity");
    s.frame = int(n.number("TexFrameStart"));
    // A signed 32-bit ARGB (read whole: a float would lose its low bits).
    const TuneNode* color = n.child("Color");
    const uint32_t argb =
        color && !color->values.empty() ? uint32_t(std::strtoll(color->values[0].c_str(), nullptr, 10)) : 0xffffffffu;
    s.color = {float(argb >> 16 & 0xff) / 255, float(argb >> 8 & 0xff) / 255, float(argb & 0xff) / 255,
               float(argb >> 24) / 255};
    return Error::success();
}

Error loadDamageSetup(const SearchSet& files, const std::string& car, DamageSetup& out) {
    std::vector<uint8_t> data;
    if (Error err = files.read("tune/vehicle/" + car + ".vehCarDamage", data)) {
        out = DamageSetup{};
        return err;
    }
    return parseDamageSetup(std::string(data.begin(), data.end()), out);
}

float CarDamage::knock(const DamageSetup& setup, float impulse) {
    if (impulse <= setup.impactThreshold)
        return 0;
    const float before = damage_;
    damage_ = std::min(setup.maxDamage, damage_ + impulse - setup.impactThreshold);
    return damage_ - before;
}

void CarDamage::update(const DamageSetup& setup, float dt) {
    damage_ = std::max(0.0f, damage_ - setup.regenerateRate * dt);
}

void CarWear::load(const SearchSet& files, const std::string& car, CarActor& actor) {
    loadDamageSetup(files, car, setup); // the defaults without
    mend(actor);
}

void CarWear::mend(CarActor& actor) {
    damage.reset();
    puffs = 0;
    actor.mend();
}

void CarWear::update(float dt, CarActor& actor, SmokePuffs* smoke) {
    if (actor.sim.impact > 0 && damage.knock(setup, actor.sim.impact) > 0)
        actor.dent(actor.sim.impactAt, setup.textureRadius);
    damage.update(setup, dt);
    if (!smoke || !damage.smoking(setup))
        return;
    // From 10 puffs a second at MedDamage to 30 wrecked, from each place.
    const float span = std::max(1.0f, setup.maxDamage - setup.medDamage);
    puffs += dt * (10 + 20 * std::min(1.0f, (damage.damage() - setup.medDamage) / span));
    const CarSim& sim = actor.sim;
    for (; puffs >= 1; puffs -= 1) {
        smoke->emit(setup.smoke, sim.position + sim.orientation * setup.smokeOffset, sim.velocity);
        if (length(setup.smokeOffset2 - setup.smokeOffset) > 0.01f)
            smoke->emit(setup.smoke, sim.position + sim.orientation * setup.smokeOffset2, sim.velocity);
    }
}

SmokePuffs::SmokePuffs(gfx::Renderer3D& renderer, TextureCache& textures) : renderer_(renderer) {
    std::vector<gfx::Vertex> v(4);
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for (size_t i = 0; i < 4; ++i) {
        v[i].pos[0] = corners[i][0];
        v[i].pos[1] = corners[i][1];
        v[i].uv[0] = corners[i][0] + 0.5f;
        v[i].uv[1] = 0.5f - corners[i][1];
        v[i].normal[2] = 1;
        v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
    }
    renderer.createMesh(v, {0, 1, 2, 0, 2, 3}, quad_);
    bool translucent;
    texture_ = textures.get("fxpt8", translucent, true);
}

SmokePuffs::~SmokePuffs() {
    if (quad_)
        renderer_.destroyMesh(quad_);
}

void SmokePuffs::emit(const SmokeRule& rule, const Vec3& at, const Vec3& carVelocity) {
    std::uniform_real_distribution<float> either(-1, 1);
    Puff p;
    p.position = at;
    p.velocity = carVelocity * 0.5f;
    for (size_t k = 0; k < 3; ++k)
        p.velocity[k] += rule.velocity[k] + rule.velocityVar[k] * either(random_);
    p.life = std::max(0.1f, rule.life + rule.lifeVar * either(random_));
    p.radius = std::max(0.05f, rule.radius + rule.radiusVar * either(random_));
    p.grow = rule.dRadius * kFrameRate;
    p.gravity = rule.gravity;
    p.color = rule.color;
    p.frame = rule.frame;
    if (puffs_.size() < 400)
        puffs_.push_back(p);
}

void SmokePuffs::update(float dt) {
    for (Puff& p : puffs_) {
        p.age += dt;
        p.velocity[1] += p.gravity * dt;
        p.velocity = p.velocity * (1 / (1 + dt)); // air drag
        p.position += p.velocity * dt;
        p.radius += p.grow * dt;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff& p) { return p.age >= p.life; }),
                 puffs_.end());
}

void SmokePuffs::draw(const Vec3& eye) const {
    if (!quad_ || !texture_)
        return;
    for (const Puff& p : puffs_) {
        Vec3 toEye = eye - p.position;
        const float d = length(toEye);
        if (d < 0.2f)
            continue;
        toEye = toEye * (1 / d);
        Vec3 right = cross(Vec3{0, 1, 0}, toEye);
        right = length(right) > 1e-3f ? normalized(right) : Vec3{1, 0, 0};
        const Vec3 up = cross(toEye, right);
        const float size = p.radius * 2;
        const Vec3 x = right * size, y = up * size;
        gfx::DrawCall call;
        call.mesh = quad_;
        call.indexCount = 6;
        call.model = {x[0],     x[1],     x[2],     0, y[0],          y[1],          y[2],          0,
                      toEye[0], toEye[1], toEye[2], 0, p.position[0], p.position[1], p.position[2], 1};
        call.texture = texture_;
        // The frame's quarter of the texture: across, then down.
        const int f = p.frame & 3;
        call.postRow0 = {0.5f, 0, 0.5f * float(f % 2)};
        call.postRow1 = {0, 0.5f, 0.5f * float(f / 2)};
        call.color = p.color;
        call.color[3] *= 1 - p.age / p.life; // fading out over its life
        call.state.src = gfx::Blend::SrcAlpha;
        call.state.dst = gfx::Blend::OneMinusSrcAlpha;
        call.state.depthWrite = false;
        call.state.cull = gfx::Cull::None;
        renderer_.draw(call);
    }
}

} // namespace reluva::age
