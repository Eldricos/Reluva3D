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

ProLogicDecoder::ProLogicDecoder(int rate, const ProLogicOptions &options) : options_(options), rate_(rate) {
    fastCoef_ = coefficient(options_.fastMs, rate_);
    slowCoef_ = coefficient(options_.slowMs, rate_);
    // A second-order Butterworth low-pass (the bilinear transform's usual
    // form, Q = 1/sqrt 2).
    const double fc = std::min(options_.surroundCutoffHz, 0.45 * rate_);
    if (fc > 0) {
        const double w = 2 * kPi * fc / rate_, cw = std::cos(w), alpha = std::sin(w) / (2 * kHalfRoot2);
        const double a0 = 1 + alpha;
        b0_ = (1 - cw) / 2 / a0;
        b1_ = (1 - cw) / a0;
        b2_ = b0_;
        a1_ = -2 * cw / a0;
        a2_ = (1 - alpha) / a0;
    }
    delay_.assign(size_t(std::max(1L, std::lround(options_.surroundDelayMs * rate_ / 1000.0) + 1)), 0.0f);
    reset();
}

void ProLogicDecoder::reset() {
    std::fill(std::begin(fast_), std::end(fast_), 0.0);
    std::fill(std::begin(slow_), std::end(slow_), 0.0);
    direction_ = 45;
    dominance_ = steering_ = 0;
    x1_ = x2_ = y1_ = y2_ = 0;
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    delayPos_ = 0;
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
    const double lt = ltf, rt = rtf;
    double o[4];
    for (int s = 0; s < 4; ++s)
        o[s] = kSpeaker[s][0] * lt + kSpeaker[s][1] * rt;

    if (options_.active) {
        const double power[4] = {lt * lt, rt * rt, (lt + rt) * (lt + rt), (lt - rt) * (lt - rt)};
        double x, y, m;
        for (int k = 0; k < 4; ++k)
            fast_[k] += fastCoef_ * (power[k] - fast_[k]);
        dominanceOf(fast_, x, y, m);
        // While the fast envelopes see one direction dominate, the slow ones
        // follow at nearly their pace.
        const double c = slowCoef_ + (fastCoef_ - slowCoef_) * m * m;
        for (int k = 0; k < 4; ++k)
            slow_[k] += c * (power[k] - slow_[k]);
        dominanceOf(slow_, x, y, m);
        dominance_ = m;
        const double span = std::max(1e-6, options_.fullDominance - options_.minDominance);
        const double g = std::clamp((m - options_.minDominance) / span, 0.0, 1.0);
        steering_ = g;
        if (g > 0) {
            // The direction on the circle, 0..180 degrees: L 0, C 45, R 90,
            // S 135, L again at 180 (the same place with its sign turned).
            double theta = std::atan2(y, x) / 2;
            if (theta < 0)
                theta += kPi;
            direction_ = theta * 180 / kPi;
            const double vx = std::cos(theta), vy = std::sin(theta);
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
    const double sx = o[S];
    const double sy = b0_ * sx + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = sx;
    y2_ = y1_;
    y1_ = std::abs(sy) < 1e-30 ? 0.0 : sy;
    delay_[delayPos_] = float(sy);
    delayPos_ = (delayPos_ + 1) % delay_.size();
    out[L] = float(o[L]);
    out[C] = float(o[C]);
    out[R] = float(o[R]);
    out[S] = delay_[delayPos_]; // the oldest: delay_.size() - 1 frames ago
}

void layOutChannels(const float *lcrs, int frames, int channels, int16_t *out) {
    auto put = [](double v) {
        return static_cast<int16_t>(std::clamp<long>(std::lround(v * 32768.0), -32768L, 32767L));
    };
    for (int i = 0; i < frames; ++i) {
        const float *f = lcrs + 4 * i;
        int16_t *o = out + size_t(i) * size_t(channels);
        std::fill(o, o + channels, int16_t(0));
        const double l = f[ProLogicDecoder::L], c = f[ProLogicDecoder::C], r = f[ProLogicDecoder::R],
                     s = f[ProLogicDecoder::S];
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
