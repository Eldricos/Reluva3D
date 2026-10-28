// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/synth.h"

#include <algorithm>
#include <cmath>

namespace reluva::audio {

namespace {

// DLS connection sources and destinations (docs/formats/dls.md).
enum : uint16_t {
    kSrcNone = 0,
    kSrcLfo = 1,
    kSrcVelocity = 2,
    kSrcKey = 3,
    kSrcEg1 = 4,
    kSrcEg2 = 5,
    kSrcModWheel = 0x81,
};
enum : uint16_t {
    kDstAttenuation = 1,
    kDstPitch = 3,
    kDstPan = 4,
    kDstLfoFrequency = 0x104,
    kDstLfoDelay = 0x105,
    kDstEg1Attack = 0x206,
    kDstEg1Decay = 0x207,
    kDstEg1Release = 0x209,
    kDstEg1Sustain = 0x20a,
    kDstEg2Attack = 0x30a,
    kDstEg2Decay = 0x30b,
    kDstEg2Release = 0x30d,
    kDstEg2Sustain = 0x30e,
};

constexpr int32_t kNoTime = int32_t(0x80000000); // a time of 0
constexpr float kSilentDb = -96;
constexpr float kShortest = 0.004f; // seconds: no envelope stage shorter (no clicks)

// Absolute time cents (65536ths) to seconds.
float seconds(int32_t timeCents) {
    if (timeCents == kNoTime)
        return 0;
    return std::pow(2.0f, float(timeCents) / 65536.0f / 1200.0f);
}

float dbToGain(float db) { return db <= kSilentDb ? 0 : std::pow(10.0f, db / 20); }

} // namespace

DlsSynth::Articulation DlsSynth::articulate(const std::vector<DlsConnection>& instrument,
                                            const std::vector<DlsConnection>& region) {
    Articulation a;
    // The instrument's, then the region's over them.
    for (const std::vector<DlsConnection>* list : {&instrument, &region})
        for (const DlsConnection& c : *list) {
            const float v = float(c.scale) / 65536.0f;
            if (c.source == kSrcNone && c.control == kSrcNone) {
                switch (c.destination) {
                case kDstAttenuation: a.gainDb = float(c.scale) / 655360.0f; break; // negative: quieter
                case kDstPan: a.pan = v / 1000; break;
                case kDstPitch: a.tuneCents = v; break;
                case kDstLfoFrequency: a.lfoFrequency = 440 * std::pow(2.0f, (v - 6900) / 1200); break;
                case kDstLfoDelay: a.lfoDelay = seconds(c.scale); break;
                case kDstEg1Attack: a.volume.attack = seconds(c.scale); break;
                case kDstEg1Decay: a.volume.decay = seconds(c.scale); break;
                case kDstEg1Release: a.volume.release = seconds(c.scale); break;
                case kDstEg1Sustain: a.volume.sustain = std::clamp(v / 1000, 0.0f, 1.0f); break;
                case kDstEg2Attack: a.pitch.attack = seconds(c.scale); break;
                case kDstEg2Decay: a.pitch.decay = seconds(c.scale); break;
                case kDstEg2Release: a.pitch.release = seconds(c.scale); break;
                case kDstEg2Sustain: a.pitch.sustain = std::clamp(v / 1000, 0.0f, 1.0f); break;
                default: break;
                }
            } else if (c.source == kSrcLfo && c.destination == kDstPitch) {
                if (c.control == kSrcModWheel)
                    a.lfoModPitchCents = v;
                else
                    a.lfoPitchCents = v;
            } else if (c.source == kSrcLfo && c.destination == kDstAttenuation) {
                a.lfoGainDb = float(c.scale) / 655360.0f;
            } else if (c.source == kSrcKey && c.destination == kDstPitch) {
                a.keyTrack = v / 128; // cents over the 128 keys
            } else if (c.source == kSrcEg2 && c.destination == kDstPitch) {
                a.pitchEnvelopeCents = v;
            }
        }
    return a;
}

float DlsSynth::envelopeLevel(const Envelope& e, float t, float releasedAt, float releaseLevel, bool decibels) {
    // Attack rises evenly; decay falls (in decibels for volume, evenly
    // for pitch) over its time from full towards nothing, holding at the
    // sustain level; release falls the same from where it was. A volume
    // sustain is a share of the 96 dB range (DLS 1: 50% is -48 dB).
    auto fall = [&](float from, float elapsed, float span) {
        if (span <= 0)
            return 0.0f;
        if (decibels) {
            const float db = (from > 0 ? 20 * std::log10(from) : kSilentDb) + kSilentDb * elapsed / span;
            return dbToGain(db);
        }
        return std::max(0.0f, from - elapsed / span);
    };
    if (releasedAt >= 0) {
        const float span = std::max(kShortest, e.release);
        return fall(releaseLevel, t - releasedAt, span);
    }
    t -= e.delay;
    if (t < 0)
        return 0;
    const float attack = std::max(decibels ? kShortest : 0.0f, e.attack);
    if (t < attack)
        return t / attack;
    t -= attack + e.hold;
    if (t < 0)
        return 1;
    const float sustain = decibels ? dbToGain(kSilentDb * (1 - e.sustain)) : e.sustain;
    if (e.decay <= 0)
        return sustain;
    return std::max(sustain, fall(1, t, e.decay));
}

void DlsSynth::setInstrument(int channel, const DlsCollection* collection, const DlsInstrument* instrument) {
    if (channel < 0 || size_t(channel) >= channels_.size())
        return;
    channels_[size_t(channel)].collection = collection;
    channels_[size_t(channel)].instrument = instrument;
}

void DlsSynth::setVolume(int channel, int volume) {
    if (channel >= 0 && size_t(channel) < channels_.size())
        channels_[size_t(channel)].volume = float(std::clamp(volume, 0, 127)) / 127;
}

void DlsSynth::setExpression(int channel, int expression) {
    if (channel >= 0 && size_t(channel) < channels_.size())
        channels_[size_t(channel)].expression = float(std::clamp(expression, 0, 127)) / 127;
}

void DlsSynth::setPan(int channel, int pan) {
    if (channel >= 0 && size_t(channel) < channels_.size())
        channels_[size_t(channel)].pan = float(std::clamp(pan, 0, 127)) / 127;
}

void DlsSynth::setModWheel(int channel, int value) {
    if (channel >= 0 && size_t(channel) < channels_.size())
        channels_[size_t(channel)].modWheel = float(std::clamp(value, 0, 127)) / 127;
}

void DlsSynth::setPitchBend(int channel, float semitones) {
    if (channel >= 0 && size_t(channel) < channels_.size())
        channels_[size_t(channel)].bend = semitones;
}

void DlsSynth::setTranspose(int channel, int semitones) {
    if (channel >= 0 && size_t(channel) < channels_.size())
        channels_[size_t(channel)].transpose = semitones;
}

void DlsSynth::noteOn(int channel, int key, int velocity) {
    if (channel < 0 || size_t(channel) >= channels_.size() || velocity <= 0)
        return;
    const Channel& ch = channels_[size_t(channel)];
    if (!ch.instrument || !ch.collection)
        return;
    key = std::clamp(key + ch.transpose, 0, 127);
    for (const DlsRegion& r : ch.instrument->regions) {
        // Level 1 collections leave the velocity range 0-0: any.
        const bool anyVelocity = r.velocityHigh == 0;
        if (key < r.keyLow || key > r.keyHigh ||
            (!anyVelocity && (velocity < r.velocityLow || velocity > r.velocityHigh)))
            continue;
        const DlsWave& wave = ch.collection->waves[r.wave];
        if (wave.samples.empty() || wave.rate <= 0)
            continue;
        // A region in a key group stops the group's other notes (a hi-hat
        // closing over an open one).
        if (r.keyGroup)
            for (Voice& v : voices_)
                if (v.channel == channel && v.releasedAt < 0 && v.key != key)
                    for (const DlsRegion& o : ch.instrument->regions)
                        if (o.keyGroup == r.keyGroup && v.key >= o.keyLow && v.key <= o.keyHigh) {
                            v.releasedAt = v.time;
                            v.releaseLevel = v.level;
                            v.art.volume.release = kShortest;
                        }
        Voice v;
        v.channel = channel;
        v.key = key;
        v.wave = &wave;
        v.info = r.hasInfo ? r.info : wave.info;
        v.art = articulate(ch.instrument->articulation, r.articulation);
        v.baseCents = v.art.keyTrack * float(key - v.info.unityNote) + float(v.info.fineTune) + v.art.tuneCents;
        // Velocity: its square, as gain (DLS's default curve).
        const float vel = float(velocity) / 127;
        v.velocityGain = vel * vel * dbToGain(v.art.gainDb + float(v.info.gain) / 655360.0f);
        voices_.push_back(v);
        break;
    }
}

void DlsSynth::noteOff(int channel, int key) {
    if (channel < 0 || size_t(channel) >= channels_.size())
        return;
    const Channel& ch = channels_[size_t(channel)];
    key = std::clamp(key + ch.transpose, 0, 127);
    for (Voice& v : voices_)
        if (v.channel == channel && v.key == key && v.releasedAt < 0 && !v.held) {
            if (ch.sustain) {
                v.held = true;
                continue;
            }
            v.releasedAt = v.time;
            v.releaseLevel = v.level;
        }
}

void DlsSynth::setSustain(int channel, bool down) {
    if (channel < 0 || size_t(channel) >= channels_.size())
        return;
    channels_[size_t(channel)].sustain = down;
    if (down)
        return;
    for (Voice& v : voices_)
        if (v.channel == channel && v.held) {
            v.held = false;
            v.releasedAt = v.time;
            v.releaseLevel = v.level;
        }
}

void DlsSynth::allNotesOff() { voices_.clear(); }

void DlsSynth::releaseAll() {
    for (Voice& v : voices_)
        if (v.releasedAt < 0) {
            v.held = false;
            v.releasedAt = v.time;
            v.releaseLevel = v.level;
        }
    for (Channel& c : channels_)
        c.sustain = false;
}

void DlsSynth::render(float* out, size_t frames, float gain) {
    constexpr size_t kBlock = 32; // controls (envelopes, LFO) worked out this often
    const float dt = 1.0f / float(rate_);
    for (Voice& v : voices_) {
        const Channel& ch = channels_[size_t(v.channel)];
        const std::vector<int16_t>& s = v.wave->samples;
        // A loop running past the wave's end (MoogBass: from 3,455 for
        // 3,456 of 3,456 samples) is taken as ending at it, as long.
        if (v.info.looped && double(v.info.loopStart) + double(v.info.loopLength) > double(s.size())) {
            v.info.loopLength = std::min<uint32_t>(v.info.loopLength, uint32_t(s.size()));
            v.info.loopStart = uint32_t(s.size()) - v.info.loopLength;
        }
        const double loopEnd = double(v.info.loopStart) + double(v.info.loopLength);
        const bool loops = v.info.looped && v.info.loopLength > 1;
        for (size_t at = 0; at < frames && !v.done; at += kBlock) {
            const size_t n = std::min(kBlock, frames - at);
            const float level = envelopeLevel(v.art.volume, v.time, v.releasedAt, v.releaseLevel, true);
            if (v.releasedAt < 0)
                v.level = level;
            if (v.releasedAt >= 0 && level <= 0.0001f) {
                v.done = true;
                break;
            }
            float cents = v.baseCents + ch.bend * 100;
            cents += v.art.pitchEnvelopeCents *
                     envelopeLevel(v.art.pitch, v.time, v.releasedAt, v.releasedAt >= 0 ? 1.0f : 0.0f, false);
            float lfoGain = 1;
            if (v.time > v.art.lfoDelay) {
                const float lfo = std::sin(6.2831853f * v.art.lfoFrequency * (v.time - v.art.lfoDelay));
                cents += lfo * (v.art.lfoPitchCents + v.art.lfoModPitchCents * ch.modWheel);
                lfoGain = dbToGain(lfo * v.art.lfoGainDb);
            }
            const double step = double(v.wave->rate) / double(rate_) * std::pow(2.0, double(cents) / 1200.0);
            const float amp = level * lfoGain * v.velocityGain * ch.volume * ch.volume * ch.expression * gain;
            const float pan = std::clamp(ch.pan + v.art.pan, 0.0f, 1.0f);
            const float left = amp * std::cos(pan * 1.5707963f), right = amp * std::sin(pan * 1.5707963f);
            for (size_t i = 0; i < n; ++i) {
                if (loops)
                    while (v.position >= loopEnd)
                        v.position -= double(v.info.loopLength);
                const size_t p = size_t(v.position);
                if (p + 1 >= s.size() && !loops) {
                    v.done = true;
                    break;
                }
                const size_t q = loops && double(p + 1) >= loopEnd ? size_t(v.info.loopStart) : p + 1;
                const float f = float(v.position - double(p));
                const float sample = (float(s[p]) + (float(s[std::min(q, s.size() - 1)]) - float(s[p])) * f) / 32768;
                out[2 * (at + i)] += sample * left;
                out[2 * (at + i) + 1] += sample * right;
                v.position += step;
            }
            v.time += float(n) * dt;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.done; }), voices_.end());
}

} // namespace reluva::audio
