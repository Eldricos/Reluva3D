// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/particles.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

constexpr float kTurn = 6.283184714f; // 0x54b6b0: sin and cos take turns

// The templates' values, read from nfs3.exe at 0x560b64 + kind × 128
// (static data; kinds 1-13, 0 is all zero): count, life, throttle, up,
// along, across, spread, the push's turning, size and growth, gravity,
// spin, inherit, drag, size, growth, size spread, colour, fade, flags,
// streak, reach, speed scale, spin held, random colour.
const ParticleTemplate kTemplates[14] = {
    {},
    {1, 64, 0, 3.0f, 2.5f, 3.5f, 0.2f, 0.05f, 0.025f, 0.0234375f, -0.3125f, 0.1f, 0.5f, 0.9f, 0.7f, -0.5f, 0.5f,
     0x80ffffff, 0x20000000, 0, false, 90.0f, 30.0f, false, 0},
    {1, 128, 1, 3.1f, 2.0f, 3.5f, 0.5f, 0.05f, 0.05f, 0.046875f, 0.0f, 0.5f, 0.5f, 0.8f, 0.75f, 3.0f, 0.5f,
     0x7fdfdfdf, 0x7f000000, 0, false, 150.0f, 30.0f, false, 0},
    {30, 16, 0, 2.0f, 5.0f, 2.0f, 0.5f, 0.02f, 0.05f, 0.03125f, -0.25f, 0.0f, 0.5f, 1.0f, 0.01f, 0.0f, 0.0f,
     0xffffffff, 0xff00c6ff, 1, true, 50.0f, 30.0f, false, 0},
    {5, 64, 0, 3.5f, 2.5f, 3.5f, 1.0f, 0.05f, 0.025f, 0.0234375f, -0.1875f, 0.4f, 0.1f, 0.95f, 0.3f, 0.1f, 0.5f,
     0xffffffff, 0x00000000, 0, false, 100.0f, 30.0f, false, 0},
    {1, 64, 0, 3.5f, 2.5f, 3.5f, 0.2f, 0.05f, 0.025f, 0.0234375f, -0.15625f, 0.25f, 0.3f, 0.9f, 0.5f, 0.25f, 0.1f,
     0xe4ffffff, 0x00000000, 0, false, 90.0f, 30.0f, false, 0},
    {1, 256, 3, 1.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.5f, 0.95f, 0.75f, 2.5f, 0.5f,
     0x7fffffff, 0x7f404040, 0, false, 150.0f, 0.0f, false, 0},
    {1, 8, 0, 5.0f, 2.0f, 3.5f, 0.5f, 0.1f, 0.05f, 0.046875f, 0.0f, 0.3f, 0.5f, 1.0f, 0.5f, 2.0f, 0.5f,
     0xffffffff, 0xff007fff, 1, false, 150.0f, 0.0f, false, 0},
    {1, 56, 2, 5.0f, 2.0f, 3.5f, 0.5f, 0.1f, 0.05f, 0.046875f, 0.0f, 0.2f, 0.5f, 0.95f, 0.5f, 3.0f, 0.5f,
     0xffffffff, 0xff007f7f, 1, false, 120.0f, 0.0f, false, 0},
    {1, 64, 2, 5.0f, 2.0f, 3.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f, 0.5f, 0.95f, 0.5f, 3.0f, 0.5f,
     0xffffffff, 0xff000000, 0, false, 200.0f, 0.0f, false, 0},
    {1, 32, 1, 3.1f, 2.0f, 3.5f, 0.5f, 0.06f, 0.05f, 0.053125f, -0.15625f, 0.5f, 0.9f, 0.9f, 0.75f, 4.0f, 0.5f,
     0x7fffffff, 0x7f000000, 0, false, 120.0f, 50.0f, false, 0},
    {1, 64, 0, 3.0f, 2.5f, 3.5f, 0.2f, 0.05f, 0.025f, 0.0234375f, -0.15625f, 0.2f, 0.5f, 0.8f, 0.7f, -0.5f, 0.5f,
     0xffffffff, 0xff000000, 0, false, 100.0f, 30.0f, false, 0},
    {2, 128, 2, 2.1f, 0.0f, 2.5f, 2.0f, 0.2f, 0.05f, 0.015625f, -0.05625f, 0.1f, 1.0f, 0.95f, 0.25f, 0.0f, 0.25f,
     0xffffffff, 0x00000000, 0, false, 90.0f, 15.0f, true, 0x00007fff},
    {3, 128, 0, 4.1f, 0.0f, 5.5f, 2.0f, 0.2f, 0.05f, 0.015625f, -0.30625f, 0.1f, 1.0f, 0.95f, 0.6f, 0.0f, 0.25f,
     0xffffffff, 0x00000000, 0, false, 90.0f, 0.0f, false, 0},
};
// Kind 7 under Direct3D: 0x4cb670 writes the bytes 0x560f30-0x560f37
// (its +0x4c and +0x50).
const ParticleTemplate kFireDirect3d = [] {
    ParticleTemplate t = kTemplates[7];
    t.colour = 0x7f7f7f7f;
    t.fade = 0x7f003f7f;
    return t;
}();
const ParticleTemplate& kSparks = kTemplates[3];

// The x87's rounding (fistp), to nearest, halves to even.
int fistp(float v) { return static_cast<int>(std::nearbyint(v)); }

ParticleVec normalized(const ParticleVec& v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l == 0)
        return v;
    return {v[0] / l, v[1] / l, v[2] / l};
}

ParticleVec offset(const ParticleVec& at, const ParticleVec& right, const ParticleVec& up, float x, float y) {
    // x right, y down, as on the screen.
    return {at[0] + right[0] * x - up[0] * y, at[1] + right[1] * x - up[1] * y, at[2] + right[2] * x - up[2] * y};
}

} // namespace

const ParticleTemplate* particleTemplate(int kind, bool direct3d) {
    if (kind == 7 && direct3d)
        return &kFireDirect3d;
    return kind >= 1 && kind < 14 ? &kTemplates[kind] : nullptr;
}

int wallParticleKind(int code) {
    static constexpr int kKinds[8] = {0, 3, 3, 2, 3, 2, 0, 2}; // 0x553740
    return code >= 0 && code < 8 ? kKinds[code] : 0;
}

uint32_t ParticleRandom::next() {
    if (!script.empty()) {
        const uint32_t v = script.front();
        script.erase(script.begin());
        return v;
    }
    seed = seed * 1664525u + 1013904223u;
    return seed ^ (seed >> 15);
}

Particle& ParticleSystem::slot() {
    Particle* least = nullptr;
    for (Particle& p : slots_) {
        if (p.life == 0)
            return p;
        if (!least || p.life <= least->life)
            least = &p;
    }
    return *least;
}

int ParticleSystem::live() const {
    return static_cast<int>(std::count_if(slots_.begin(), slots_.end(), [](const Particle& p) { return p.life != 0; }));
}

Particle* ParticleSystem::spawn(int kind, const ParticleVec& at, float scale, const ParticleVec& a,
                                const ParticleVec& b, const ParticleVec& c, uint32_t tick, ParticleRandom& random,
                                int horizon) {
    const ParticleTemplate* t = particleTemplate(kind, direct3d);
    if (!t)
        return nullptr;
    Particle& p = slot();
    // The throttle and the Horizon option: none at Off, every other one
    // of its turns at Low.
    const uint32_t half = tick >> 1;
    const bool halved = half % static_cast<uint32_t>(2 * t->throttle + 2) == 0;
    if (horizon == 2 || (horizon == 1 && halved))
        return nullptr;
    if (half % static_cast<uint32_t>(t->throttle + 1) != 0)
        return nullptr;
    p = Particle{};
    p.kind = kind;
    p.life = p.lifeMax = t->life;
    p.halved = halved;
    p.position = at;
    // Each speed × 1 ± spread/2, in this order.
    const float up = ((random.unit() - 0.5f) * t->spread + 1) * t->up;
    const float along = ((random.unit() - 0.5f) * t->spread + 1) * t->along;
    const float across = ((random.unit() - 0.5f) * t->spread + 1) * t->across;
    for (int i = 0; i < 3; ++i)
        p.velocity[i] = scale * along * a[i] + scale * across * b[i] + t->inherit * c[i];
    p.velocity[1] += scale * up;
    p.size = p.baseSize = t->size + random.unit() * t->size * t->sizeSpread;
    p.growth = t->growth * scale;
    p.angle = random.unit();
    p.spin = t->spin * scale;
    p.wobbleAngle = random.unit();
    p.wobbleTurn = t->wobbleTurn;
    p.wobble = t->wobble;
    p.wobbleGrowth = t->wobbleGrowth;
    p.colour = t->colour;
    // Less a random part of +0x54's bytes (kind 12's 0x7FFF), alpha
    // first (0x4caa8a-0x4cab04).
    if (t->colourRandom)
        for (int shift = 24; shift >= 0; shift -= 8) {
            const int part = fistp(random.unit() * static_cast<float>((t->colourRandom >> shift) & 0xff));
            const uint32_t byte = ((p.colour >> shift) - static_cast<uint32_t>(part)) & 0xff;
            p.colour = (p.colour & ~(0xffu << shift)) | byte << shift;
        }
    // Not moving at all: not made.
    if (p.velocity[0] == 0 && p.velocity[1] == 0 && p.velocity[2] == 0)
        p.life = 0;
    return &p;
}

void ParticleSystem::spawnOne(int kind, const ParticleVec& at, uint32_t tick, ParticleRandom& random, int horizon) {
    const float sign = random.next() & 1 ? 1.0f : -1.0f;
    if (kind == 0)
        return;
    if (Particle* p = spawn(kind, at, 1, {}, {}, {}, tick, random, horizon))
        p->spin *= sign;
}

int burstCount(float speed) {
    const int count = kSparks.count;
    if (speed >= 20)
        return count;
    return fistp(static_cast<float>(count) * speed / 20.0f);
}

void ParticleSystem::burst(const ParticleVec& at, const ParticleVec& carPosition, const ParticleVec& carVelocity,
                           float speed, uint32_t tick, ParticleRandom& random, int horizon) {
    const int count = burstCount(speed);
    const ParticleVec along = normalized(carVelocity);
    const ParticleVec across =
        normalized({at[0] - carPosition[0], at[1] - carPosition[1], at[2] - carPosition[2]});
    for (int i = 0; i < count; ++i) {
        ParticleVec thrown;
        for (int k = 0; k < 3; ++k)
            thrown[k] = static_cast<float>(random.unit() * 6.0 + carVelocity[k] - 3.0);
        const float scale = random.unit() * 0.25f + 0.75f;
        Particle* p = spawn(3, at, scale, along, across, thrown, tick, random, horizon);
        if (!p)
            continue;
        // A shorter life, by up to half, and 0-2 more sparks when it dies.
        const uint32_t half = static_cast<uint32_t>(p->life >> 1);
        p->life -= half ? static_cast<int>(random.next() % half) : 0;
        p->children = static_cast<int>(random.next() % 3);
    }
}

uint32_t particleColour(const ParticleTemplate& t, float part) {
    uint32_t out = 0;
    for (int i = 0; i < 4; ++i) {
        const int from = static_cast<int>((t.colour >> (8 * i)) & 0xff);
        const int off = fistp(static_cast<float>((t.fade >> (8 * i)) & 0xff) * part);
        out |= static_cast<uint32_t>((from - off) & 0xff) << (8 * i);
    }
    return out;
}

void ParticleSystem::step(uint32_t tick, ParticleRandom& random, int horizon) {
    const bool odd = tick & 1;
    for (Particle& p : slots_) {
        if (p.life == 0)
            continue;
        const ParticleTemplate* t = particleTemplate(p.kind, direct3d);
        if (odd && t) {
            const float drag = t->drag;
            const float part = static_cast<float>(p.lifeMax - p.life) / static_cast<float>(p.lifeMax);
            if (p.wobbleTurn != 0) {
                p.velocity[0] += std::sin(p.wobbleAngle * kTurn) * p.wobble;
                p.velocity[2] += std::cos(p.wobbleAngle * kTurn) * p.wobble;
                p.wobbleGrowth *= drag;
                p.wobbleTurn *= drag;
                p.wobble += p.wobbleGrowth;
                p.wobbleAngle += p.wobbleTurn;
            }
            if (p.spin != 0) {
                // +0x3c set (kind 12): the spin turned back towards 0 once
                // the angle is past ±0.25 of a turn (0x4cb556).
                if (t->spinHeld) {
                    const float s = std::fabs(p.spin);
                    if (p.angle > 0.25f)
                        p.spin = -s;
                    else if (p.angle < -0.25f)
                        p.spin = s;
                }
                p.angle += p.spin;
                p.spin *= drag;
            }
            p.size = p.baseSize + p.growth * part;
            if ((tick & 2) && t->fade)
                p.colour = particleColour(*t, part);
            p.velocity[0] *= drag;
            p.velocity[2] *= drag;
            p.velocity[1] = (p.velocity[1] + t->gravity) * drag;
            --p.life;
            if ((p.colour >> 24) == 0)
                p.life = 0;
            if (p.children && p.life == 0) {
                // 0x4cab60: its kind again, along its velocity, at half life.
                const Particle parent = p;
                const ParticleVec along = normalized(parent.velocity);
                for (int i = 0; i < parent.children; ++i) {
                    const float scale = random.unit() * 0.75f + 0.25f;
                    if (Particle* child =
                            spawn(parent.kind, parent.position, scale, along, {}, parent.velocity, tick, random, horizon)) {
                        child->children = 0;
                        child->life >>= 1;
                    }
                }
            }
        }
        for (int i = 0; i < 3; ++i)
            p.position[i] += p.velocity[i] * 0.015625f;
    }
}

void wallHitParticles(ParticleSystem& system, const WallHit& hit, uint32_t tick, ParticleRandom& random,
                      int horizon) {
    const int kind = wallParticleKind(hit.code);
    if (hit.impact > 5)
        system.spawnOne(kind, hit.contact, tick, random, horizon);
    if (hit.impact == 0)
        return;
    if (hit.speed >= 22) {
        if (hit.rough >= 3)
            system.spawnOne(2, hit.contact, tick, random, horizon);
        else if (kind == 3)
            system.burst(hit.contact, hit.carPosition, hit.carVelocity, hit.speed * 0.5f, tick, random, horizon);
        else
            system.spawnOne(kind, hit.contact, tick, random, horizon);
    } else if (hit.speed > 15 || hit.impact > 15) {
        system.spawnOne(6, hit.contact, tick, random, horizon);
    }
}

uint32_t dimByIntensity(uint32_t colour, float intensity) {
    if (!(intensity < 1))
        return colour;
    const uint32_t k = static_cast<uint32_t>(std::min(fistp(intensity * 65536.0f), 0xffff)) >> 8 & 0xff;
    return (((colour & 0xff00ff00u) >> 8) * k & 0xff00ff00u) | ((colour & 0x00ff00ffu) * k >> 8 & 0x00ff00ffu);
}

uint32_t dimParticle(uint32_t colour, float intensity) {
    if (!(intensity < 1))
        return colour;
    const uint32_t k = static_cast<uint32_t>(fistp(intensity * 65536.0f));
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8)
        out |= ((((colour >> shift) & 0xff) * k >> 16) & 0xff) << shift;
    return out;
}

const SurfaceEffect& surfaceEffect(int surface, int weather) {
    // 0x553080, read from nfs3.exe: level, speed, range, kind, smoke,
    // landing kind for surfaces 0-15, dry; rain and snow (weathers 1 and
    // 2) have the same kinds and speeds.
    static const SurfaceEffect kDry[16] = {
        {0, 0, 0, 0, false, 0},  {0, 0, 0, 0, true, 3},   {0, 5, 20, 4, false, 2},  {0, 0, 0, 5, false, 2},
        {10, 5, 20, 12, true, 3}, {0, 3, 20, 2, false, 2}, {0, 5, 20, 10, false, 0}, {0, 0, 0, 0, true, 3},
        {0, 0, 0, 0, false, 1},  {0, 8, 20, 1, false, 1},  {0, 0, 0, 0, true, 3},   {10, 5, 20, 12, true, 3},
        {0, 0, 0, 0, true, 3},   {0, 3, 20, 2, false, 2},  {0, 0, 0, 0, false, 0},  {0, 8, 20, 1, false, 1},
    };
    static const SurfaceEffect kWet[16] = {
        {0, 0, 0, 0, false, 0},   {0, 5, 20, 10, false, 3}, {0, 5, 20, 11, false, 11}, {0, 0, 0, 11, false, 11},
        {0, 5, 20, 10, false, 3}, {0, 0, 0, 11, false, 11}, {0, 5, 20, 10, false, 0},  {0, 5, 20, 10, false, 3},
        {0, 0, 0, 0, false, 1},   {0, 8, 20, 1, false, 1},  {0, 5, 20, 10, false, 3},  {0, 5, 20, 10, false, 3},
        {0, 5, 20, 10, false, 3}, {0, 0, 0, 11, false, 11}, {0, 0, 0, 0, false, 0},    {0, 8, 20, 1, false, 1},
    };
    return (weather == 1 || weather == 2 ? kWet : kDry)[surface & 15];
}

void wheelParticles(ParticleSystem& system, int kind, int wheel, const ParticleVec& at, const ParticleVec& velocity,
                    const ParticleVec& right, float speed, bool overspeed, uint32_t tick, ParticleRandom& random,
                    int horizon, bool software) {
    const ParticleTemplate* t = particleTemplate(kind, system.direct3d);
    if (!t || (kind == 10 && software))
        return;
    float scale = 1;
    if (t->speedScale != 0) {
        float s = std::fabs(speed) / t->speedScale;
        if (kind != 12 && overspeed && wheel > 1)
            s += 0.5f; // 0x5403f8
        scale = std::min(s, 1.0f);
    }
    // 0x4972f0: the velocity made a unit vector, left at 0 when it is 0.
    ParticleVec along = velocity;
    const float length = std::sqrt(along[2] * along[2] + along[0] * along[0] + along[1] * along[1]);
    if (length != 0)
        for (float& v : along)
            v *= 1.0f / length;
    const float side = wheel == 0 || wheel == 3 ? -1.0f : 1.0f;
    const ParticleVec across{right[0] * side, right[1] * side, right[2] * side};
    const ParticleVec half{velocity[0] * 0.5f, velocity[1] * 0.5f, velocity[2] * 0.5f}; // 0x540408
    for (int i = 0; i < t->count; ++i)
        if (Particle* p = system.spawn(kind, at, scale, along, across, half, tick, random, horizon))
            p->spin *= side;
}

float wheelSpray(ParticleSystem& system, int wheel, float level, int surface, int weather, const ParticleVec& at,
                 const ParticleVec& velocity, const ParticleVec& right, float speed, uint32_t tick,
                 ParticleRandom& random, int horizon, bool software) {
    const SurfaceEffect& row = surfaceEffect(surface, weather);
    if (row.kind == 0)
        return level;
    if (level > row.level) {
        wheelParticles(system, row.kind, wheel, at, velocity, right, speed, false, tick, random, horizon, software);
        return level;
    }
    if (row.speed == 0 || row.range == 0 || speed <= row.speed)
        return level;
    wheelParticles(system, row.kind, wheel, at, velocity, right, speed, false, tick, random, horizon, software);
    return std::clamp((speed - row.speed) / row.range * 5.0f, 0.0f, 5.0f); // 0x537268
}

int soundDistance(const ParticleVec& d) {
    int x = std::abs(fistp(d[0])), y = std::abs(fistp(d[1])), z = std::abs(fistp(d[2]));
    int flat = z < x ? x + (z >> 2) : z + (x >> 2);
    return y < flat ? flat + (y >> 2) : y + (flat >> 2);
}

void carHitParticles(ParticleSystem& system, const ParticleVec& point, const ParticleVec& carPosition,
                     const ParticleVec& carVelocity, float strength, uint32_t tick, ParticleRandom& random,
                     int horizon) {
    if (!(strength > 5)) // 0x42ac9d: the strength's bits over 5.0's
        return;
    system.burst(point, carPosition, carVelocity, strength * 2.0f, tick, random, horizon);
    system.spawnOne(2, point, tick, random, horizon);
}

bool startCarSmoke(CarSmoke& smoke, float strength, bool traffic, const ParticleVec& point,
                   const ParticleVec& carPosition, const ParticleVec& right, const ParticleVec& up,
                   const ParticleVec& forward) {
    if (!(strength > 5) || !(traffic || strength > 30) || smoke.timer != 0)
        return false;
    smoke.timer += std::abs(fistp(strength * 0.15f));
    const ParticleVec d{point[0] - carPosition[0], point[1] - carPosition[1], point[2] - carPosition[2]};
    auto dot = [&](const ParticleVec& a) { return (a[0] * d[0] + a[1] * d[1] + a[2] * d[2]) * 0.6f; };
    smoke.point = {dot(right), dot(up), dot(forward)};
    return true;
}

void carFire(ParticleSystem& system, const ParticleVec& at, const ParticleVec& carPosition,
             const ParticleVec& carVelocity, uint32_t tick, ParticleRandom& random, int horizon) {
    system.spawn(9, at, 1, {}, {}, {}, tick, random, horizon);
    system.spawn(7, at, 1, {}, {}, {}, tick, random, horizon);
    if (tick % 48 < 8 && (random.next() & 1))
        system.burst(at, carPosition, carVelocity, 20.0f, tick, random, horizon);
    if (random.next() & 7) {
        const ParticleVec half{carVelocity[0] * 0.5f, carVelocity[1] * 0.5f, carVelocity[2] * 0.5f}; // 0x5403f0
        if (Particle* p = system.spawn(8, at, static_cast<float>(tick % 48) * (1.0f / 48.0f), {}, {}, half, tick,
                                       random, horizon))
            p->wobbleAngle = 0;
    }
}

void carSmokeTick(ParticleSystem& system, CarSmoke& smoke, const ParticleVec& carPosition, const ParticleVec& right,
                  const ParticleVec& up, const ParticleVec& forward, const ParticleVec& carVelocity, bool airborne,
                  float upright, float spinE4, float spinEC, uint32_t tick, ParticleRandom& random, int horizon) {
    if (smoke.timer > 0)
        ++smoke.timer;
    if (smoke.timer != 0) {
        ParticleVec at;
        for (size_t i = 0; i < 3; ++i)
            at[i] = smoke.point[0] * right[i] + smoke.point[1] * up[i] + smoke.point[2] * forward[i] + carPosition[i];
        if (smoke.timer <= 64)
            system.spawnOne(smoke.timer <= 16 ? 6 : 9, at, tick, random, horizon);
        else
            carFire(system, at, carPosition, carVelocity, tick, random, horizon);
    }
    if (smoke.timer != 0 && !airborne && upright >= 0.5f && std::fabs(spinE4) < 0.2f && std::fabs(spinEC) < 0.2f)
        smoke.timer = 0;
}

int carHitSoundSlot(int loudness, int& last, ParticleRandom& random) {
    int slot = loudness > 90 ? 33 : loudness > 70 ? 32 : loudness > 50 ? 37 : loudness > 25 ? 34 : 30;
    if (slot == last) {
        switch (last) {
        case 32: slot = random.next() & 1 ? 37 : 34; break;
        case 30: slot = 34; break;
        case 34: slot = random.next() & 1 ? 30 : 37; break;
        case 37: slot = random.next() & 1 ? 30 : 34; break;
        default: break;
        }
    }
    last = slot;
    return slot;
}

int hitSoundVolume(int distance) {
    const int64_t twice = 2 * static_cast<int64_t>(distance) * distance;
    return twice < 0x15f9 ? static_cast<int>((0x15f9 - twice) * 0x7f / 0x15f9) : 0;
}

int soundPan(uint32_t azimuth) {
    azimuth &= 0xffff;
    if (azimuth > 0x3fff && azimuth < 0xc000)
        return static_cast<int>((0xbfff - azimuth) >> 8);
    return static_cast<int>(((azimuth + 0x4000) & 0xffff) >> 8);
}

std::array<ParticleVec, 4> streakCorners(Particle& p, const ParticleVec& right, const ParticleVec& up, float pixel,
                                         float half) {
    const float w = std::clamp(half, 0.5f, 1.0f) * pixel;
    std::array<ParticleVec, 4> out;
    const ParticleVec& head = p.position;
    if (p.tailSet) {
        out[0] = out[1] = p.tail;
        out[2] = offset(head, right, up, w, w);
        out[3] = offset(head, right, up, -w, -w);
        for (int i = 0; i < 3; ++i)
            p.tail[i] = (head[i] + p.tail[i]) * 0.5f;
    } else {
        out[0] = head;
        out[1] = offset(head, right, up, w, -w);
        out[2] = offset(head, right, up, w, w);
        out[3] = offset(head, right, up, 0, w);
        p.tail = head;
        p.tailSet = true;
    }
    return out;
}

std::array<ParticleVec, 4> pictureCorners(const Particle& p, const ParticleVec& right, const ParticleVec& up,
                                          float half, float aspect) {
    // On the screen (0x4cc235): (-S, C), (C, S), (S, -C), (-C, -S) with
    // S = sin x the half size by the view's y scale (sy, negative: -330.33)
    // and C = cos x it by the x scale (sx, 320). Taken back to the world
    // the sine's part is the other way round to the screen's y, and
    // `aspect` (|sy| / sx) longer across and shorter up and down.
    const float s = std::sin(p.angle * kTurn) * half, c = std::cos(p.angle * kTurn) * half;
    return {offset(p.position, right, up, s * aspect, c / aspect), offset(p.position, right, up, c, -s),
            offset(p.position, right, up, -s * aspect, -c / aspect), offset(p.position, right, up, -c, s)};
}

} // namespace reluva::nfs

// ---------------------------------------------------------------------------
// Rain and snow (col.md, "Rain and snow").

namespace reluva::nfs {

namespace {

// 0x564290 + kind x 0x34, read from nfs3.exe (kind 0 is all zero).
const WeatherKind kRain = {0xC0000, 3.0f, 50, 25, 15, 5, 180, 3, 126, 16, 0.2f, 128, 100};
const WeatherKind kSnow = {0x20000, 6.0f, 100, 20, 20, 5, 0, 0, 200, 96, 0.3f, 128, 100};

constexpr double kFixed = 1.0 / 65536.0; // 0x5497b4, 0x5497c4

// The share of 800 or 20 0x4dcdf0 works out: ((percent << 16) / 100 x n)
// >> 16, rounded towards zero.
int percentOf(int percent, int n) {
    const int scaled = ((percent << 16) / 100) * n;
    return scaled / 65536;
}

uint32_t grey4(int g) {
    const uint32_t b = static_cast<uint32_t>(g) & 0xff;
    return b | b << 8 | b << 16 | b << 24;
}

// The quarter table of 0x5672ac: 65536 sin(k turns / 1024), k 0-256.
int sineTable(int k) {
    static const std::array<int, 257> table = [] {
        std::array<int, 257> t{};
        for (int i = 0; i <= 256; ++i)
            t[size_t(i)] = static_cast<int>(std::lround(65536.0 * std::sin(i * 6.283185307179586 / 1024.0)));
        return t;
    }();
    return table[size_t(k)];
}

// The table's sine of a 1024-step index, by quadrant.
int sineStep(int k) {
    k &= 1023;
    const int q = k >> 8, i = k & 255;
    switch (q) {
    case 0: return sineTable(i);
    case 1: return sineTable(256 - i);
    case 2: return -sineTable(i);
    default: return -sineTable(256 - i);
    }
}

} // namespace

int fixedSin(uint32_t angle) {
    // sin of the step, plus its cosine x the 64ths between steps x 2 pi.
    const int k = static_cast<int>((angle >> 6) & 1023);
    const int64_t frac = static_cast<int64_t>(angle & 0x3f) * 0x6487e >> 9;
    return sineStep(k) + static_cast<int>((static_cast<int64_t>(sineStep(k + 256) >> 2) * frac) >> 21);
}

int fixedCos(uint32_t angle) {
    const int k = static_cast<int>((angle >> 6) & 1023);
    const int64_t frac = static_cast<int64_t>(angle & 0x3f) * 0x6487e >> 9;
    return sineStep(k + 256) + static_cast<int>((static_cast<int64_t>(-(sineStep(k) >> 2)) * frac) >> 21);
}

const WeatherKind* weatherKind(int type) {
    switch (type % 3) {
    case 1: return &kRain;
    case 2: return &kSnow;
    default: return nullptr;
    }
}

WeatherSetup weatherSetup(int type, int startSlice, int endSlice, int fade, int stayOn, int fadeOff, int stayOff,
                          int fadeOn) {
    WeatherSetup s;
    const WeatherKind* k = weatherKind(type & 0xff); // a byte (0x47890d)
    if (!k)
        return s;
    s.kind = (type & 0xff) % 3;
    s.count = percentOf(k->countPercent, Weather::kDrops);
    s.splashes = percentOf(k->splashPercent, Weather::kSplashes);
    const int half = std::max(k->half, 1);
    s.half = static_cast<float>(half);
    s.top = static_cast<float>(k->top);
    s.bottom = static_cast<float>(-k->below);
    // The wind: (sin, 0, cos) of its heading in turns, x its speed; then
    // the fall on y.
    const float heading = static_cast<float>(static_cast<double>((k->windDegrees << 16) / 360) * kFixed);
    const float wind = static_cast<float>(static_cast<double>((k->windTenths << 16) / 10) * kFixed);
    s.velocity = {static_cast<float>(std::sin(heading * kTurn)) * wind, 0,
                  static_cast<float>(std::cos(heading * kTurn)) * wind};
    s.velocity[1] = -static_cast<float>(static_cast<double>(k->fall / 10) * kFixed);
    s.flake = k->flake;
    s.colour = grey4(k->grey);
    s.splashLife = k->splashLife;
    s.splashSize = k->splashSize;
    s.splashGrey = k->splashGrey;
    s.startSlice = static_cast<int16_t>(startSlice);
    s.endSlice = static_cast<int16_t>(endSlice);
    s.fade = static_cast<int16_t>(fade);
    s.times = {stayOn, stayOn + fadeOff, stayOn + fadeOff + stayOff, stayOn + fadeOff + stayOff + fadeOn};
    return s;
}

int weatherAtSlice(const WeatherSetup& s, int slice, int trackSlices) {
    int start = s.startSlice, end = s.endSlice;
    const int fade = s.fade;
    if (start == end && fade == 0)
        return s.count;
    const int fadeIn = start + fade;
    int fadeOut = end - fade;
    if (end < start) { // past the line
        if (slice < end)
            slice += trackSlices;
        fadeOut += trackSlices;
        end += trackSlices;
    }
    if (slice < start || slice > end)
        return 0;
    if (fade != 0 && (slice <= fadeIn || slice >= fadeOut)) {
        if (slice < fadeIn)
            return s.count - s.count * (fadeIn - slice) / fade;
        if (slice > fadeOut)
            return s.count - s.count * (slice - fadeOut) / fade;
    }
    return s.count;
}

int weatherAtTime(const WeatherSetup& s, int count, int elapsed, bool* restart) {
    if (restart)
        *restart = false;
    const auto& t = s.times;
    if (t[0] == 0 || t[1] == 0 || t[2] == 0 || t[3] == 0 || elapsed < t[0])
        return count;
    if (elapsed < t[1])
        return (t[1] - elapsed) * count / (t[1] - t[0]);
    if (elapsed < t[2])
        return 0;
    if (elapsed < t[3])
        return count - (t[3] - elapsed) * count / (t[3] - t[2]);
    if (restart)
        *restart = true;
    return count;
}

int weatherForEffects(int count, int sfxLevel) {
    if (sfxLevel == 1)
        return count / 2;
    if (sfxLevel == 2)
        return count / 4;
    return count;
}

float snowflakeSide(const WeatherSetup& s, float w) {
    // (size - w x size x (1 / half)) x 2 (0x549804), at least 1.
    const float side = (s.flake - w * s.flake * (1.0f / s.half)) * 2.0f;
    return std::max(side, 1.0f);
}

void Weather::start(const WeatherSetup& setup, const ParticleVec& eye, int step, ParticleRandom& random) {
    setup_ = setup;
    drops_ = {};
    splashes_ = {};
    last_ = step; // 0x8c0be0, so the first move is two ticks on
    cycleStart_ = step;
    const float h = setup.half, range = setup.top - setup.bottom;
    for (int i = 0; i < setup.count; ++i) {
        Drop& d = drops_[size_t(i)];
        d.position[0] = random.unit() * h * 2 - h + eye[0];
        d.position[1] = random.unit() * range + setup.bottom + eye[1];
        d.position[2] = random.unit() * h * 2 - h + eye[2];
    }
}

int Weather::active(int slice, int trackSlices, int step, int sfxLevel) {
    if (setup_.kind == 0)
        return 0;
    int n = weatherAtSlice(setup_, slice, trackSlices);
    bool restart = false;
    n = weatherAtTime(setup_, n, step - cycleStart_, &restart);
    if (restart)
        cycleStart_ = step;
    return weatherForEffects(n, sfxLevel);
}

void Weather::update(int step, int active, const ParticleVec& eye, bool covered, ParticleRandom& random) {
    if (setup_.kind == 0 || step - last_ <= 1)
        return;
    last_ = step;
    for (int i = 0; i < setup_.splashes; ++i)
        if (splashes_[size_t(i)].life != 0)
            --splashes_[size_t(i)].life;
    for (int i = 0; i < setup_.count; ++i)
        for (size_t a = 0; a < 3; ++a)
            drops_[size_t(i)].position[a] += setup_.velocity[a];
    const float h = setup_.half, range = setup_.top - setup_.bottom;
    for (int i = 0; i < std::min(active, setup_.count); ++i) {
        Drop& d = drops_[size_t(i)];
        ParticleVec& p = d.position;
        // Snow drifts round a little circle, each flake its own way.
        if (setup_.kind == 2 && !d.hidden) {
            const uint32_t angle = static_cast<uint32_t>(i) * 0x100 + static_cast<uint32_t>(step) * 0x100;
            p[0] = static_cast<float>((fixedSin(angle) >> 4) * kFixed) + p[0];
            p[2] = static_cast<float>((fixedCos(angle) >> 4) * kFixed) + p[2];
        }
        float x = p[0] - eye[0], y = p[1] - eye[1], z = p[2] - eye[2];
        if (!(y < setup_.bottom || y > setup_.top || std::fabs(x) > h || std::fabs(z) > h))
            continue;
        // Out of the box: back in on the other side (0x4dd7c7).
        d.hidden = covered;
        auto otherTwo = [&](float& a, float& b) {
            a = random.unit() * range + setup_.bottom;
            b = random.unit() * h * 2 - h;
        };
        if (x > h) {
            d.tailSet = false;
            x = -h;
            otherTwo(y, z);
        } else if (x < -h) {
            d.tailSet = false;
            x = h;
            otherTwo(y, z);
        }
        if (z > h || z < -h) {
            d.tailSet = false;
            otherTwo(y, z);
        }
        if (y > setup_.top || y < setup_.bottom) {
            d.tailSet = false;
            y = y > setup_.top ? setup_.bottom : setup_.top;
            x = random.unit() * h * 2 - h;
            z = random.unit() * h * 2 - h;
        }
        p = {eye[0] + x, eye[1] + y, eye[2] + z};
    }
}

void Weather::cut(const ParticleVec& eye, bool covered, ParticleRandom& random) {
    if (setup_.kind == 0)
        return;
    if (!covered) {
        const float h = setup_.half, range = setup_.top - setup_.bottom;
        for (int i = 0; i < setup_.count; ++i) {
            Drop& d = drops_[size_t(i)];
            d.tailSet = false;
            d.position[0] = random.unit() * h * 2 - h + eye[0];
            d.position[1] = random.unit() * range + setup_.bottom + eye[1];
            d.position[2] = random.unit() * h * 2 - h + eye[2];
        }
    }
    for (int i = 0; i < setup_.splashes; ++i)
        splashes_[size_t(i)].life = 0;
}

void Weather::tails(bool inCar, bool cut) {
    if (inCar != inCar_ || cut)
        for (int i = 0; i < setup_.count; ++i)
            drops_[size_t(i)].tailSet = false;
    inCar_ = inCar;
}

SplashSound splashSound(ParticleRandom& random) {
    SplashSound s;
    s.sound = static_cast<int>(random.next() & 3);
    s.volume = 90 + static_cast<int>(random.next() % 30);
    s.azimuth = random.next() % 0xffff;
    return s;
}

void Weather::respawnTop(Drop& d, const ParticleVec& eye, ParticleRandom& random) const {
    const float h = setup_.half;
    d.position[1] = eye[1] + setup_.top;
    d.position[0] = random.unit() * h * 2 - h + eye[0];
    d.position[2] = random.unit() * h * 2 - h + eye[2];
    d.tailSet = false;
}

// 0x4dd930: a free splash slot takes the drop's place on the screen (its
// x and y over its depth, before the view's scales); its life the kind's
// less up to a quarter, its size a half to all of the kind's, its angle at
// random. False when every slot is in use.
bool Weather::splash(const WeatherView& view, const ParticleVec& at, ParticleRandom& random) {
    for (int i = 0; i < setup_.splashes; ++i) {
        Splash& s = splashes_[size_t(i)];
        if (s.life != 0)
            continue;
        const float d[3] = {at[0] - view.eye[0], at[1] - view.eye[1], at[2] - view.eye[2]};
        const float w = d[0] * view.forward[0] + d[1] * view.forward[1] + d[2] * view.forward[2];
        const float inv = std::fabs(w) == 0 ? 65535.0f : 1.0f / w;
        int quarter = setup_.splashLife >> 2;
        if (quarter < 2)
            quarter = 1;
        const uint32_t r = random.next();
        s.x = (d[0] * view.right[0] + d[1] * view.right[1] + d[2] * view.right[2]) * inv;
        s.y = (d[0] * view.up[0] + d[1] * view.up[1] + d[2] * view.up[2]) * inv;
        s.life = setup_.splashLife - static_cast<int>(r % static_cast<uint32_t>(quarter));
        const float size = random.unit() * setup_.splashSize;
        s.size = size < setup_.splashSize * 0.5f ? setup_.splashSize * 0.5f : std::min(size, setup_.splashSize);
        s.angle = random.unit();
        return true;
    }
    return false;
}

void Weather::draw(const WeatherView& view, int active, bool splashes, ParticleRandom& random,
                   std::vector<WeatherPoly>& out, float intensity) {
    if (setup_.kind == 0)
        return;
    const bool rain = setup_.kind == 1;
    for (int i = 0; i < std::min(active, setup_.count); ++i) {
        Drop& d = drops_[size_t(i)];
        if (d.hidden)
            continue;
        const float r[3] = {d.position[0] - view.eye[0], d.position[1] - view.eye[1], d.position[2] - view.eye[2]};
        const float w = r[0] * view.forward[0] + r[1] * view.forward[1] + r[2] * view.forward[2];
        // Just behind the camera: back at the top of the box.
        if (w > -3 && w < 0) {
            respawnTop(d, view.eye, random);
            continue;
        }
        if (w <= 0 || w > setup_.half) {
            d.tailSet = false;
            continue;
        }
        const float x = view.cx + view.sx * (r[0] * view.right[0] + r[1] * view.right[1] + r[2] * view.right[2]) / w;
        const float y = view.cy + view.sy * (r[0] * view.up[0] + r[1] * view.up[1] + r[2] * view.up[2]) / w;
        if (x < view.left || x > view.rightEdge || y < view.top || y > view.bottom) {
            d.tailSet = false;
            continue;
        }
        // Near the camera: a splash on the screen, if a slot is free (the
        // rain also plays one of its sounds, 0x414f90, 0x4de989).
        if (w < 3 && splash(view, d.position, random)) {
            if (rain)
                ++splashSounds_;
            respawnTop(d, view.eye, random);
            continue;
        }
        WeatherPoly poly;
        poly.depth = w;
        poly.colour = dimByIntensity(setup_.colour, intensity);
        if (rain) {
            // From where it was, halfway on each frame, to where it is, and
            // two pixels across at its head.
            if (!d.tailSet)
                d.tail = {x, y};
            else
                d.tail = {(x + d.tail[0]) * 0.5f, (y + d.tail[1]) * 0.5f};
            d.tailSet = true;
            poly.corners = 3;
            poly.at = {{{d.tail[0], d.tail[1]}, {x, y}, {x + 2.0f, y}, {0, 0}}};
        } else {
            const float side = snowflakeSide(setup_, w);
            poly.corners = 4;
            poly.at = {{{x, y}, {x + side, y}, {x + side, y + side}, {x, y + side}}};
        }
        out.push_back(poly);
    }
    if (!splashes)
        return;
    for (int i = 0; i < setup_.splashes; ++i) {
        const Splash& s = splashes_[size_t(i)];
        if (s.life == 0)
            continue;
        const int grey = setup_.splashGrey * s.life / setup_.splashLife;
        float size = s.size;
        if (rain)
            size = static_cast<float>(s.life) * size / static_cast<float>(setup_.splashLife);
        const float c = static_cast<float>(std::cos(s.angle * kTurn)) * size;
        const float n = static_cast<float>(std::sin(s.angle * kTurn)) * size;
        WeatherPoly poly;
        poly.corners = 4;
        poly.splash = true;
        poly.depth = 1.0f / 0.9f; // rhw 0x3f666666
        poly.colour = dimByIntensity(grey4(grey), intensity);
        auto at = [&](float a, float b) { return std::array<float, 2>{a * view.sx + view.cx, b * view.sy + view.cy}; };
        poly.at = {at(s.x + c, s.y + n), at(s.x - n, s.y + c), at(s.x - c, s.y - n), at(s.x + n, s.y - c)};
        out.push_back(poly);
    }
}

const SplatTemplate* splatTemplate(int kind) {
    // +0x64-+0x7C of the templates at 0x560b64, read from nfs3.exe
    // (November 2026); every other kind's count and life are 0.
    static const SplatTemplate k1{10, 96, 1, 0.08f, 0.5f, 0x7FFFFFFF, 0};
    static const SplatTemplate k4{1, 76800, 1, 0.1f, 0, 0x8FFFFFFF, 0.001f};
    static const SplatTemplate k10{5, 128, 1, 0.2f, 0.75f, 0xFFECECFF, 0};
    static const SplatTemplate k11{2, 1024, 0, 0.2f, 0.5f, 0x7FFFFFFF, 0};
    switch (kind) {
    case 1: return &k1;
    case 4: return &k4;
    case 10: return &k10;
    case 11: return &k11;
    default: return nullptr;
    }
}

bool SplatScreen::hit(int kind, float x, float y, float z, bool inCar, ParticleRandom& random) {
    const SplatTemplate* t = splatTemplate(kind);
    if (!t || t->count == 0 || t->life == 0 || inCar)
        return false;
    if (t->chance != 0 && !(random.unit() <= t->chance))
        return true;
    for (int i = 0; i < t->count; ++i) {
        Splat* free = nullptr;
        for (Splat& s : slots_)
            if (s.life == 0) {
                free = &s;
                break;
            }
        if (!free)
            return false;
        const float depth = z != 0 ? z : 1.5259022e-05f;
        float sx = x * (1.0f / depth), sy = y * (1.0f / depth);
        if (t->jitter != 0) {
            sx += random.unit() * t->jitter * 2.0f - t->jitter; // 0x540398
            sy += random.unit() * t->jitter * 2.0f - t->jitter;
        }
        bool covered = false;
        for (Splat& s : slots_) {
            if (s.life == 0 || !(std::fabs(sx - s.x) < s.size * 0.5f) || !(std::fabs(sy - s.y) < s.size * 0.5f))
                continue; // 0x5403a0
            if (kind != 10 || s.kind != 11) {
                covered = true;
                break;
            }
            s.life >>= 1; // rain washing mud off
        }
        if (covered)
            continue;
        free->kind = kind;
        free->life = t->life - static_cast<int>(random.next() % static_cast<uint32_t>(t->life >> 1));
        const float size = t->size - t->size * 0.5f * random.unit();
        const float turn = random.unit() * 2.0f * 3.14159265358979f;
        const float s = std::sin(turn) * size, c = std::cos(turn) * size;
        free->x = sx;
        free->y = sy;
        free->size = size;
        free->corners = {{{sx - s, sy + c}, {sx + c, sy + s}, {sx + s, sy - c}, {sx - c, sy - s}}};
        free->colour = t->colour;
        free->flags = t->flags;
        if (kind == 4)
            ++sounds_; // 0x4ca833: 0x415010(view)
    }
    return true;
}

void SplatScreen::step() {
    for (Splat& s : slots_) {
        if (s.life == 0)
            continue;
        const SplatTemplate* t = splatTemplate(s.kind);
        const uint32_t alpha = t && t->life ? static_cast<uint32_t>((static_cast<int64_t>(t->colour >> 24) * s.life) / t->life) & 0xff : 0;
        s.colour = (s.colour & 0x00FFFFFF) | (alpha << 24);
        --s.life;
        if (alpha == 0)
            s.life = 0;
    }
}

int SplatScreen::live() const {
    return static_cast<int>(std::count_if(slots_.begin(), slots_.end(), [](const Splat& s) { return s.life != 0; }));
}

} // namespace reluva::nfs
