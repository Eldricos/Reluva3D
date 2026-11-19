// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace reluva::audio {

Mixer::Mixer(int outputRate) : outputRate_(std::max(outputRate, 1)) {
}

Mixer::Channel *Mixer::find(SoundHandle handle) {
    for (Channel &c : channels_)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

const Mixer::Channel *Mixer::find(SoundHandle handle) const {
    for (const Channel &c : channels_)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

SoundHandle Mixer::play(std::shared_ptr<const Sound> sound, SoundType type, int volume, int pan, bool loop) {
    if (!sound || sound->frames() == 0 || sound->rate <= 0 || sound->channels < 1 || sound->channels > 2)
        return kNoSound;
    std::lock_guard<std::mutex> lock(mutex_);
    const SoundHandle handle = nextHandle_++;
    if (nextHandle_ == kNoSound)
        nextHandle_ = 1;
    channels_.push_back({handle, std::move(sound), nullptr, type, std::clamp(volume, 0, kMaxVolume),
                         std::clamp(pan, -kMaxPan, kMaxPan), loop});
    return handle;
}

SoundHandle Mixer::play(std::shared_ptr<Stream> stream, SoundType type, int volume, int pan) {
    if (!stream || stream->rate() <= 0)
        return kNoSound;
    std::lock_guard<std::mutex> lock(mutex_);
    const SoundHandle handle = nextHandle_++;
    if (nextHandle_ == kNoSound)
        nextHandle_ = 1;
    Channel c{
        handle, nullptr, std::move(stream), type, std::clamp(volume, 0, kMaxVolume), std::clamp(pan, -kMaxPan, kMaxPan),
        false};
    channels_.push_back(std::move(c));
    return handle;
}

void SoundStream::push(const int16_t *samples, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (read_ > 0 && read_ * 2 >= samples_.size()) { // drop what's been taken
        samples_.erase(samples_.begin(), samples_.begin() + long(read_));
        read_ = 0;
    }
    samples_.insert(samples_.end(), samples, samples + count);
}

void SoundStream::end() {
    std::lock_guard<std::mutex> lock(mutex_);
    ended_ = true;
}

size_t SoundStream::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (samples_.size() - read_) / size_t(channels_);
}

SoundHandle Mixer::playStream(std::shared_ptr<SoundStream> stream, SoundType type, int volume, int pan,
                              int prebufferMs) {
    if (!stream || stream->rate() <= 0 || stream->channels() < 1 || stream->channels() > 2)
        return kNoSound;
    std::lock_guard<std::mutex> lock(mutex_);
    const SoundHandle handle = nextHandle_++;
    if (nextHandle_ == kNoSound)
        nextHandle_ = 1;
    Channel c{handle, nullptr, nullptr, type, std::clamp(volume, 0, kMaxVolume), std::clamp(pan, -kMaxPan, kMaxPan),
              false};
    c.prebuffer = size_t(std::max(prebufferMs, 0)) * size_t(stream->rate()) / 1000;
    c.queue = std::move(stream);
    channels_.push_back(std::move(c));
    return handle;
}

void Mixer::stop(SoundHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->done = true;
}

void Mixer::stopType(SoundType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel &c : channels_)
        if (c.type == type)
            c.done = true;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel &c : channels_)
        c.done = true;
}

bool Mixer::isPlaying(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(handle) != nullptr;
}

bool Mixer::isTypePlaying(SoundType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(channels_.begin(), channels_.end(), [&](const Channel &c) { return c.type == type && !c.done; });
}

double Mixer::position(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Channel *c = find(handle);
    return c ? c->position : 0;
}

void Mixer::setVolume(SoundHandle handle, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->volume = std::clamp(volume, 0, kMaxVolume);
}

void Mixer::setPaused(SoundHandle handle, bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->paused = paused;
}

bool Mixer::isPaused(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Channel *c = find(handle);
    return c && c->paused;
}

void Mixer::setAllPaused(bool paused) {
    std::lock_guard<std::mutex> lock(mutex_);
    allPaused_ = paused;
}

bool Mixer::allPaused() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return allPaused_;
}

void Mixer::setPan(SoundHandle handle, int pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->pan = std::clamp(pan, -kMaxPan, kMaxPan);
}

void Mixer::setPitch(SoundHandle handle, double pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->pitch = std::clamp(pitch, 0.05, 8.0);
}

void Mixer::setMuffle(SoundHandle handle, float muffle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->muffle = std::clamp(muffle, 0.0f, 1.0f);
}

namespace {
constexpr double kPi = 3.14159265358979323846;

// A 2nd-order Butterworth (bilinear) low-pass or high-pass at `hz`.
void butterworth(double &a0, double &a1, double &a2, double &b1, double &b2, bool high, double hz, double rate) {
    hz = std::clamp(hz, 20.0, rate / 2 - 1);
    const double r2 = std::sqrt(2.0);
    if (!high) {
        const double c = 1 / std::tan(kPi * hz / rate);
        a0 = 1 / (1 + r2 * c + c * c);
        a1 = 2 * a0;
        a2 = a0;
        b1 = 2 * a0 * (1 - c * c);
        b2 = a0 * (1 - r2 * c + c * c);
    } else {
        const double c = std::tan(kPi * hz / rate);
        a0 = 1 / (1 + r2 * c + c * c);
        a1 = -2 * a0;
        a2 = a0;
        b1 = 2 * a0 * (c * c - 1);
        b2 = a0 * (1 - r2 * c + c * c);
    }
}
} // namespace

double Mixer::Channel::Biquad::run(int side, double x) {
    double *st = state[side];
    const double y = a0 * x + a1 * st[0] + a2 * st[1] - b1 * st[2] - b2 * st[3] + 1e-10;
    st[1] = st[0];
    st[0] = x;
    st[3] = st[2];
    st[2] = y;
    return y;
}

void Mixer::setFilter(SoundHandle handle, Filter kind, float hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    Channel *c = find(handle);
    if (!c || (c->filter == kind && c->filterHz == hz))
        return;
    if (c->filter != kind)
        c->biquad = {};
    c->filter = kind;
    c->filterHz = hz;
    if (kind != Filter::None)
        butterworth(c->biquad.a0, c->biquad.a1, c->biquad.a2, c->biquad.b1, c->biquad.b2, kind == Filter::HighPass, hz,
                    double(outputRate_));
}

// The reverb's lines at the output rate: four combs (29.7, 37.1, 41.1,
// 43.7 ms), each fed back by 0.001^(its delay / the reverb's time), so
// its echoes fall 60 dB over that; two all-passes (5 and 1.7 ms, 0.7).
void Mixer::setUpReverb(Channel &c, const Reverb &r) const {
    auto &st = c.reverb;
    const bool fresh = !st;
    if (fresh)
        st = std::make_shared<Channel::ReverbState>();
    const double rate = double(outputRate_);
    auto samples = [&](double ms) { return std::max<size_t>(1, size_t(std::lround(ms * rate / 1000))); };
    if (fresh) {
        const double combs[4] = {29.7, 37.1, 41.1, 43.7};
        for (int k = 0; k < 4; ++k)
            st->comb[k].assign(samples(combs[k]), 0);
        st->pass1.assign(samples(5.0), 0);
        st->pass2[0].assign(samples(1.7), 0);
        st->pass2[1].assign(samples(1.7), 0);
    }
    if (fresh || r.predelay != st->params.predelay) {
        st->pre.assign(std::max<size_t>(1, size_t(std::lround(std::clamp(r.predelay, 0.0f, 0.25f) * rate))), 0);
        st->preAt = 0;
    }
    if (fresh || r.dampHz != st->params.dampHz)
        butterworth(st->damp.a0, st->damp.a1, st->damp.a2, st->damp.b1, st->damp.b2, false, r.dampHz, rate);
    for (int k = 0; k < 4; ++k) {
        const double delay = double(st->comb[k].size()) / rate;
        st->combGain[k] = std::pow(0.001, delay / std::max(0.01, double(r.seconds)));
    }
    st->params = r;
}

void Mixer::setReverb(SoundHandle handle, const Reverb &reverb) {
    std::lock_guard<std::mutex> lock(mutex_);
    Channel *c = find(handle);
    if (!c)
        return;
    if (reverb.mix <= 0) { // dry from now
        c->reverb.reset();
        return;
    }
    setUpReverb(*c, reverb);
}

void Mixer::seek(SoundHandle handle, double frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->position = std::max(0.0, frame);
}

void Mixer::setTypeVolume(SoundType type, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    typeVolume_[int(type)] = std::clamp(volume, 0, kMaxBoostVolume);
}

int Mixer::typeVolume(SoundType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return typeVolume_[int(type)];
}

void Mixer::setMasterVolume(int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    masterVolume_ = std::clamp(volume, 0, kMaxBoostVolume);
}

int Mixer::masterVolume() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return masterVolume_;
}

void Mixer::mixSound(Channel &c, int frames, int64_t gain) {
    const Sound &s = *c.sound;
    const size_t n = s.frames(), ch = size_t(s.channels);
    const double step = double(s.rate) / double(outputRate_) * c.pitch;
    // A linear pan that keeps the louder side at full level.
    const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
    const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
    auto sample = [&](size_t frame, size_t channel) { return double(s.samples[frame * ch + channel]); };
    // Muffled: a one-pole low-pass, its cutoff from the full band
    // down to about 1 kHz (k = 1 - e^(-2 pi fc / rate)).
    const bool muffled = c.muffle > 0;
    const double cutoff = 20000.0 * std::pow(1000.0 / 20000.0, double(c.muffle));
    const double k = 1 - std::exp(-2 * 3.14159265358979 * cutoff / double(outputRate_));
    for (int i = 0; i < frames; ++i) {
        // Looping: over the sound's own loop if it has one (its WAV's
        // smpl), else all of it.
        const bool ownLoop = c.loop && s.loopEnd > s.loopStart;
        const double loopFrom = ownLoop ? double(s.loopStart) : 0, loopTo = ownLoop ? double(s.loopEnd) : double(n);
        // Ended (not looping): done, or with a reverb its tail first,
        // the reverb fed silence.
        bool silent = false;
        if (c.position >= (c.loop ? loopTo : double(n))) {
            if (!c.loop) {
                if (c.reverb && c.tailLeft < 0)
                    c.tailLeft = std::floor(double(c.reverb->params.tail) * double(outputRate_));
                if (!c.reverb || c.tailLeft < 1) {
                    c.done = true;
                    break;
                }
                c.tailLeft -= 1;
                silent = true;
            } else {
                c.position = loopFrom + std::fmod(c.position - loopFrom, loopTo - loopFrom);
            }
        }
        // Linear interpolation towards the next frame (the loop's
        // start, when looping past its end; else the last holds).
        const size_t f = silent ? 0 : size_t(c.position);
        const size_t g = silent    ? 0
                         : ownLoop ? (double(f + 1) < loopTo ? f + 1 : s.loopStart)
                                   : (f + 1 < n ? f + 1 : (c.loop ? 0 : f));
        const double t = silent ? 0 : c.position - double(f);
        double l = silent ? 0 : sample(f, 0) * (1 - t) + sample(g, 0) * t;
        double r = silent ? 0 : ch > 1 ? sample(f, 1) * (1 - t) + sample(g, 1) * t : l;
        if (muffled) { // one pole: each output moves part way to the input
            c.lowL += k * (l - c.lowL);
            c.lowR += k * (r - c.lowR);
            l = c.lowL;
            r = c.lowR;
        } else { // kept with the sound, so muffling starts smoothly
            c.lowL = l;
            c.lowR = r;
        }
        if (c.filter != Filter::None) {
            l = c.biquad.run(0, l);
            r = ch > 1 ? c.biquad.run(1, r) : l;
        }
        // Its volume and side.
        l *= double(gainL) / 65536.0;
        r *= double(gainR) / 65536.0;
        if (c.reverb) {
            // As Miles' reverb filter on a sample: fed what the sample
            // mixes, its two sides summed (a sound in the middle twice
            // as strongly as one at a side), delayed, damped, through
            // the combs side by side, then the all-passes in turn (the
            // second's input a quarter of the first's output on the
            // left, 0.15 on the right); out, the dry part less the
            // wet's share and the wet.
            Channel::ReverbState &rv = *c.reverb;
            double x = l + r;
            const double delayed = rv.pre[rv.preAt];
            rv.pre[rv.preAt] = x;
            rv.preAt = (rv.preAt + 1) % rv.pre.size();
            x = rv.damp.run(0, rv.pre.size() > 1 ? delayed : x);
            double sum = 0;
            for (int k = 0; k < 4; ++k) {
                std::vector<double> &b = rv.comb[k];
                const double y = b[rv.combAt[k]];
                b[rv.combAt[k]] = x + rv.combGain[k] * y;
                rv.combAt[k] = (rv.combAt[k] + 1) % b.size();
                sum += y;
            }
            auto allPass = [](std::vector<double> &b, size_t &at, double in) {
                const double v = b[at];
                const double w = in + 0.7 * v;
                b[at] = w;
                at = (at + 1) % b.size();
                return v - 0.7 * w;
            };
            const double first = allPass(rv.pass1, rv.pass1At, sum);
            const double wetL = allPass(rv.pass2[0], rv.pass2At[0], first * 0.25);
            const double wetR = allPass(rv.pass2[1], rv.pass2At[1], first * 0.15);
            const double mixWet = double(rv.params.mix);
            l = l * (1 - mixWet) + wetL * mixWet;
            r = r * (1 - mixWet) + wetR * mixWet;
        }
        accumulator_[size_t(i) * 2] += int32_t(std::lround(l));
        accumulator_[size_t(i) * 2 + 1] += int32_t(std::lround(r));
        c.position += step;
    }
}

bool Mixer::readFrame(Stream &s, int16_t frame[2]) {
    if (s.stereo())
        return s.read(frame, 2) == 2;
    const bool got = s.read(frame, 1) == 1;
    frame[1] = frame[0];
    return got;
}

void Mixer::mixStream(Channel &c, int frames, int64_t gain) {
    Stream &s = *c.stream;
    // In 16.16 fixed point: how far each output frame moves through the
    // stream's, and the gains (65536 is as recorded).
    const uint32_t step = uint32_t((uint64_t(s.rate()) << 16) / uint64_t(outputRate_));
    const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
    const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
    if (!c.primed) {
        c.done = !readFrame(s, c.cur) || !readFrame(s, c.next);
        c.primed = true;
    }
    uint32_t frac = uint32_t(std::lround((c.position - std::floor(c.position)) * 65536.0)) & 0xffff;
    double whole = std::floor(c.position);
    for (int i = 0; i < frames && !c.done; ++i) {
        const int32_t l = c.cur[0] + int32_t((int64_t(c.next[0]) - c.cur[0]) * frac >> 16);
        const int32_t r = c.cur[1] + int32_t((int64_t(c.next[1]) - c.cur[1]) * frac >> 16);
        accumulator_[size_t(i) * 2] += int32_t(l * gainL >> 16);
        accumulator_[size_t(i) * 2 + 1] += int32_t(r * gainR >> 16);
        frac += step;
        while (frac >= 0x10000 && !c.done) {
            frac -= 0x10000;
            whole += 1;
            c.cur[0] = c.next[0];
            c.cur[1] = c.next[1];
            if (!readFrame(s, c.next))
                c.done = true;
        }
    }
    c.position = whole + double(frac) / 65536.0;
}

// A SoundStream's next frames, resampled as a sound's are; nothing while it
// waits for enough to be queued.
void Mixer::mixQueue(Channel &c, int frames, int64_t gain) {
    SoundStream &s = *c.queue;
    const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
    const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
    std::lock_guard<std::mutex> lock(s.mutex_);
    const size_t ch = size_t(s.channels_);
    const size_t available = (s.samples_.size() - s.read_) / ch;
    if (c.waiting) {
        if (available < std::max<size_t>(c.prebuffer, 2) && !s.ended_)
            return;
        c.waiting = false;
    }
    const double step = double(s.rate_) / double(outputRate_);
    const int16_t *base = s.samples_.data() + s.read_;
    auto sample = [&](size_t frame, size_t channel) { // the last frame held, once the stream has ended
        return double(base[std::min(frame, available - 1) * ch + channel]);
    };
    for (int i = 0; i < frames; ++i) {
        const size_t f = size_t(c.position);
        if (f >= available || (f + 1 == available && !s.ended_)) { // run dry: wait for more, or end
            if (s.ended_)
                c.done = true;
            else
                c.waiting = true;
            break;
        }
        const double t = c.position - double(f);
        const double l = sample(f, 0) * (1 - t) + sample(f + 1, 0) * t;
        const double r = ch > 1 ? sample(f, 1) * (1 - t) + sample(f + 1, 1) * t : l;
        accumulator_[size_t(i) * 2] += int32_t(std::lround(l * double(gainL) / 65536.0));
        accumulator_[size_t(i) * 2 + 1] += int32_t(std::lround(r * double(gainR) / 65536.0));
        c.position += step;
    }
    const size_t used = std::min(size_t(c.position), available);
    s.read_ += used * ch;
    c.position -= double(used);
}

void Mixer::mix(int16_t *out, int frames) {
    if (frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    accumulator_.assign(size_t(frames) * 2, 0);
    for (Channel &c : channels_) {
        if (c.done || c.paused || allPaused_)
            continue;
        // In 1/65536ths: the sound's volume x its type's x the master
        // volume (as recorded at their defaults).
        int64_t gain = int64_t(c.volume) * typeVolume_[int(c.type)];
        if (masterVolume_ != kMaxVolume)
            gain = gain * masterVolume_ / kMaxVolume;
        if (c.queue)
            mixQueue(c, frames, gain);
        else if (c.stream)
            mixStream(c, frames, gain);
        else
            mixSound(c, frames, gain);
    }
    for (size_t i = 0; i < accumulator_.size(); ++i)
        out[i] = int16_t(std::clamp<int32_t>(accumulator_[i], -32768, 32767));
    channels_.erase(std::remove_if(channels_.begin(), channels_.end(), [](const Channel &c) { return c.done; }),
                    channels_.end());
}

Error startMixerOutput(System &sys, Mixer &mixer) {
    return sys.startAudio([&mixer](int16_t *out, int frames) { mixer.mix(out, frames); });
}

} // namespace reluva::audio
