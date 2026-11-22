// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The team's noise, and the enemies hearing it and going to look
// (docs/engines/stawayteam/systems/maps.md).

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// Heard from as far above or below as this times less than to the side:
// the view's foreshortening (0x519465).
constexpr float kHearingSquash = 1.40299f;
// An attribute's whole (0x5e5830).
constexpr float kAttributeWhole = 100.0f;
// How long an enemy looks about where it heard something when its AI
// doesn't say (inferred).
constexpr float kLookAbout = 5.0f;

float number(const TextObject *o, const char *key, float fallback = 0.0f) {
    return o ? float(o->number(key).value_or(fallback)) : fallback;
}

} // namespace

void MapScreen::makeNoise(float x, float y, float radius, float age) {
    if (radius <= 0.0f || age <= 0.0f)
        return;
    _noises.insert(_noises.begin(), Noise{x, y, radius, age, age});
}

float MapScreen::footstepNoise(const Officer &o, const Animation::Noise &step) const {
    // Its Radius, times 1 less half of the maker's attribute's part of
    // 100 (0x5e54b0, at half from 0x5180be), times Rules.txt's modifier
    // for how they move (0x5c9530).
    float quiet = 1.0f;
    if (!step.attribute.empty())
        for (const TeamMember::Attribute &a : _team[o.member].attributes)
            if (a.name == step.attribute)
                quiet = 0.5f + 0.5f * (1.0f - float(a.value) / kAttributeWhole);
    const TextObject *custom = _rules.object("Custom Rules");
    const char *modifier = o.move == 1   ? "Run Noise Modifier"
                           : o.move == 2 ? "Crouch Noise Modifier"
                                         : "Walk Noise Modifier";
    return step.radius * quiet * number(custom, modifier, 1.0f);
}

void MapScreen::updateNoises(float seconds) {
    // Each heard for its age, then gone (0x519560).
    for (Noise &n : _noises)
        n.left -= seconds;
    std::erase_if(_noises, [](const Noise &n) { return n.left <= 0.0f; });
}

bool MapScreen::hears(const Enemy &e, float &x, float &y) const {
    // The newest noise it's inside, nearer up and down by the view's
    // foreshortening (0x519440); walls don't stop it (inferred).
    if (readText(e.ai, "Investigate Sounds") != "1")
        return false;
    for (const Noise &n : _noises) {
        const float dx = e.x - n.x;
        const float dy = (e.y - n.y) * kHearingSquash;
        if (dx * dx + dy * dy < n.radius * n.radius) {
            x = n.x;
            y = n.y;
            return true;
        }
    }
    return false;
}

void MapScreen::investigate(Enemy &e, float x, float y) {
    const bool already = e.state == "Investigate";
    e.heardX = x;
    e.heardY = y;
    e.route =
        _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{x, y}} : _wayPoints.path(e.x, e.y, x, y);
    e.looked = -1.0f;
    const TextObject *state = e.ai->object("Investigate");
    if (state)
        forget(e, state->object("Investigate AI"));
    if (already)
        return;
    setState(e, "Investigate");
}

bool MapScreen::runInvestigate(Enemy &e, float seconds, const char *stateName, const char *aiKey) {
    // A CChaseInvestigateAI goes there by the waypoints (Do Path Finding),
    // running with Run To Investigate, then runs its own Investigate AI
    // (looking about) for Time To Investigate (inferred: the AI's code
    // isn't traced). Others turn to the noise and wait.
    const TextObject *state = e.ai->object(stateName);
    const TextObject *ai = state ? state->object(aiKey) : nullptr;
    const bool chases = ai && ai->className == "CChaseInvestigateAI";
    const float time = chases ? number(ai, "Time To Investigate", kLookAbout) : kLookAbout;
    if (e.looked < 0.0f) {
        if (!chases) {
            e.route.clear();
            e.looked = 0.0f;
        }
        const bool running = chases && readText(ai, "Run To Investigate") == "1";
        while (!e.route.empty()) {
            const auto [x, y] = e.route.front();
            if (!walkTo(e, x, y, 1.0f, seconds, running))
                return false;
            e.route.erase(e.route.begin());
        }
        e.looked = 0.0f;
    }
    e.looked += seconds;
    if (chases) {
        const TextObject *look = ai->object("Investigate AI");
        if (look && look->object("AI"))
            look = look->object("AI");
        runAI(e, look, seconds);
    } else {
        const float toward = std::atan2(e.heardY - e.y, -(e.heardX - e.x));
        if (std::hypot(e.heardX - e.x, e.heardY - e.y) > 1.0f) {
            float a = toward - e.angle;
            while (a > kPi)
                a -= 2 * kPi;
            while (a < -kPi)
                a += 2 * kPi;
            e.angle += std::clamp(a, -kPi * seconds, kPi * seconds);
        }
    }
    return e.looked >= time;
}

void MapScreen::drawNoises(Surface &view, int left, int top) const {
    // Each a ring growing to its radius over its age by a quarter sine
    // (0x5197c0), squashed up and down as it's heard (inferred for how
    // it's drawn: its colour and the squash).
    if (!_listening)
        return;
    for (const Noise &n : _noises) {
        const float grown = n.radius * std::sin((n.age - n.left) / n.age * kPi / 2.0f);
        if (grown < 1.0f)
            continue;
        const int steps = std::max(16, int(2.0f * kPi * grown));
        for (int k = 0; k < steps; ++k) {
            const float a = 2.0f * kPi * float(k) / float(steps);
            const int x = int(std::lround(n.x + grown * std::cos(a))) - left;
            const int y = int(std::lround(n.y + grown / kHearingSquash * std::sin(a))) - top;
            if (x < 0 || y < 0 || x >= view.w || y >= view.h)
                continue;
            uint8_t *p = view.row(y) + x * 4;
            p[0] = 255;
            p[1] = 255;
            p[2] = 0;
        }
    }
}

} // namespace reluva::stawayteam
