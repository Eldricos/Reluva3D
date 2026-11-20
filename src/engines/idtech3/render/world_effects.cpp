// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/render/world_effects.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace reluva::idtech3 {

namespace {

// Drops start again this high above the eye (0x48e975).
constexpr float kRespawnHeight = 40;
// The drop's width, of the view's left axis (0x4cf510).
constexpr float kDropWidth = 0.2f;
// How fast the rain fades in or out, a second (0x48e891).
constexpr float kFadeSpeed = 0.5f;
// The frames between gusts: 200 and up to 249 more (0x48e802).
constexpr int kGustFrames = 200;
constexpr int kGustSpread = 250;

} // namespace

float WorldEffects::random(float from, float to) {
    return from + (to - from) * std::uniform_real_distribution<float>(0, 1)(_random);
}

void WorldEffects::respawn(std::array<float, 6> &drop, bool start) {
    // Anywhere round the eye; at the start anywhere in height, after at
    // the top (0x48e420, 0x48e958).
    drop[0] = random(0, _spread[0]);
    drop[1] = random(0, _spread[1]);
    drop[2] = start ? random(-_spread[2], kRespawnHeight) : kRespawnHeight;
    for (size_t i = 0; i < 3; ++i)
        drop[3 + i] = random(_minVelocity[i], _maxVelocity[i]);
}

void WorldEffects::clear() {
    _drops.clear();
    _fade = 0;
    _wind = {};
    _windTarget = {};
    _gustFrames = 0;
    _fallingHere = false;
}

bool WorldEffects::command(const std::string &line) {
    // As jk2sp.exe's R_WorldEffectCommand (0x48ed50): "rain init <drops>"
    // starts the rain afresh, "rain remove" stops it; the rest set its
    // values (0x48e530).
    std::istringstream in(line);
    std::string system;
    std::string verb;
    in >> system >> verb;
    system = fs::toLower(system);
    verb = fs::toLower(verb);
    if (system != "rain")
        return false;
    if (verb == "init") {
        int count = 0;
        in >> count;
        *this = WorldEffects{};
        _drops.resize(size_t(std::max(count, 0)));
        for (auto &drop : _drops)
            respawn(drop, true);
        return true;
    }
    if (verb == "remove") {
        clear();
        return true;
    }
    if (_drops.empty())
        return false;
    if (verb == "fall") {
        in >> _minVelocity[2] >> _maxVelocity[2];
    } else if (verb == "spread") {
        in >> _spread[1] >> _spread[2];
    } else if (verb == "alpha") {
        in >> _alpha;
    } else if (verb == "height") {
        in >> _height;
    } else if (verb == "angle") {
        in >> _windAngle;
    } else {
        return false;
    }
    return true;
}

void WorldEffects::update(float seconds, int32_t eyeContents) {
    // As the rain's update (0x48e740).
    if (_drops.empty())
        return;
    seconds = std::max(seconds, 0.0f);
    // A gust's direction now and then, across; the wind turns to it.
    if (--_gustFrames < 0) {
        _windTarget = {1 - 2 * random(0, 1), 1 - 2 * random(0, 1), 0};
        const float length = std::sqrt(_windTarget[0] * _windTarget[0] + _windTarget[1] * _windTarget[1]);
        if (length > 0)
            for (float &v : _windTarget)
                v /= length;
        _gustFrames = kGustFrames + int(_random() % kGustSpread);
    }
    for (size_t i = 0; i < 3; ++i)
        _wind[i] += (_windTarget[i] - _wind[i]) * seconds;
    // In the open and out of water it fades in, else out.
    _fallingHere = (eyeContents & kContentsOutside) && !(eyeContents & kContentsWater);
    if (_fallingHere)
        _fade = std::min(1.0f, _fade + seconds * kFadeSpeed);
    else
        _fade = std::max(0.0f, _fade - seconds * kFadeSpeed);
    if (_fade <= 0)
        return;
    for (auto &drop : _drops) {
        for (size_t i = 0; i < 3; ++i)
            drop[i] += drop[3 + i] * seconds;
        if (drop[2] < -_spread[2])
            respawn(drop, false);
    }
}

void WorldEffects::triangles(const std::array<float, 3> &eye, const std::array<float, 3> &left,
                             std::vector<gfx::Vertex> &out) const {
    // As the rain's render (0x48e9d0): each drop a triangle from where it
    // is (leant with the wind the lower it is), across by a fifth of the
    // left axis, and down by its height against the wind. Below the eye
    // a drop is fainter the nearer it is, as the alpha times its distance
    // over its depth.
    if (_drops.empty() || _fade <= 0)
        return;
    const std::array<float, 3> side{left[0] * kDropWidth, left[1] * kDropWidth, left[2] * kDropWidth};
    const std::array<float, 3> fall{-_height * _windAngle * _wind[0], -_height * _windAngle * _wind[1], -_height};
    out.reserve(out.size() + _drops.size() * 3);
    for (const auto &drop : _drops) {
        float alpha = _alpha;
        if (drop[2] < 0)
            alpha = std::min(-drop[1] / drop[2] * _alpha, _alpha);
        alpha *= _fade;
        const std::array<float, 3> at{eye[0] + std::sin(drop[0]) * drop[1] + drop[2] * _windAngle * _wind[0],
                                      eye[1] + std::cos(drop[0]) * drop[1] + drop[2] * _windAngle * _wind[1],
                                      eye[2] + drop[2]};
        auto vertex = [&](const std::array<float, 3> &by1, const std::array<float, 3> &by2, float s, float t) {
            gfx::Vertex v{};
            for (size_t i = 0; i < 3; ++i)
                v.pos[i] = at[i] + by1[i] + by2[i];
            v.uv[0] = s;
            v.uv[1] = t;
            v.color[0] = v.color[1] = v.color[2] = 255;
            v.color[3] = uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255 + 0.5f);
            v.normal[2] = 1;
            out.push_back(v);
        };
        const std::array<float, 3> none{};
        vertex(none, none, 1, 0);
        vertex(side, none, 0, 0);
        vertex(side, fall, 0, 1);
    }
}

} // namespace reluva::idtech3
