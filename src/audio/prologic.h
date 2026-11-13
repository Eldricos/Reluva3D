// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// A model of a Dolby Surround / Pro Logic decoder, written from the public
// descriptions of the matrix (no decoder's code): two channels in (Lt, Rt),
// four out (left, centre, right, surround).
//
// - The passive matrix: L = Lt, R = Rt, C = (Lt + Rt) / sqrt 2,
//   S = (Lt - Rt) / sqrt 2.
// - Active steering, as Pro Logic does it: the envelopes of Lt, Rt, Lt+Rt
//   and Lt-Rt (their power, smoothed) give the direction of the strongest
//   sound and how much it dominates. The decoder then takes that sound out
//   of the speakers it leaks into and pans it between the two speakers next
//   to its direction, so a centre sound leaves left and right and a
//   surround one (Lt = -Rt) leaves the front. The rest of the signal stays
//   passive. The envelopes follow slowly (`slowMs`) and speed up while one
//   direction clearly dominates (`fastMs`).
// - The surround is low-passed (7 kHz, second-order Butterworth) and
//   delayed (20 ms by default; 15 to 30 in the descriptions), so the front
//   is heard first and its leak into the rear isn't localised there.
// - Left out: the surround's modified Dolby B noise reduction. Encoders
//   apply it before the matrix and decoders undo it; a source that never
//   applied it (a game's own mixer, which puts sounds behind into L-R
//   directly) would only be dulled by the decoder's half of it.
//   Also left out: input auto-balance and the centre modes (phantom, wide):
//   a 4.0 output folds the centre into left and right.

#include <cstdint>
#include <vector>

namespace reluva::audio {

struct ProLogicOptions {
    bool active = true;          // false: the passive matrix alone
    double surroundDelayMs = 20; // the surround's delay
    double surroundCutoffHz = 7000;
    double fastMs = 10;  // the envelopes' time constant while one direction dominates
    double slowMs = 200; // ... and otherwise
    // Steering starts at this dominance (0 none, 1 one sound alone) and is
    // full at `fullDominance`.
    double minDominance = 0.2;
    double fullDominance = 0.8;
};

class ProLogicDecoder {
public:
    enum Output { kL = 0, kC = 1, kR = 2, kS = 3 };

    explicit ProLogicDecoder(int rate, const ProLogicOptions &options = {});
    void reset();

    // `frames` stereo frames in (interleaved, -1..1), four out per frame
    // (L, C, R, S).
    void process(const float *in, float *out, int frames);
    void process(const int16_t *in, float *out, int frames);

    // The steering at the last frame: the dominant direction on the matrix's
    // circle in degrees (0 left, 45 centre, 90 right, 135 surround, 180 left
    // again), its dominance (0..1) and the steering gain applied (0..1).
    double direction() const { return _direction; }
    double dominance() const { return _dominance; }
    double steering() const { return _steering; }

    const ProLogicOptions &options() const { return _options; }
    int rate() const { return _rate; }

private:
    void frame(float lt, float rt, float *out);

    ProLogicOptions _options;
    int _rate;
    double _fastCoef;
    double _slowCoef;
    // The power of Lt, Rt, Lt+Rt and Lt-Rt: followed fast, and slowly
    // (faster while the fast ones show one direction dominating).
    double _fast[4] = {};
    double _slow[4] = {};
    double _direction = 45;
    double _dominance = 0;
    double _steering = 0;
    // The surround's low-pass (direct form I) and delay line.
    double _b0 = 1;
    double _b1 = 0;
    double _b2 = 0;
    double _a1 = 0;
    double _a2 = 0;
    double _x1 = 0;
    double _x2 = 0;
    double _y1 = 0;
    double _y2 = 0;
    std::vector<float> _delay;
    size_t _delayPos = 0;
};

// Lays decoded L, C, R, S frames out for a device of `channels` channels
// (SDL's orders), as 16-bit samples:
// - 2: a headphone fold-down: the centre to both ears at -3 dB, the
//   surround (already delayed) to both at -6 dB;
// - 4 (FL FR BL BR): the centre folded into the front at -3 dB, the
//   surround to both rear speakers at -3 dB;
// - 5 (FL FR LFE BL BR) as 4, the LFE silent;
// - 6 or more (FL FR FC LFE BL BR ...): as 4 with the centre on its own,
//   the LFE and any further channels silent;
// - 1: everything but the surround, summed.
void layOutChannels(const float *lcrs, int frames, int channels, int16_t *out);

} // namespace reluva::audio
