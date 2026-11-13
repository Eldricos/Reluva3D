// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/prologic.h"

#include <algorithm>
#include <cmath>

namespace reluva::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfRoot2 = 0.70710678118654752440;

// Each speaker's place in (Lt, Rt): what the passive matrix takes for it.
constexpr double kSpeaker[4][2] = {
    {1, 0},                    // L
    {kHalfRoot2, kHalfRoot2},  // C
    {0, 1},                    // R
    {kHalfRoot2, -kHalfRoot2}, // S
};

// A one-pole smoother's coefficient for a time constant.
double coefficient(double ms, int rate) {
    return ms <= 0 ? 1.0 : 1.0 - std::exp(-1000.0 / (ms * double(rate)));
}

// How strongly one direction dominates powers (Lt, Rt, Lt+Rt, Lt-Rt), and
// its angle: twice the direction's angle is that of (Lt^2 - Rt^2, 2 Lt Rt).
void dominanceOf(const double *p, double &x, double &y, double &m) {
    x = p[0] - p[1];
    y = (p[2] - p[3]) / 2;
    const double total = p[0] + p[1];
    m = total > 1e-12 ? std::min(1.0, std::sqrt(x * x + y * y) / total) : 0.0;
}

} // namespace

ProLogicDecoder::ProLogicDecoder(int rate, const ProLogicOptions &options) : _options(options), _rate(rate) {
    _fastCoef = coefficient(_options.fastMs, _rate);
    _slowCoef = coefficient(_options.slowMs, _rate);
    // A second-order Butterworth low-pass (the bilinear transform's usual
    // form, Q = 1/sqrt 2).
    const double fc = std::min(_options.surroundCutoffHz, 0.45 * _rate);
    if (fc > 0) {
        const double w = 2 * kPi * fc / _rate;
        const double cw = std::cos(w);
        const double alpha = std::sin(w) / (2 * kHalfRoot2);
        const double a0 = 1 + alpha;
        _b0 = (1 - cw) / 2 / a0;
        _b1 = (1 - cw) / a0;
        _b2 = _b0;
        _a1 = -2 * cw / a0;
        _a2 = (1 - alpha) / a0;
    }
    _delay.assign(size_t(std::max(1L, std::lround(_options.surroundDelayMs * _rate / 1000.0) + 1)), 0.0f);
    reset();
}

void ProLogicDecoder::reset() {
    std::fill(std::begin(_fast), std::end(_fast), 0.0);
    std::fill(std::begin(_slow), std::end(_slow), 0.0);
    _direction = 45;
    _dominance = _steering = 0;
    _x1 = _x2 = _y1 = _y2 = 0;
    std::fill(_delay.begin(), _delay.end(), 0.0f);
    _delayPos = 0;
}

void ProLogicDecoder::process(const float *in, float *out, int frames) {
    for (int i = 0; i < frames; ++i)
        frame(in[2 * i], in[2 * i + 1], out + 4 * i);
}

void ProLogicDecoder::process(const int16_t *in, float *out, int frames) {
    for (int i = 0; i < frames; ++i)
        frame(in[2 * i] / 32768.0f, in[2 * i + 1] / 32768.0f, out + 4 * i);
}

void ProLogicDecoder::frame(float ltf, float rtf, float *out) {
    const double lt = ltf;
    const double rt = rtf;
    double o[4];
    for (int s = 0; s < 4; ++s)
        o[s] = kSpeaker[s][0] * lt + kSpeaker[s][1] * rt;

    if (_options.active) {
        const double power[4] = {lt * lt, rt * rt, (lt + rt) * (lt + rt), (lt - rt) * (lt - rt)};
        double x;
        double y;
        double m;
        for (int k = 0; k < 4; ++k)
            _fast[k] += _fastCoef * (power[k] - _fast[k]);
        dominanceOf(_fast, x, y, m);
        // While the fast envelopes see one direction dominate, the slow ones
        // follow at nearly their pace.
        const double c = _slowCoef + (_fastCoef - _slowCoef) * m * m;
        for (int k = 0; k < 4; ++k)
            _slow[k] += c * (power[k] - _slow[k]);
        dominanceOf(_slow, x, y, m);
        _dominance = m;
        const double span = std::max(1e-6, _options.fullDominance - _options.minDominance);
        const double g = std::clamp((m - _options.minDominance) / span, 0.0, 1.0);
        _steering = g;
        if (g > 0) {
            // The direction on the circle, 0..180 degrees: L 0, C 45, R 90,
            // S 135, L again at 180 (the same place with its sign turned).
            double theta = std::atan2(y, x) / 2;
            if (theta < 0)
                theta += kPi;
            _direction = theta * 180 / kPi;
            const double vx = std::cos(theta);
            const double vy = std::sin(theta);
            const double d = vx * lt + vy * rt; // the dominant sound
            // Panned between the two speakers beside it, at constant power.
            double pan[4] = {0, 0, 0, 0};
            const double q = std::min(theta / (kPi / 4), 3.999999);
            const int k = int(q);
            const double t = (q - k) * kPi / 2;
            pan[k] = std::cos(t);
            pan[(k + 1) % 4] += std::sin(t);
            for (int s = 0; s < 4; ++s) {
                const double along = kSpeaker[s][0] * vx + kSpeaker[s][1] * vy; // what passive gives it
                const double steered = along < 0 ? -pan[s] : pan[s];
                o[s] += g * (steered - along) * d;
            }
        }
    }

    // The surround: low-passed, then delayed.
    const double sx = o[kS];
    const double sy = _b0 * sx + _b1 * _x1 + _b2 * _x2 - _a1 * _y1 - _a2 * _y2;
    _x2 = _x1;
    _x1 = sx;
    _y2 = _y1;
    _y1 = std::abs(sy) < 1e-30 ? 0.0 : sy;
    _delay[_delayPos] = float(sy);
    _delayPos = (_delayPos + 1) % _delay.size();
    out[kL] = float(o[kL]);
    out[kC] = float(o[kC]);
    out[kR] = float(o[kR]);
    out[kS] = _delay[_delayPos]; // the oldest: delay_.size() - 1 frames ago
}

void layOutChannels(const float *lcrs, int frames, int channels, int16_t *out) {
    auto put = [](double v) {
        return static_cast<int16_t>(std::clamp<long>(std::lround(v * 32768.0), -32768L, 32767L));
    };
    for (int i = 0; i < frames; ++i) {
        const float *f = lcrs + 4 * i;
        int16_t *o = out + size_t(i) * size_t(channels);
        std::fill(o, o + channels, int16_t(0));
        const double l = f[ProLogicDecoder::kL];
        const double c = f[ProLogicDecoder::kC];
        const double r = f[ProLogicDecoder::kR];
        const double s = f[ProLogicDecoder::kS];
        if (channels == 2) {
            o[0] = put(l + kHalfRoot2 * c + 0.5 * s);
            o[1] = put(r + kHalfRoot2 * c + 0.5 * s);
        } else if (channels >= 6) {
            o[0] = put(l);
            o[1] = put(r);
            o[2] = put(c);
            o[4] = put(kHalfRoot2 * s);
            o[5] = put(kHalfRoot2 * s);
        } else if (channels >= 4) {
            o[0] = put(l + kHalfRoot2 * c);
            o[1] = put(r + kHalfRoot2 * c);
            const int rear = channels == 5 ? 3 : 2; // 4.1: FL FR LFE BL BR
            o[rear] = put(kHalfRoot2 * s);
            o[rear + 1] = put(kHalfRoot2 * s);
        } else if (channels == 1) {
            o[0] = put(kHalfRoot2 * (l + r) + c);
        }
    }
}

} // namespace reluva::audio
