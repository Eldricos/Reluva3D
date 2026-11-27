// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The interface's buttons on the map: time stopped, orders cancelled, and
// an enemy's sight shown (docs/engines/stawayteam/systems/maps.md#the-interface).

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// An enemy's sight is tested and tinted in squares this wide (inferred).
constexpr int kVisionStep = 6;
// Its tint, and how much of it shows (inferred).
constexpr uint8_t kVisionTint[3] = {255, 64, 32};
constexpr int kVisionAmount = 80; // of 255
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
    // Where the enemy shown can see: in its Max Distance, half its Vision
    // Cone either side of its facing, and in sight past what stands in
    // the way, as it sees the team; tinted over the ground (inferred for
    // how it's drawn).
    if (_vision != 2 || _visionEnemy < 0)
        return;
    const Enemy &e = _enemies[size_t(_visionEnemy)];
    if (!e.alive() || !shown(e.part))
        return;
    const float reach = e.ai ? float(e.ai->number("Max Distance").value_or(kSightDefault)) : kSightDefault;
    const float half = (e.ai ? float(e.ai->number("Vision Cone").value_or(125.0)) : 125.0f) / 2.0f * kPi / 180.0f;
    const int x0 = std::max(0, int(e.x - reach) - left);
    const int x1 = std::min(view.w, int(e.x + reach) - left + 1);
    const int y0 = std::max(0, int(e.y - reach) - top);
    const int y1 = std::min(view.h, int(e.y + reach) - top + 1);
    for (int by = y0; by < y1; by += kVisionStep)
        for (int bx = x0; bx < x1; bx += kVisionStep) {
            const float wx = float(left + bx) + float(kVisionStep) / 2.0f;
            const float wy = float(top + by) + float(kVisionStep) / 2.0f;
            const float dx = wx - e.x;
            const float dy = wy - e.y;
            if (std::hypot(dx, dy) > reach)
                continue;
            float off = std::atan2(dy, -dx) - e.angle;
            while (off > kPi)
                off -= 2 * kPi;
            while (off < -kPi)
                off += 2 * kPi;
            if (std::abs(off) > half || !clearSight(e.x, e.y, wx, wy))
                continue;
            for (int y = by; y < std::min(by + kVisionStep, view.h); ++y) {
                uint8_t *p = view.row(y) + bx * 4;
                for (int x = bx; x < std::min(bx + kVisionStep, view.w); ++x, p += 4)
                    for (int c = 0; c < 3; ++c)
                        p[c] = uint8_t((p[c] * (255 - kVisionAmount) + kVisionTint[c] * kVisionAmount) / 255);
            }
        }
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
