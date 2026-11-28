// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The team's noise, and the enemies hearing it and going to look
// (docs/engines/stawayteam/systems/maps.md).

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// Heard from as far above or below as this times less than to the side:
// the view's foreshortening (0x519465).
constexpr float kHearingSquash = 1.40299f;
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
    // foreshortening (0x519440), with nothing standing between, as for
    // its sight (0x497d40): a wall stops it. Whoever made it, any side.
    if (readText(e.ai, "Investigate Sounds") != "1")
        return false;
    for (const Noise &n : _noises) {
        const float dx = e.x - n.x;
        const float dy = (e.y - n.y) * kHearingSquash;
        if (dx * dx + dy * dy < n.radius * n.radius && clearSight(e.x, e.y, n.x, n.y)) {
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
    e.route = route(e, x, y);
    e.looked = -1.0f;
    const TextObject *state = e.ai->object("Investigate");
    if (state)
        forget(e, state->object("Investigate AI"));
    if (already)
        return;
    setState(e, "Investigate");
}

bool MapScreen::runInvestigate(Enemy &e, float seconds, const char *stateName, const char *aiKey) {
    // A CChaseInvestigateAI (0x47ea60) goes there by the waypoints (Do
    // Path Finding), running with Run To Investigate; there, turns to face
    // the place (its state 1), then runs its own Investigate AI, looking
    // about, for Time To Investigate (state 2), and is done (state 5).
    // Others turn to the noise and wait (inferred).
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
        // The turn's pace isn't traced: as the others' (inferred).
        if (chases && std::hypot(e.heardX - e.x, e.heardY - e.y) > 1.0f &&
            !turnTo(e, std::atan2(e.heardY - e.y, -(e.heardX - e.x)), 180.0f, seconds))
            return false;
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
    // Each grows to its radius over its age by a quarter sine (0x5197c0),
    // drawn in blue (0x5199b1) as two pairs of arcs, a quarter turn each
    // to its left and right, at that radius and 15/16 of it (0x626c0c),
    // squashed up and down by the map's Y to X ratio; each arc in its
    // radius / 4 straight pieces, 10 to 25 of them (0x4ff090).
    if (!_listening)
        return;
    constexpr std::array<uint8_t, 3> kBlue = {0, 0, 255};
    constexpr float kInner = 0.9375f;
    const auto arc = [&](float x, float y, float r, float from, float to) {
        const int pieces = std::clamp(int(r) / 4, 10, 25);
        float px = x + std::cos(from) * r;
        float py = y + std::sin(from) * r / kHearingSquash;
        for (int k = 1; k <= pieces; ++k) {
            const float a = from + (to - from) * float(k) / float(pieces);
            const float nx = x + std::cos(a) * r;
            const float ny = y + std::sin(a) * r / kHearingSquash;
            drawLine(view, px - float(left), py - float(top), nx - float(left), ny - float(top), kBlue, 1.0f);
            px = nx;
            py = ny;
        }
    };
    for (const Noise &n : _noises) {
        const float grown = n.radius * std::sin((n.age - n.left) / n.age * kPi / 2.0f);
        if (grown < 1.0f)
            continue;
        for (const float r : {grown, grown * kInner}) {
            arc(n.x, n.y, r, -kPi / 4.0f, kPi / 4.0f);
            arc(n.x, n.y, r, 3.0f * kPi / 4.0f, 5.0f * kPi / 4.0f);
        }
    }
}

} // namespace reluva::stawayteam
