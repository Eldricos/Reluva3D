// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/particles.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

constexpr float kTurn = 6.283184714f; // 0x54b6b0: sin and cos take turns

// The templates' values, read from nfs3.exe at 0x560b64 + kind × 128.
const ParticleTemplate kDust = {
    1, 128, 1,            // count, life, throttle
    3.1f, 2.0f, 3.5f,     // up, along, across
    0.5f,                 // spread
    0.05f, 0.05f, 0.046875f, // wobble
    0.0f,                 // gravity
    0.5f, 0.5f, 0.8f,     // spin, inherit, drag
    0.75f, 3.0f, 0.5f,    // size, growth, size spread
    0x7fdfdfdf, 0x7f000000, 0, false, 150.0f};
const ParticleTemplate kSparks = {
    30, 16, 0,
    2.0f, 5.0f, 2.0f,
    0.5f,
    0.02f, 0.05f, 0.03125f,
    -0.25f,
    0.0f, 0.5f, 1.0f,
    0.01f, 0.0f, 0.0f,
    0xffffffff, 0xff00c6ff, 1, true, 50.0f};
const ParticleTemplate kSmoke = {
    1, 256, 3,
    1.5f, 0.5f, 0.5f,
    0.5f,
    0.0f, 0.0f, 0.0f,
    0.0f,
    0.1f, 0.5f, 0.95f,
    0.75f, 2.5f, 0.5f,
    0x7fffffff, 0x7f404040, 0, false, 150.0f};

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

const ParticleTemplate* particleTemplate(int kind) {
    switch (kind) {
    case 2: return &kDust;
    case 3: return &kSparks;
    case 6: return &kSmoke;
    default: return nullptr;
    }
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
    const ParticleTemplate* t = particleTemplate(kind);
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
    p.colour = t->colour; // less a random part of +0x54's bytes, 0 in these kinds
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
        const ParticleTemplate* t = particleTemplate(p.kind);
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
            // (A template with +0x3c set holds the spin within ±0.25; none
            // of these do.)
            if (p.spin != 0) {
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
                                          float half) {
    const float a = std::sin(p.angle * kTurn) * half, b = std::cos(p.angle * kTurn) * half;
    return {offset(p.position, right, up, -a, b), offset(p.position, right, up, b, a),
            offset(p.position, right, up, a, -b), offset(p.position, right, up, -b, -a)};
}

} // namespace reluva::nfs
