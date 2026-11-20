// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/dls.h"

#include <array>
#include <cstdint>
#include <vector>

namespace reluva::audio {

/**
 * A software synthesizer playing DLS instruments: notes on 64 channels (as
 * MIDI's), each channel an instrument, volume, expression and pan; voices
 * follow the instruments' articulations (volume envelope, pitch envelope,
 * LFO). Mixes into float stereo. See docs/common/audio/dls.md.
 *
 * Calls naming a channel outside 0-63 are ignored.
 */
class DlsSynth {
public:
    /** A synthesizer rendering `rate` frames a second, every channel silent. */
    explicit DlsSynth(int rate) : _rate(rate) {}

    /** Frames a second it renders. */
    int rate() const { return _rate; }
    /**
     * Sets a channel's instrument, for the notes that start after.
     *
     * The synthesizer keeps the pointers: `collection` (which holds `instrument`) has to outlive the setting
     * and the notes it plays.
     * @param instrument Null: notes on the channel are silent.
     */
    void setInstrument(int channel, const DlsCollection *collection, const DlsInstrument *instrument);
    /** Sets a channel's volume (CC 7), 0-127; 100 to begin with. */
    void setVolume(int channel, int volume);
    /** Sets a channel's expression (CC 11), 0-127; 127 to begin with. */
    void setExpression(int channel, int expression);
    /** Sets a channel's pan (CC 10), 0-127, 64 the middle. */
    void setPan(int channel, int pan);
    /** Sets a channel's modulation wheel (CC 1), 0-127: deepens the LFO's vibrato as far as the instrument says. */
    void setModWheel(int channel, int value);
    /** Bends a channel's pitch by `semitones`, up or down. */
    void setPitchBend(int channel, float semitones);
    /** Moves a channel's keys by `semitones`, for the notes it plays after. */
    void setTranspose(int channel, int semitones);
    /** Sets a channel's sustain pedal (CC 64): while down, notes let go of keep sounding until it's let up. */
    void setSustain(int channel, bool down);

    /**
     * Starts a note: the first of the instrument's regions holding the key
     * (and velocity, where it gives a range) plays.
     * @param key 0-127, before the channel's transpose.
     * @param velocity 1-127; 0 or less plays nothing.
     */
    void noteOn(int channel, int key, int velocity);
    /** Lets go of a channel's notes on `key`: each fades by its release, unless the sustain pedal is down. */
    void noteOff(int channel, int key);
    /** Cuts every note at once. */
    void allNotesOff();
    /** Lets go of every note, sustain pedals let up too, each fading by its release. */
    void releaseAll();

    /**
     * Adds `frames` stereo frames to `out` (interleaved, so 2 x frames
     * values, full scale at 1), mixed at `gain`; notes that have ended are dropped.
     */
    void render(float *out, size_t frames, float gain = 1);
    /** The number of notes sounding, fading ones included. */
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
        float gainDb = 0;
        float pan = 0; // -0.5..0.5
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
