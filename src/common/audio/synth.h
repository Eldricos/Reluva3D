// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/dls.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::audio {

// A software synthesizer playing DLS instruments: notes on channels (as
// MIDI's, but any number), each channel an instrument, volume, expression
// and pan; voices follow the instruments' articulations (volume envelope,
// pitch envelope, LFO). Renders 16-bit stereo. See docs/formats/dls.md.
class DlsSynth {
public:
    explicit DlsSynth(int rate) : rate_(rate) {}

    int rate() const { return rate_; }
    // A channel's instrument (null: silent), and its controls (0-127).
    void setInstrument(int channel, const DlsCollection* collection, const DlsInstrument* instrument);
    void setVolume(int channel, int volume);         // CC 7
    void setExpression(int channel, int expression); // CC 11
    void setPan(int channel, int pan);               // CC 10, 64 the middle
    void setModWheel(int channel, int value);        // CC 1
    void setPitchBend(int channel, float semitones);
    void setTranspose(int channel, int semitones);

    void noteOn(int channel, int key, int velocity);
    void noteOff(int channel, int key);
    void allNotesOff();

    // Adds `frames` stereo frames to `out` (interleaved, so 2 x frames
    // values), mixed at `gain`.
    void render(float* out, size_t frames, float gain = 1);
    size_t voices() const { return voices_.size(); }

private:
    struct Channel {
        const DlsCollection* collection = nullptr;
        const DlsInstrument* instrument = nullptr;
        float volume = 100 / 127.0f, expression = 1, pan = 0.5f, modWheel = 0, bend = 0;
        int transpose = 0;
    };
    // An envelope's times (seconds) and sustain (0-1).
    struct Envelope {
        float delay = 0, attack = 0, hold = 0, decay = 0, sustain = 1, release = 0;
    };
    struct Articulation {
        Envelope volume, pitch;
        float pitchEnvelopeCents = 0;
        float lfoFrequency = 5, lfoDelay = 0, lfoPitchCents = 0, lfoGainDb = 0, lfoModPitchCents = 0;
        float keyTrack = 100; // cents per key
        float tuneCents = 0;
        float gainDb = 0, pan = 0; // pan -0.5..0.5
    };
    struct Voice {
        int channel = 0, key = 0;
        const DlsWave* wave = nullptr;
        DlsSampleInfo info;
        Articulation art;
        double position = 0;
        float baseCents = 0; // from the key and tuning
        float velocityGain = 1;
        float time = 0; // since the note began
        float releasedAt = -1;
        float releaseLevel = 0;
        float level = 0; // the volume envelope's last level
        bool done = false;
    };
    static Articulation articulate(const std::vector<DlsConnection>& instrument,
                                   const std::vector<DlsConnection>& region);
    static float envelopeLevel(const Envelope& e, float time, float releasedAt, float releaseLevel, bool decibels);

    int rate_;
    std::array<Channel, 64> channels_{};
    std::vector<Voice> voices_;
};

} // namespace reluva::audio
