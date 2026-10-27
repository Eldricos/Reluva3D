// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/particles.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

ParticleSettings ParticleSettings::fromRecord(const Ac2Record& o) {
    ParticleSettings s;
    // The class's defaults (the constructors at 0x42c5b0, 0x434290 and
    // 0x436f40), which the record's fields then replace.
    if (o.tag == "RAIN" || o.tag == "SNOW") {
        const bool rain = o.tag == "RAIN";
        s.texture = rain ? "Elements\\Particles\\rain01.tga" : "Elements\\Particles\\snowflake01.tga";
        s.position[1] = 1500;
        s.depth = 500;
        s.velocity[0] = rain ? -500 : 0;
        s.velocity[1] = rain ? -1000 : -400;
        s.velocity[2] = 0;
        s.burst = rain ? 50 : 5;
        s.interval = rain ? 0.05f : 0.08f;
        s.spread = 2000;
        s.size = 30;
        s.life = rain ? 0.2f : 5;
        s.alphaBlend = true;
        s.mode = 2;
        s.condition = "weather >0";
        if (!rain)
            s.sway = 20, s.swaySpeed = 5;
    }
    auto real = [&](const Ac2Record& r, const char* name, float& out) {
        if (r.field(name))
            out = r.f(name);
    };
    auto integer = [&](const Ac2Record& r, const char* name, int& out) {
        if (r.field(name))
            out = r.i(name);
    };
    auto flag = [&](const Ac2Record& r, const char* name, bool& out) {
        if (r.field(name))
            out = r.i(name) != 0;
    };
    if (!o.text("file").empty())
        s.texture = o.text("file");
    if (o.field("condition"))
        s.condition = o.text("condition");
    real(o, "x", s.position[0]);
    real(o, "y", s.position[1]);
    real(o, "z", s.position[2]);
    real(o, "rotX", s.rotation[0]);
    real(o, "rotY", s.rotation[1]);
    real(o, "rotZ", s.rotation[2]);
    flag(o, "alpha", s.alphaBlend);
    // Drawn with the 2D layers at its depth if it has one, else at z.
    if (o.field("depth") && o.f("depth") != 0)
        s.depth = o.f("depth");
    else if (o.field("z"))
        s.depth = o.f("z");
    if (const Ac2Record* p = o.part("SYST")) {
        real(*p, "red", s.colour[0]);
        real(*p, "green", s.colour[1]);
        real(*p, "blue", s.colour[2]);
        real(*p, "velocityX", s.velocity[0]);
        real(*p, "velocityY", s.velocity[1]);
        real(*p, "velocityZ", s.velocity[2]);
        integer(*p, "burst", s.burst);
        real(*p, "interval", s.interval);
        integer(*p, "copies", s.copies);
        integer(*p, "spread", s.spread);
        real(*p, "bounce", s.bounce);
        integer(*p, "bounces", s.bounces);
        real(*p, "size", s.size);
        real(*p, "life", s.life);
        integer(*p, "mode", s.mode);
        flag(*p, "blink", s.blink);
        real(*p, "onTime", s.onTime);
        real(*p, "offTime", s.offTime);
        flag(*p, "screenSpace", s.screenSpace);
        real(*p, "sway", s.sway);
        real(*p, "swaySpeed", s.swaySpeed);
        real(*p, "gravity", s.gravity);
        real(*p, "fadeIn", s.fadeIn);
        real(*p, "fadeOut", s.fadeOut);
        integer(*p, "signX", s.signX);
        integer(*p, "signY", s.signY);
        integer(*p, "signZ", s.signZ);
    }
    return s;
}

ParticleSystem::ParticleSystem(ParticleSettings settings, uint32_t seed) : s_(std::move(settings)), seed_(seed) {}

int ParticleSystem::random() {
    seed_ = seed_ * 214013u + 2531011u;
    return int((seed_ >> 16) & 0x7fff);
}

// The emitter's coordinate plus a whole number in [-spread, spread).
float ParticleSystem::spreadRandom(float centre) {
    const int range = std::max(1, s_.spread);
    return centre - float(range) + float(random() % (2 * range));
}

void ParticleSystem::emit() {
    Particle p;
    p.swayAngle = time_;
    if (s_.mode == 1) {
        for (int k = 0; k < 3; k++)
            p.origin[k] = spreadRandom(s_.position[k]);
        std::copy(p.origin, p.origin + 3, p.position);
    } else if (s_.mode == 2) {
        p.origin[0] = spreadRandom(s_.position[0]);
        p.origin[2] = spreadRandom(s_.position[2]);
        p.origin[1] = s_.position[1];
        std::copy(p.origin, p.origin + 3, p.position);
    } else {
        // Thrown: a direction around (a) and up from straight up (b, at
        // most 45 degrees), its speed scaled per axis.
        const float a = float(random()) * (1 / 32767.0f) * 6.2831855f;
        const float b = float(random()) * (1 / 32767.0f) * 0.7853982f;
        p.origin[0] = spreadRandom(s_.position[0]);
        p.origin[1] = s_.position[1];
        p.origin[2] = spreadRandom(s_.position[2]);
        auto sign = [](float v, int rule) {
            // 1: made positive, 2: made negative, else as it came.
            if ((rule == 1 && v < 0) || (rule == 2 && v > 0))
                return -v;
            return v;
        };
        p.start[0] = sign(std::cos(a) * s_.velocity[0] * std::sin(b), s_.signX);
        p.start[2] = sign(std::sin(b) * std::sin(a) * s_.velocity[2], s_.signZ);
        float r = float(random()) * (1 / 32767.0f);
        if (r < 0.5f)
            r += 0.5f;
        p.start[1] = r * std::cos(b) * s_.velocity[1];
        if (s_.signY == 2 || (s_.signY == 0 && (random() & 1)))
            p.start[1] = -p.start[1];
        std::copy(p.origin, p.origin + 3, p.position);
        std::copy(p.start, p.start + 3, p.velocity);
    }
    p.born = time_;
    particles_.push_back(p);
}

void ParticleSystem::update(float dt) {
    time_ += dt;
    for (Particle& p : particles_) {
        p.swayAngle += dt * s_.swaySpeed;
        const float age = time_ - p.born;
        if (s_.mode == 1) {
            p.dead = age > s_.life;
            continue;
        }
        if (s_.mode == 2) {
            for (int k = 0; k < 3; k++)
                p.position[k] += dt * s_.velocity[k];
            p.dead = p.position[1] < 0;
            continue;
        }
        // Thrown: its start velocity over its age, turned by the object's
        // rotation (D3DXMatrixRotationYawPitchRoll), then gravity.
        const float v[3] = {age * p.start[0], age * p.start[1], age * p.start[2]};
        // Yaw is rotY (the object's +8), pitch rotX (+4), roll rotZ (+0xc).
        const float cy = std::cos(s_.rotation[1]), sy = std::sin(s_.rotation[1]);
        const float cp = std::cos(s_.rotation[0]), sp = std::sin(s_.rotation[0]);
        const float cr = std::cos(s_.rotation[2]), sr = std::sin(s_.rotation[2]);
        // A row vector times roll (about Z), pitch (X), then yaw (Y).
        const float t[3] = {v[0] * cr - v[1] * sr, v[0] * sr + v[1] * cr, v[2]};
        const float u[3] = {t[0], t[1] * cp - t[2] * sp, t[1] * sp + t[2] * cp};
        const float w[3] = {u[0] * cy + u[2] * sy, u[1], -u[0] * sy + u[2] * cy};
        for (int k = 0; k < 3; k++)
            p.position[k] = p.origin[k] + w[k];
        p.position[1] += age * age * s_.gravity * -0.5f;
        p.velocity[1] = p.start[1] - age * s_.gravity;
        const bool landed = p.position[1] < 0 && !s_.screenSpace;
        if (!landed && age <= s_.life)
            continue;
        // On the ground or out of time: off again from where it is, slower,
        // while it has bounces left.
        if (s_.bounce > 0 && p.bounces < s_.bounces) {
            std::copy(p.position, p.position + 3, p.origin);
            for (int k = 0; k < 3; k++)
                p.start[k] *= s_.bounce;
            std::copy(p.start, p.start + 3, p.velocity);
            p.born = time_;
            p.bounces++;
        } else {
            p.dead = true;
        }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.dead; }),
                     particles_.end());

    if (s_.blink) {
        blinkLeft_ -= dt;
        if (blinkLeft_ <= 0) {
            on_ = !on_;
            blinkLeft_ = on_ ? s_.onTime : s_.offTime;
        }
        if (!on_)
            return;
    }
    nextBurst_ -= dt;
    if (nextBurst_ > 0)
        return;
    nextBurst_ = s_.interval;
    constexpr size_t kMax = 0x1000;
    for (int n = 0; n < s_.burst && particles_.size() < kMax; n++)
        emit();
}

void ParticleSystem::points(std::vector<Point>& out) const {
    auto channel = [](float v) {
        if (v >= 1)
            return uint8_t(255);
        if (v <= 0)
            return uint8_t(0);
        return uint8_t(v * 255 + 0.5f);
    };
    for (const Particle& p : particles_) {
        Point pt;
        std::copy(p.position, p.position + 3, pt.position);
        if (s_.sway > 0)
            pt.position[0] += std::sin(p.swayAngle) * s_.sway;
        // Faded in over its first fadeIn seconds, out over its last fadeOut.
        const float age = time_ - p.born;
        float alpha = 1;
        if (s_.fadeIn != 0 && age <= s_.fadeIn)
            alpha = age / s_.fadeIn;
        else if (s_.fadeOut != 0 && age >= s_.life - s_.fadeOut)
            alpha = (s_.fadeOut - (age - (s_.life - s_.fadeOut))) / s_.fadeOut;
        pt.rgba[0] = channel(s_.colour[0]);
        pt.rgba[1] = channel(s_.colour[1]);
        pt.rgba[2] = channel(s_.colour[2]);
        pt.rgba[3] = channel(alpha);
        for (int c = 0; c < s_.copies; c++)
            out.push_back(pt);
    }
}

} // namespace reluva::awe
