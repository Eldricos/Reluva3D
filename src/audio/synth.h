// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/dls.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::audio {

/**
 * A software synthesizer playing DLS instruments: notes on channels (as
 * MIDI's, but 64 of them), each channel an instrument, volume, expression
 * and pan; voices follow the instruments' articulations (volume envelope,
 * pitch envelope, LFO). Renders stereo, as floats where 1 is a 16-bit
 * sample's full scale. See docs/common/audio/dls.md.
 *
 * A channel outside 0-63 is ignored.
 */
class DlsSynth {
public:
    /** A synthesizer rendering `rate` frames a second. */
    explicit DlsSynth(int rate) : _rate(rate) {}

    /** The frames a second it renders. */
    int rate() const { return _rate; }
    /**
     * A channel's instrument (null: silent), and its controls (0-127, clamped;
     * volume counts squared, as a gain).
     */
    ///@{
    void setInstrument(int channel, const DlsCollection *collection, const DlsInstrument *instrument);
    void setVolume(int channel, int volume);         // CC 7
    void setExpression(int channel, int expression); // CC 11
    void setPan(int channel, int pan);               // CC 10, 64 the middle
    void setModWheel(int channel, int value);        // CC 1
    ///@}
    /** A channel's pitch bend and transposition, in semitones. */
    ///@{
    void setPitchBend(int channel, float semitones);
    void setTranspose(int channel, int semitones);
    ///@}
    /** CC 64: while down, notes let go of keep sounding until it's let up. */
    void setSustain(int channel, bool down);

    /**
     * Starts a note: a voice from the first of the instrument's regions
     * that takes the key and velocity, if one does. A velocity of 0 starts
     * nothing.
     */
    void noteOn(int channel, int key, int velocity);
    /** Lets a channel's notes of that key go, each fading by its release. */
    void noteOff(int channel, int key);
    /** Every voice, cut at once. */
    void allNotesOff();
    /** Lets every note go, each fading by its release, the sustain pedals let up. */
    void releaseAll();

    /**
     * Adds `frames` stereo frames to `out` (interleaved, so 2 x frames
     * values), mixed at `gain`.
     */
    void render(float *out, size_t frames, float gain = 1);
    /** How many voices are sounding. */
    size_t voices() const { return _voices.size(); }

private:
    struct Channel {
        const DlsCollection *collection = nullptr;
        const DlsInstrument *instrument = nullptr;
        float volume = 100 / 127.0f;
        float expression = 1;
        float pan = 0.5f;
        float modWheel = 0;
        float bend = 0;
        int transpose = 0;
        bool sustain = false;
    };
    // An envelope's times (seconds) and sustain (0-1).
    struct Envelope {
        float delay = 0;
        float attack = 0;
        float hold = 0;
        float decay = 0;
        float sustain = 1;
        float release = 0;
    };
    struct Articulation {
        Envelope volume;
        Envelope pitch;
        float pitchEnvelopeCents = 0;
        float lfoFrequency = 5;
        float lfoDelay = 0;
        float lfoPitchCents = 0;
        float lfoGainDb = 0;
        float lfoModPitchCents = 0;
        float keyTrack = 100; // cents per key
        float tuneCents = 0;
        float gainDb = 0; // negative: quieter
        float pan = 0;    // -0.5..0.5
    };
    struct Voice {
        int channel = 0;
        int key = 0;
        const DlsWave *wave = nullptr;
        DlsSampleInfo info;
        Articulation art;
        double position = 0;
        float baseCents = 0; // from the key and tuning
        float velocityGain = 1;
        float time = 0; // since the note began
        float releasedAt = -1;
        float releaseLevel = 0;
        bool held = false; // let go of under the sustain pedal
        float level = 0;   // the volume envelope's last level
        bool done = false;
    };
    static Articulation articulate(const std::vector<DlsConnection> &instrument,
                                   const std::vector<DlsConnection> &region);
    static float envelopeLevel(const Envelope &e, float time, float releasedAt, float releaseLevel, bool decibels);

    int _rate;
    std::array<Channel, 64> _channels{};
    std::vector<Voice> _voices;
};

} // namespace reluva::audio
