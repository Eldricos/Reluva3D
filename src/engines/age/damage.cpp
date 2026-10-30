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
constexpr size_t kMostParticles = 600;
constexpr int kMostBlast = 64;    // particles a burst starts with, at most
constexpr float kSpewLongest = 3; // seconds a spew with no time limit goes on

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
    out.smoke = parseBirthRule(n);
    return Error::success();
}

BirthRule parseBirthRule(const TuneNode& n) {
    BirthRule s;
    auto vec = [&](const char* name, const Vec3& fallback = {}) {
        return Vec3{n.number(name, 0, fallback[0]), n.number(name, 1, fallback[1]), n.number(name, 2, fallback[2])};
    };
    s.positionVar = vec("PositionVar");
    s.velocity = vec("Velocity", s.velocity);
    s.velocityVar = vec("VelocityVar");
    s.life = n.number("Life", 0, s.life);
    s.lifeVar = n.number("LifeVar");
    s.radius = n.number("Radius", 0, s.radius);
    s.radiusVar = n.number("RadiusVar");
    s.dRadius = n.number("DRadius");
    s.drag = n.number("Drag");
    s.dAlpha = n.number("DAlpha");
    s.gravity = n.number("Gravity");
    s.initialBlast = int(n.number("InitialBlast"));
    s.spewRate = n.number("SpewRate");
    s.spewTime = n.number("SpewTimeLimit");
    s.frame = int(n.number("TexFrameStart"));
    s.frameEnd = std::max(s.frame, int(n.number("TexFrameEnd", 0, float(s.frame))));
    // A signed 32-bit ARGB (read whole: a float would lose its low bits).
    const TuneNode* color = n.child("Color");
    const uint32_t argb =
        color && !color->values.empty() ? uint32_t(std::strtoll(color->values[0].c_str(), nullptr, 10)) : 0xffffffffu;
    s.color = {float(argb >> 16 & 0xff) / 255, float(argb >> 8 & 0xff) / 255, float(argb & 0xff) / 255,
               float(argb >> 24) / 255};
    return s;
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
    if (actor.sim.impact > 0 && damage.knock(setup, actor.sim.impact) > 0) {
        actor.dent(actor.sim.impactAt, setup.textureRadius);
        ++dents;
        std::rotate(recentDents.rbegin(), recentDents.rbegin() + 1, recentDents.rend());
        recentDents[0] = actor.sim.impactAt;
    }
    damage.update(setup, dt);
    puff(dt, actor, smoke);
}

void CarWear::puff(float dt, const CarActor& actor, SmokePuffs* smoke) {
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

SmokePuffs::SmokePuffs(gfx::Renderer3D& renderer, TextureCache& textures) : renderer_(renderer), textures_(textures) {
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
    smoke_ = look("fxpt8");
}

SmokePuffs::Look SmokePuffs::look(const std::string& texture) {
    auto it = looks_.find(texture);
    if (it != looks_.end())
        return it->second;
    Look l;
    bool translucent;
    l.texture = textures_.get(texture, translucent, true);
    // The game's sheets of four are its 128-pixel particle textures.
    int w = 0, h = 0;
    l.grid = textures_.size(texture, w, h) && w >= 128 && h >= 128 ? 2 : 1;
    return looks_[texture] = l;
}

SmokePuffs::~SmokePuffs() {
    if (quad_)
        renderer_.destroyMesh(quad_);
}

void SmokePuffs::emit(const BirthRule& rule, const Vec3& at, const Vec3& carVelocity, const Look* look) {
    std::uniform_real_distribution<float> either(-1, 1);
    Puff p;
    p.position = at;
    p.velocity = carVelocity * 0.5f;
    for (size_t k = 0; k < 3; ++k) {
        p.position[k] += rule.positionVar[k] * either(random_);
        p.velocity[k] += rule.velocity[k] + rule.velocityVar[k] * either(random_);
    }
    p.life = std::max(0.1f, rule.life + rule.lifeVar * either(random_));
    p.radius = std::max(0.05f, rule.radius + rule.radiusVar * either(random_));
    p.grow = rule.dRadius * kFrameRate;
    p.gravity = rule.gravity;
    p.drag = std::max(0.0f, rule.drag);
    p.fade = rule.dAlpha * kFrameRate / 255;
    p.color = rule.color;
    // One of its frames (a coin, a newspaper...).
    p.frame = rule.frame + int(random_() % uint32_t(rule.frameEnd - rule.frame + 1));
    p.look = look ? *look : smoke_;
    if (puffs_.size() < kMostParticles)
        puffs_.push_back(p);
}

void SmokePuffs::burst(const BirthRule& rule, const Vec3& at, const Vec3& velocity, const Look& look) {
    for (int i = 0; i < std::max(1, std::min(rule.initialBlast, kMostBlast)); ++i)
        emit(rule, at, velocity, &look);
    if (rule.spewRate > 0)
        spews_.push_back({rule, at, velocity, look, rule.spewTime > 0 ? rule.spewTime : kSpewLongest, 0});
}

namespace {

// The materials' effect numbers (ptxindex) and tune/effects' rules: the
// numbers aren't named anywhere, so Reluva pairs them by what the
// materials are (grass throws grass, sand dirt, water splashes, cobbles
// and every road smoke) and draws each with the particle texture that
// looks like it.
struct WheelEffectName {
    int index;
    const char* rule;
    const char* texture;
};
constexpr WheelEffectName kWheelEffects[] = {
    {1, "dust", "fxpt8"},  {2, "grass", "fxpt4"},  {4, "smoke", "fxpt8"},
    {5, "dirt", "fxpt11"}, {6, "splash", "fxpt9"}, {7, "rock", "fxpt14"},
};
constexpr float kWheelRate = 8;       // particles a second from a wheel slipping fully
constexpr float kWheelSize = 0.5f;    // the rules' sizes are for clouds; a wheel's are smaller
constexpr float kWheelRolling = 1.5f; // m/s: a wheel at an effect slipping at 0 throws when rolling

} // namespace

void SmokePuffs::loadWheelEffects(const SearchSet& files) {
    for (const WheelEffectName& e : kWheelEffects) {
        WheelEffect& w = wheelEffects_[size_t(e.index)];
        std::vector<uint8_t> data;
        TuneNode root;
        if (files.read(std::string("tune/effects/") + e.rule + ".asBirthRule", data) ||
            parseTune(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), root) ||
            root.children.empty())
            continue;
        w.rule = parseBirthRule(root.children.front());
        w.rule.radius *= kWheelSize;
        w.rule.radiusVar *= kWheelSize;
        w.look = look(e.texture);
        // Its frames are of a sheet the game's own; one of this one's.
        const int frames = w.look.grid * w.look.grid;
        w.rule.frame = 0;
        w.rule.frameEnd = frames - 1;
        w.loaded = true;
    }
}

void SmokePuffs::wheels(float dt, const CarSim& car) {
    std::array<float, 8>& owed = wheelOwed_[&car];
    const float speed = length(car.velocity);
    for (size_t i = 0; i < car.wheels.size(); ++i) {
        const CarSim::Wheel& w = car.wheels[i];
        for (size_t k = 0; k < 2; ++k) {
            float& due = owed[i * 2 + k];
            const int index = w.contact ? w.surface.particles[k] : -1;
            if (index < 0 || size_t(index) >= wheelEffects_.size() || !wheelEffects_[size_t(index)].loaded) {
                due = 0;
                continue;
            }
            // Past its threshold, more the more it slips; at 0, whenever
            // it rolls (water).
            const float threshold = w.surface.particleSlip[k];
            const float share = threshold > 0 ? std::clamp((w.slip - threshold) / (1 - threshold), 0.0f, 1.0f)
                                              : std::min(1.0f, speed / 10) * (speed > kWheelRolling);
            due += dt * kWheelRate * share;
            const WheelEffect& e = wheelEffects_[size_t(index)];
            std::uniform_real_distribution<float> either(-1, 1);
            for (; due >= 1; due -= 1) {
                // At the tyre's foot, across the surface's width and up its height.
                Vec3 at = car.wheelCenter(int(i));
                at[1] -= car.setup().wheelRadius[i] - w.surface.rise;
                at[0] += w.surface.spread * 0.5f * either(random_);
                at[2] += w.surface.spread * 0.5f * either(random_);
                emit(e.rule, at, car.velocity, &e.look);
            }
        }
    }
}

void SmokePuffs::update(float dt) {
    for (Spew& s : spews_) {
        s.left -= dt;
        for (s.owed += s.rule.spewRate * dt; s.owed >= 1; s.owed -= 1)
            emit(s.rule, s.at, s.velocity, &s.look);
    }
    spews_.erase(std::remove_if(spews_.begin(), spews_.end(), [](const Spew& s) { return s.left <= 0; }), spews_.end());
    for (Puff& p : puffs_) {
        p.age += dt;
        p.velocity[1] += p.gravity * dt;
        p.velocity = p.velocity * (1 / (1 + dt * (1 + p.drag))); // air drag, and the rule's
        p.position += p.velocity * dt;
        p.radius = std::max(0.01f, p.radius + p.grow * dt);
        p.color[3] = std::clamp(p.color[3] + p.fade * dt, 0.0f, 1.0f);
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(), [](const Puff& p) { return p.age >= p.life; }),
                 puffs_.end());
}

void SmokePuffs::draw(const Vec3& eye) const {
    if (!quad_)
        return;
    for (const Puff& p : puffs_) {
        if (!p.look.texture)
            continue;
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
        call.texture = p.look.texture;
        // The frame's part of the texture: across, then down.
        const int g = std::max(1, p.look.grid), f = p.frame % (g * g);
        const float part = 1.0f / float(g);
        call.postRow0 = {part, 0, part * float(f % g)};
        call.postRow1 = {0, part, part * float(f / g)};
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
