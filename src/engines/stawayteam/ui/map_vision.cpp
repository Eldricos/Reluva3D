// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The interface's buttons on the map: time stopped, orders cancelled, and
// an enemy's sight shown (docs/engines/stawayteam/systems/maps.md#the-interface).

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// Time stopped: its words (0x5be1b2), this far under the view's top
// (0x5be1dc).
const char *const kPauseText = "Pause Time Activated";
constexpr int kPauseTextY = 0x14;

} // namespace

void MapScreen::cancelOrders() {
    // Each chosen officer's CPlayerAI drops its orders, the one under way
    // and those queued, and any process they're going through ends
    // (0x5b80e0, 0x539fe0, 0x5b8150): sent as a command (0x481b20).
    for (Officer &o : _officers)
        if (o.chosen) {
            o.path.clear();
            o.attacking = -1;
            o.task = Officer::Task::kNone;
            o.taskProcess = nullptr;
            o.taskStep = 0;
        }
}

void MapScreen::pressVision() {
    // Off, choosing an enemy, showing its sight, and round (the button's
    // states, 0x51a600). Shown with none chosen, the one in the view
    // nearest its middle (inferred).
    _vision = (_vision + 1) % 3;
    if (_vision == 2 && (_visionEnemy < 0 || !_enemies[size_t(_visionEnemy)].alive())) {
        _visionEnemy = -1;
        float best = 1e9f;
        for (size_t i = 0; i < _enemies.size(); ++i) {
            const Enemy &e = _enemies[i];
            const float d = std::hypot(e.x - _cameraX, e.y - _cameraY);
            if (e.alive() && shown(e.part) && d < best) {
                best = d;
                _visionEnemy = int(i);
            }
        }
    }
}

void MapScreen::cycleVision() {
    // The next enemy alive in the view, from the one shown (inferred).
    const auto inView = [&](const Enemy &e) {
        return e.alive() && shown(e.part) && std::abs(e.x - _cameraX) <= float(shownW()) / 2.0f &&
               std::abs(e.y - _cameraY) <= float(shownH()) / 2.0f;
    };
    const int n = int(_enemies.size());
    for (int k = 1; k <= n; ++k) {
        const int i = ((_visionEnemy < 0 ? -1 : _visionEnemy) + k) % n;
        if (inView(_enemies[size_t(i)])) {
            _visionEnemy = i;
            _vision = 2;
            return;
        }
    }
}

void MapScreen::drawVision(Surface &view, int left, int top) const {
    // The program's CEntityAnimatedViewArea (0x5b6b00): on the enemy
    // watched, facing as it does (0x49d880), its Max Distance and Vision
    // Cone from its AI (0x435360), none in Inactive, Stun or Dead, whose
    // states answer no (0x401030). The cone's cut into segments, Angle
    // Per Segment (40) of them to a turn, one more than fit (0x49d088), and
    // drawn in white lines (0x49e3b6), each point's height on the view
    // scaled by the map's Y to X ratio (0x616fbc). Each ray stops where
    // sight's blocked (inferred: the program sweeps the walls' edges into
    // its segments, 0x49cef0).
    if (_vision != 2 || _visionEnemy < 0)
        return;
    const Enemy &e = _enemies[size_t(_visionEnemy)];
    if (!e.alive() || !shown(e.part) || e.state == "Inactive" || e.state == "Stun")
        return;
    constexpr float kSegmentsPerTurn = 40.0f;
    constexpr float kYToX = 0.712766f;
    constexpr std::array<uint8_t, 3> kWhite = {255, 255, 255};
    const float reach = e.ai ? float(e.ai->number("Max Distance").value_or(kSightDefault)) : kSightDefault;
    const float cone = (e.ai ? float(e.ai->number("Vision Cone").value_or(125.0)) : 125.0f) * kPi / 180.0f;
    if (reach <= 0.0f || cone <= 0.0f)
        return;
    const int segments = int(cone * kSegmentsPerTurn / (2.0f * kPi)) + 1;
    // A ray from the enemy at `angle` (as Facing Angle: 0 left), as far as
    // it sees, on the view.
    const auto end = [&](float angle) {
        const float dx = -std::cos(angle) * reach;
        const float dy = std::sin(angle) * reach;
        float seen = 0.0f;
        float blind = 1.0f;
        if (clearSight(e.x, e.y, e.x + dx, e.y + dy)) {
            seen = 1.0f;
        } else {
            for (int k = 0; k < 12; ++k) {
                const float t = (seen + blind) / 2.0f;
                (clearSight(e.x, e.y, e.x + dx * t, e.y + dy * t) ? seen : blind) = t;
            }
        }
        return std::pair<float, float>{e.x + dx * seen - float(left), e.y + dy * seen * kYToX - float(top)};
    };
    const float cx = e.x - float(left);
    const float cy = e.y - float(top);
    auto previous = end(e.angle - cone / 2.0f);
    drawLine(view, cx, cy, previous.first, previous.second, kWhite, 1.0f);
    for (int i = 1; i <= segments; ++i) {
        const auto next = end(e.angle - cone / 2.0f + cone * float(i) / float(segments));
        drawLine(view, previous.first, previous.second, next.first, next.second, kWhite, 1.0f);
        previous = next;
    }
    drawLine(view, cx, cy, previous.first, previous.second, kWhite, 1.0f);
}

void MapScreen::drawPause(Surface &screen, uint64_t now) {
    // The Pause Indicator at the view's top left, its frames by the time,
    // and "Pause Time Activated" across the view's top, its brightness
    // going up and down (0x5be085; inferred for the words' place across
    // and the beat).
    blitBy(screen, _pauseIndicator.at(now), _viewX, _viewY);
    if (!_pauseFont.loaded())
        return;
    const std::string text = kPauseText;
    const float beat = float(now % 2000) / 2000.0f;
    const float brightness = 0.7f + 0.3f * std::sin(beat * 2.0f * kPi);
    _pauseFont.draw(screen, _viewX + (_viewW - _pauseFont.width(text)) / 2, _viewY + kPauseTextY, text, 255,
                    brightness);
}

} // namespace reluva::stawayteam
