// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/formats/cockpit.h"

#include <cmath>
#include <cstring>

namespace reluva::nfs {

Error decodeForGlide(const Shape &shape, image::Image &out, bool fourFour) {
    if (Error err = decodeShape(shape, out))
        return err;
    if (shape.type != kShape32Bit)
        return {};
    bool partial = false; // 0x4d1950: an alpha other than 0 and 255
    for (size_t i = 3; i < out.rgba.size() && !partial; i += 4)
        partial = out.rgba[i] != 0 && out.rgba[i] != 255;
    if (fourFour || partial)
        for (uint8_t &c : out.rgba)
            c = static_cast<uint8_t>((c >> 4) * 17);
    return {};
}

bool readCockpit(const Shape &shape, Cockpit &out) {
    for (const Shape::Block &b : shape.unknown) {
        // A u32 count, then (x, y) pairs; the game takes 12 pairs whatever
        // the count says (0x488c41).
        if (b.type != kGauges || b.data.size() < 4 + 24 * 4)
            continue;
        float v[24];
        for (int i = 0; i < 24; ++i) {
            int32_t n;
            std::memcpy(&n, b.data.data() + 4 + 4 * i, 4);
            v[i] = static_cast<float>(n);
        }
        out.colorX = static_cast<int>(v[0]);
        out.colorY = static_cast<int>(v[1]);
        out.width = v[2];
        auto gauge = [&](const float *g, Gauge &to) {
            to = {g[0], g[1], g[2], g[3] != 0, g[4], g[5], g[6], g[7], g[8], g[9]};
        };
        gauge(v + 4, out.speedometer);
        gauge(v + 14, out.tachometer);
        return true;
    }
    return false;
}

Needle needle(const Gauge &g, float width, float reading) {
    // The two ends' directions in turns, each in [0, 1).
    auto turns = [](float dx, float dy) {
        const float a = std::atan2(dy, dx) / 6.28318531f;
        return a < 0 ? a + 1 : a;
    };
    float a0 = turns(g.p0x - g.centerX, g.p0y - g.centerY);
    float a1 = turns(g.p1x - g.centerX, g.p1y - g.centerY);
    if (g.low < g.high) {
        if (a1 < a0) // y grows downward, so a growing angle turns clockwise
            a1 += 1;
    } else if (a0 < a1) {
        a0 += 1;
    }
    const float scale = g.high != g.low ? (a1 - a0) / (g.high - g.low) : 0.0f;
    const float a = (reading * scale + (a1 - g.high * scale)) * 6.28318531f;
    const float length =
        (std::hypot(g.p0x - g.centerX, g.p0y - g.centerY) + std::hypot(g.p1x - g.centerX, g.p1y - g.centerY)) / 2;
    const float ux = std::cos(a);
    const float uy = std::sin(a);
    const float nx = -uy;
    const float ny = ux;
    const float bx = g.centerX + ux * g.hub;
    const float by = g.centerY + uy * g.hub;
    const float tx = g.centerX + ux * length;
    const float ty = g.centerY + uy * length;
    const float base = width / 2;
    const float tip = width / 4;
    Needle n;
    n.x[0] = bx + nx * base, n.y[0] = by + ny * base;
    n.x[1] = bx - nx * base, n.y[1] = by - ny * base;
    n.x[2] = tx - nx * tip, n.y[2] = ty - ny * tip;
    n.x[3] = tx + nx * tip, n.y[3] = ty + ny * tip;
    return n;
}

float wheelTurns(int steering, float speed) {
    // 2.2369 turns metres a second into mph; 0.00125 is 1/800.
    return steering * (0.5f - std::fabs(speed) * 2.2369f * 0.00125f) / 256;
}

float metresPerSecond(const Gauge &speedometer, float reading) {
    return reading * (speedometer.kmh ? 1 / 3.6f : 0.44704726f);
}

} // namespace reluva::nfs
