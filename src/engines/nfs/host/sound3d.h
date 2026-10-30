// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::nfs {

// DirectSound 3D's model of a sound and its listener, as
// IDirectSound3DBuffer and IDirectSound3DListener set them (DS3DBUFFER and
// DS3DLISTENER, Microsoft's published structures). DirectSound's
// coordinates are left-handed: x to the right, y up, z ahead.
struct Ds3dSource {
    float position[3] = {0, 0, 0}, velocity[3] = {0, 0, 0};
    // The cone: inside and outside angles in degrees, which way it points,
    // and the attenuation outside it (hundredths of a decibel, 0 down to
    // -10000).
    uint32_t insideCone = 360, outsideCone = 360;
    float coneOrientation[3] = {0, 0, 1};
    int32_t coneOutsideVolume = 0;
    float minDistance = 1, maxDistance = 1e9f;
    // DS3DMODE_NORMAL 0, HEADRELATIVE 1 (the position is the listener's
    // own), DISABLE 2 (no 3D: played as it is).
    uint32_t mode = 0;
};

struct Ds3dListener {
    float position[3] = {0, 0, 0}, velocity[3] = {0, 0, 0};
    float front[3] = {0, 0, 1}, top[3] = {0, 1, 0};
    // Metres a unit; how fast it falls off, and how strong the Doppler
    // shift is, each as many times the real world's (1 is real).
    float distanceFactor = 1, rolloffFactor = 1, dopplerFactor = 1;
};

// What a source comes to at the listener: a gain for each speaker, the
// ratio its frequency is shifted by, and how far behind the listener it is
// (0 beside or ahead, 1 straight behind), for the rear's muffling.
struct Ds3dResult {
    double left = 1, right = 1, doppler = 1;
    double behind = 0;
};

// Behind the listener, as a head would hear it: turned down by up to 3 dB
// and muffled, through a one-pole low-pass whose cut-off falls from 20 kHz
// beside to 4 kHz straight behind. DirectSound leaves front and back to
// the card (its documentation describes no rule); this is Reluva's, so
// that a sound behind doesn't sound the same as one ahead.
double rearGain(double behind);
double rearCutoff(double behind);
// The low-pass's coefficient for a cut-off at a sample rate:
// y += a * (x - y).
double lowPassCoefficient(double cutoff, double rate);

// The model, worked out from DirectSound's documentation of it:
// - distance: full volume up to the minimum distance; past it, the gain is
//   min / (min + rolloff * (d - min)) (6 dB a doubling of the distance at
//   rolloff 1), no quieter than it is at the maximum distance;
// - the cone: full volume inside, the outside volume outside, in decibels
//   linearly between;
// - pan: from where the source is, side to side, as seen by the listener.
//   The nearer speaker gets it all; the farther one is turned down by up
//   to 20 dB, for a sound straight to one side;
// - Doppler: f * (c + v_listener) / (c + v_source), the velocities each
//   along the line from the listener to the source, c 343.3 m/s; within
//   half to twice the frequency;
// - behind: rearGain on both speakers (and `behind` for the muffling).
Ds3dResult spatialize(const Ds3dListener& listener, const Ds3dSource& source);

} // namespace reluva::nfs
