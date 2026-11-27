// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/particles.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace reluva::stawayteam {

namespace {

constexpr float kDegree = std::numbers::pi_v<float> / 180.0f;

float number(const TextObject *o, const char *key, float fallback = 0.0f) {
    return o ? float(o->number(key).value_or(fallback)) : fallback;
}

std::string text(const TextObject *o, const char *key) {
    const std::string *value = o ? o->value(key) : nullptr;
    return value ? *value : std::string();
}

struct Vec {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

Vec operator*(const Vec &a, float k) {
    return {a.x * k, a.y * k, a.z * k};
}

Vec operator+(const Vec &a, const Vec &b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

} // namespace

Envelope Envelope::read(const TextObject *o) {
    Envelope e;
    e.start = number(o, "Start Value");
    e.end = number(o, "End Value");
    e.speed = number(o, "Speed");
    const std::string ending = text(o, "Ending Behavior");
    if (ending == "Restart from beginning")
        e.ending = Ending::kRestart;
    else if (ending == "Reverse Direction")
        e.ending = Ending::kReverse;
    e.reset();
    return e;
}

void Envelope::reset() {
    value = start;
    toEnd = true;
    direction = end > start ? 1.0f : -1.0f;
}

void Envelope::update(float seconds) {
    // At the end it's heading for, its Ending Behavior; then on by its
    // Speed, kept from passing that end (0x4aa500).
    if (value == (toEnd ? end : start)) {
        if (ending == Ending::kStop)
            return;
        if (ending == Ending::kRestart) {
            value = start;
        } else {
            direction = -direction;
            toEnd = !toEnd;
        }
    }
    const float target = toEnd ? end : start;
    value += speed * direction * seconds;
    if ((direction >= 0.0f && value > target) || (direction < 0.0f && value < target))
        value = target;
}

ParticleSystem::ParticleSystem(const TextObject &ai, uint32_t seed) : _random(seed | 1u) {
    _outerCone = Envelope::read(ai.object("Outer Cone Angle"));
    _innerCone = Envelope::read(ai.object("Inner Cone Angle"));
    _faceAngle = Envelope::read(ai.object("Face Angle"));
    _trajectory = Envelope::read(ai.object("Cone Trajectory"));
    _speed = Envelope::read(ai.object("Speed"));
    _age = Envelope::read(ai.object("Age"));
    _speedVariance = number(&ai, "Speed Variance");
    _ageVariance = number(&ai, "Age Variance");
    _frequency = number(&ai, "Burst Frequency");
    _frequencyVariance = number(&ai, "Burst Frequency Variance");
    _stopAfter = int(number(&ai, "Num Bursts To Stop After"));
    _orient = number(&ai, "Orient To Parent's Rotation") != 0.0f;
    _offsetX = number(&ai, "X Offset");
    _offsetY = number(&ai, "Y Offset");
    _height = number(&ai, "Height");
    _enabled = number(&ai, "Enabled", 1.0f) != 0.0f;
    if (const TextObject *shape = ai.object("Start Point Type")) {
        const std::string &kind = shape->className;
        if (kind == "CLineParticleStartPositionType") {
            _shape = Shape::kLine;
            _inside = Envelope::read(shape->object("Center Length"));
        } else if (kind == "CDiskParticleStartPositionType" || kind == "CCylinderParticleStartPositionType") {
            _shape = kind == "CDiskParticleStartPositionType" ? Shape::kDisk : Shape::kCylinder;
            _inside = Envelope::read(shape->object("Inside Radius"));
            _outside = Envelope::read(shape->object("Outside Radius"));
            _length = Envelope::read(shape->object("Height of Cylinder"));
            _yxRatio = number(shape, "YX Ratio", 1.0f);
            _useCone = number(shape, "Use Cone Angle For Start Pos") != 0.0f;
        }
    }
    if (const TextObject *list = ai.object("Particles"))
        for (const TextField *item : list->items()) {
            const TextObject *info = item->object.get();
            if (!info)
                continue;
            Kind k;
            k.model = text(info, "Model");
            const TextObject *motion = info->object("AI");
            if (motion && motion->className == "CGravityParticleAI") {
                k.motion = Motion::kGravity;
                k.gravity = number(motion, "Gravity");
            } else if (motion && motion->className == "CSwirlParticleAI") {
                k.motion = Motion::kSwirl;
                k.angular = number(motion, "Angular Velocity");
                k.angularVariance = number(motion, "Angular Velocity Variance");
            }
            k.animated = number(info, "Animated") != 0.0f;
            k.obeyFps = number(info, "Obey Sequence FPS") != 0.0f;
            k.count = int(number(info, "Num Particles"));
            k.countVariance = int(number(info, "Num Particles Variance"));
            _kinds.push_back(std::move(k));
        }
    if (const TextObject *sounds = ai.object("Sounds"))
        for (const TextField *item : sounds->items())
            _sounds.push_back(item->value);
    _soundByBurst = ai.number("Choose Sound Based On Burst").value_or(0.0) != 0.0;
    // Started as it's made (inferred: as the AI starts).
    restart();
}

void ParticleSystem::restart() {
    // Its rate with its variance drawn, and the next burst a random part of
    // the way off (0x524870).
    _rate = _frequency + random(-_frequencyVariance, _frequencyVariance);
    _bursts = random(0.0f, 1.0f);
    _made = 0;
    for (Envelope *e : {&_outerCone, &_innerCone, &_faceAngle, &_trajectory, &_speed, &_age})
        e->reset();
}

void ParticleSystem::turn(bool on) {
    _enabled = on;
    if (on)
        restart();
}

void ParticleSystem::state(StateIo &s) {
    s.list(_particles, [&](Particle &p) {
        s.io(p.kind);
        for (float *f : {&p.u, &p.v, &p.z, &p.vu, &p.vv, &p.vz, &p.age, &p.life, &p.omega})
            s.io(*f);
    });
    for (Envelope *e :
         {&_outerCone, &_innerCone, &_faceAngle, &_trajectory, &_speed, &_age, &_inside, &_outside, &_length}) {
        s.io(e->value);
        s.io(e->direction);
        s.io(e->toEnd);
    }
    s.io(_enabled);
    s.io(_bursts);
    s.io(_rate);
    s.io(_made);
    s.io(_lastMade);
    s.io(_lastMost);
    s.io(_random);
}

float ParticleSystem::random(float low, float high) {
    _random = _random * 1103515245u + 12345u;
    const float t = float((_random >> 8) & 0xffffff) / float(0xffffff);
    return low + (high - low) * t;
}

int ParticleSystem::randomInt(int low, int high) {
    if (high <= low)
        return low;
    return std::min(high, low + int(random(0.0f, float(high - low + 1))));
}

int ParticleSystem::update(float seconds, float ownerAngle) {
    // The particles: aged, and gone at their age; falling by their Gravity
    // (its units, 0x51faa0) and bouncing on the ground by their model's
    // Bounce Factor and Friction (0x51fc10); a swirl's turned about the
    // emitter point, its across-the-map part by the start shape's YX Ratio
    // (0x5208e0).
    for (Particle &p : _particles) {
        p.age += seconds;
        const Kind &k = _kinds[p.kind];
        if (k.motion == Motion::kGravity)
            p.vz -= k.gravity * kGravityScale * seconds;
        p.u += p.vu * seconds;
        p.v += p.vv * seconds;
        p.z += p.vz * seconds;
        if (k.motion == Motion::kGravity && p.z < 0.0f && p.vz < 0.0f) {
            p.z = 0.0f;
            // As anything bouncing (0x51fc10): too slow along the ground and
            // it stops there, too slow a bounce and it stays on it.
            p.vz = -p.vz * k.bounceFactor;
            p.vu *= 1.0f - k.bounceFriction;
            p.vv *= 1.0f - k.bounceFriction;
            if (std::hypot(p.vu, p.vv) < k.minBounceFriction)
                p.vu = p.vv = 0.0f;
            if (p.vz <= k.minBounce)
                p.vz = 0.0f;
        }
        if (k.motion == Motion::kSwirl && p.omega != 0.0f) {
            const float turn = p.omega * seconds;
            const float c = std::cos(turn);
            const float s = std::sin(turn);
            const float y = p.v / _yxRatio;
            const float u = p.u * c - y * s;
            p.v = (p.u * s + y * c) * _yxRatio;
            p.u = u;
        }
    }
    std::erase_if(_particles, [](const Particle &p) { return p.age >= p.life; });
    if (!_enabled)
        return 0;
    // Its envelopes move on; bursts come at its frequency a second, its
    // variance drawn anew after each; one with no rate yet has its first at
    // once (0x5249a6).
    for (Envelope *e : {&_outerCone, &_innerCone, &_faceAngle, &_trajectory, &_speed, &_age})
        e->update(seconds);
    for (Envelope *e : {&_inside, &_outside, &_length})
        e->update(seconds);
    if (_rate == 0.0f) {
        _rate = _frequency + random(-_frequencyVariance, _frequencyVariance);
        _bursts = 1.0f - _rate * seconds;
    }
    _bursts += seconds * _rate;
    if (_bursts < 1.0f)
        return 0;
    int due = int(_bursts);
    _bursts -= float(due);
    _made += due;
    if (_stopAfter > 0 && _made >= _stopAfter) {
        due -= _made - _stopAfter;
        _enabled = false;
    }
    for (int i = 0; i < due; ++i)
        burst(ownerAngle);
    _rate = _frequency + random(-_frequencyVariance, _frequencyVariance);
    return std::max(due, 0);
}

void ParticleSystem::burst(float ownerAngle) {
    // The cone's axis from its Face Angle about the ground (and the owner's
    // facing, for one that Orients To Parent's Rotation) and its Cone
    // Trajectory up from it, and two directions across it, as the game
    // turns a particle's direction (0x5250e0).
    const float face = _faceAngle.now() * kDegree + (_orient ? ownerAngle : 0.0f);
    const float up = _trajectory.now() * kDegree;
    const Vec axis{std::cos(up) * std::cos(face), std::cos(up) * std::sin(face), std::sin(up)};
    const Vec across{-std::sin(face), std::cos(face), 0.0f};
    const Vec third{axis.y * across.z - axis.z * across.y, axis.z * across.x - axis.x * across.z,
                    axis.x * across.y - axis.y * across.x};
    _lastMade = 0;
    _lastMost = 0;
    for (size_t k = 0; k < _kinds.size(); ++k) {
        const Kind &kind = _kinds[k];
        const int count = std::max(0, kind.count + randomInt(-kind.countVariance, kind.countVariance));
        _lastMade += count;
        _lastMost += kind.count + kind.countVariance;
        for (int n = 0; n < count; ++n) {
            // Off the axis by the inner cone angle and some of the outer,
            // at any turn about it (0x524bb7, 0x525040); its speed and age
            // each its envelope's, give or take its variance.
            const float off = (_innerCone.now() + random(0.0f, _outerCone.now())) * kDegree;
            const float turn = random(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
            const Vec dir = axis * std::cos(off) + (across * std::cos(turn) + third * std::sin(turn)) * std::sin(off);
            const float speed = _speed.now() + random(-_speedVariance, _speedVariance);
            Particle p;
            p.kind = k;
            p.life = _age.now() + random(-_ageVariance, _ageVariance);
            // Where it starts: the emitter point; a line's anywhere along its
            // Center Length either way across the axis (0x522ee0); a disk's
            // on a ring about it, from its Inside Radius out by its Outside,
            // the one way across scaled by its YX Ratio (0x522640); a
            // cylinder's, that up the axis by up to its Height (0x523740).
            Vec at;
            if (_shape == Shape::kLine) {
                at = third * (_inside.now() * random(-1.0f, 1.0f));
            } else if (_shape == Shape::kDisk || _shape == Shape::kCylinder) {
                // Following the cone, on the side of the particle's own
                // direction across the axis (its (y, z) made 1 long,
                // 0x52264f); else at any turn, (cos a, -sin a) (0x522710).
                const float angle = _useCone ? turn : random(0.0f, 2.0f * std::numbers::pi_v<float>);
                const float inside = _inside.now();
                const float r = random(inside, inside + _outside.now());
                const float sense = _useCone ? 1.0f : -1.0f;
                at = across * (std::cos(angle) * r * _yxRatio) + third * (sense * std::sin(angle) * r);
                if (_shape == Shape::kCylinder)
                    at = at + axis * random(0.0f, _length.now());
            }
            p.u = at.x;
            p.v = at.y;
            p.z = _height + at.z;
            p.vu = dir.x * speed;
            p.vv = dir.y * speed;
            p.vz = dir.z * speed;
            if (kind.motion == Motion::kSwirl)
                p.omega = kind.angular + random(-std::abs(kind.angularVariance), std::abs(kind.angularVariance));
            if (p.life > 0.0f)
                _particles.push_back(p);
        }
    }
}

int ParticleSystem::burstSound(uint32_t random) const {
    if (_sounds.empty())
        return -1;
    const auto n = int(_sounds.size());
    if (_soundByBurst)
        return std::clamp(n * _lastMade / (_lastMost + 1), 0, n - 1);
    return int(random % uint32_t(n));
}

} // namespace reluva::stawayteam
