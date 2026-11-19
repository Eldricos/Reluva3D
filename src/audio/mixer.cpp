// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/mixer.h"
#include "audio/prologic.h"

#include <algorithm>
#include <cmath>

namespace reluva::audio {

Mixer::Mixer(int outputRate) : _outputRate(std::max(outputRate, 1)) {
}

Mixer::Channel *Mixer::find(SoundHandle handle) {
    for (Channel &c : _channels)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

const Mixer::Channel *Mixer::find(SoundHandle handle) const {
    for (const Channel &c : _channels)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

SoundHandle Mixer::play(std::shared_ptr<const Sound> sound, SoundType type, int volume, int pan, bool loop) {
    if (!sound || sound->frames() == 0 || sound->rate <= 0 || sound->channels < 1 || sound->channels > 2)
        return kNoSound;
    std::lock_guard<std::mutex> lock(_mutex);
    const SoundHandle handle = _nextHandle++;
    if (_nextHandle == kNoSound)
        _nextHandle = 1;
    _channels.push_back({handle, std::move(sound), nullptr, type, std::clamp(volume, 0, kMaxVolume),
                         std::clamp(pan, -kMaxPan, kMaxPan), loop});
    return handle;
}

SoundHandle Mixer::play(std::shared_ptr<Stream> stream, SoundType type, int volume, int pan) {
    if (!stream || stream->rate() <= 0)
        return kNoSound;
    std::lock_guard<std::mutex> lock(_mutex);
    const SoundHandle handle = _nextHandle++;
    if (_nextHandle == kNoSound)
        _nextHandle = 1;
    Channel c{
        handle, nullptr, std::move(stream), type, std::clamp(volume, 0, kMaxVolume), std::clamp(pan, -kMaxPan, kMaxPan),
        false};
    _channels.push_back(std::move(c));
    return handle;
}

void SoundStream::push(const int16_t *samples, size_t count) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_read > 0 && _read * 2 >= _samples.size()) { // drop what's been taken
        _samples.erase(_samples.begin(), _samples.begin() + long(_read));
        _read = 0;
    }
    _samples.insert(_samples.end(), samples, samples + count);
}

void SoundStream::end() {
    std::lock_guard<std::mutex> lock(_mutex);
    _ended = true;
}

size_t SoundStream::queued() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return (_samples.size() - _read) / size_t(_channels);
}

SoundHandle Mixer::playStream(std::shared_ptr<SoundStream> stream, SoundType type, int volume, int pan,
                              int prebufferMs) {
    if (!stream || stream->rate() <= 0 || stream->channels() < 1 || stream->channels() > 2)
        return kNoSound;
    std::lock_guard<std::mutex> lock(_mutex);
    const SoundHandle handle = _nextHandle++;
    if (_nextHandle == kNoSound)
        _nextHandle = 1;
    Channel c{handle, nullptr, nullptr, type, std::clamp(volume, 0, kMaxVolume), std::clamp(pan, -kMaxPan, kMaxPan),
              false};
    c.prebuffer = size_t(std::max(prebufferMs, 0)) * size_t(stream->rate()) / 1000;
    c.queue = std::move(stream);
    _channels.push_back(std::move(c));
    return handle;
}

void Mixer::stop(SoundHandle handle) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (Channel *c = find(handle))
        c->done = true;
}

void Mixer::stopType(SoundType type) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (Channel &c : _channels)
        if (c.type == type)
            c.done = true;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(_mutex);
    for (Channel &c : _channels)
        c.done = true;
}

bool Mixer::isPlaying(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return find(handle) != nullptr;
}

bool Mixer::isTypePlaying(SoundType type) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return std::any_of(_channels.begin(), _channels.end(), [&](const Channel &c) { return c.type == type && !c.done; });
}

double Mixer::position(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Channel *c = find(handle);
    return c ? c->position : 0;
}

void Mixer::setVolume(SoundHandle handle, int volume) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (Channel *c = find(handle))
        c->volume = std::clamp(volume, 0, kMaxVolume);
}

void Mixer::setPaused(SoundHandle handle, bool paused) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (Channel *c = find(handle))
        c->paused = paused;
}

bool Mixer::isPaused(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Channel *c = find(handle);
    return c && c->paused;
}

void Mixer::setAllPaused(bool paused) {
    std::lock_guard<std::mutex> lock(_mutex);
    _allPaused = paused;
}

bool Mixer::allPaused() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _allPaused;
}

SoundHandle Mixer::playOutput(std::shared_ptr<OutputStream> stream, SoundType type, int volume) {
    if (!stream)
        return kNoSound;
    std::lock_guard<std::mutex> lock(_mutex);
    const SoundHandle handle = _nextHandle++;
    if (_nextHandle == kNoSound)
        _nextHandle = 1;
    Channel c{handle, nullptr, nullptr, type, std::clamp(volume, 0, kMaxVolume), 0, true};
    c.output = std::move(stream);
    _channels.push_back(std::move(c));
    return handle;
}

void Mixer::setRate(SoundHandle handle, double factor) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (Channel *c = find(handle))
        c->rate = std::clamp(factor, 0.05, 20.0);
}

void Mixer::mixOutput(Channel &c, int frames, int64_t gain) {
    _outputBuffer.assign(size_t(frames) * 2, 0);
    c.output->read(_outputBuffer.data(), frames, _outputRate);
    const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
    const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
    for (int i = 0; i < frames; ++i) {
        _accumulator[size_t(i) * 2] += int32_t(int64_t(_outputBuffer[size_t(i) * 2]) * gainL / 65536);
        _accumulator[size_t(i) * 2 + 1] += int32_t(int64_t(_outputBuffer[size_t(i) * 2 + 1]) * gainR / 65536);
    }
    c.position += double(frames);
}

void Mixer::setPan(SoundHandle handle, int pan) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (Channel *c = find(handle))
        c->pan = std::clamp(pan, -kMaxPan, kMaxPan);
}

void Mixer::setMuffle(SoundHandle handle, float muffle) {
    std::lock_guard<std::mutex> lock(_mutex);
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
    std::lock_guard<std::mutex> lock(_mutex);
    Channel *c = find(handle);
    if (!c || (c->filter == kind && c->filterHz == hz))
        return;
    if (c->filter != kind)
        c->biquad = {};
    c->filter = kind;
    c->filterHz = hz;
    if (kind != Filter::None)
        butterworth(c->biquad.a0, c->biquad.a1, c->biquad.a2, c->biquad.b1, c->biquad.b2, kind == Filter::HighPass, hz,
                    double(_outputRate));
}

// The reverb's lines at the output rate: four combs (29.7, 37.1, 41.1,
// 43.7 ms), each fed back by 0.001^(its delay / the reverb's time), so
// its echoes fall 60 dB over that; two all-passes (5 and 1.7 ms, 0.7).
void Mixer::setUpReverb(Channel &c, const Reverb &r) const {
    auto &st = c.reverb;
    const bool fresh = !st;
    if (fresh)
        st = std::make_shared<Channel::ReverbState>();
    const double rate = double(_outputRate);
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
    std::lock_guard<std::mutex> lock(_mutex);
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
    std::lock_guard<std::mutex> lock(_mutex);
    if (Channel *c = find(handle))
        c->position = std::max(0.0, frame);
}

void Mixer::setTypeVolume(SoundType type, int volume) {
    std::lock_guard<std::mutex> lock(_mutex);
    _typeVolume[int(type)] = std::clamp(volume, 0, kMaxBoostVolume);
}

int Mixer::typeVolume(SoundType type) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _typeVolume[int(type)];
}

void Mixer::setMasterVolume(int volume) {
    std::lock_guard<std::mutex> lock(_mutex);
    _masterVolume = std::clamp(volume, 0, kMaxBoostVolume);
}

int Mixer::masterVolume() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _masterVolume;
}

void Mixer::mixSound(Channel &c, int frames, int64_t gain) {
    const Sound &s = *c.sound;
    const size_t n = s.frames();
    const size_t ch = size_t(s.channels);
    const double step = double(s.rate) * c.rate / double(_outputRate);
    // Looping: the sound's loop, or all of it.
    const size_t loopEnd = s.loopEnd && s.loopEnd < n ? s.loopEnd : n;
    const size_t loopStart = s.loopStart < loopEnd ? s.loopStart : 0;
    const size_t end = c.loop ? loopEnd : n;
    // A linear pan that keeps the louder side at full level.
    const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
    const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
    // (Scaling by 1/65536 first gives the same products: a power of two.)
    const double scaleL = double(gainL) / 65536.0;
    const double scaleR = double(gainR) / 65536.0;
    auto sample = [&](size_t frame, size_t channel) { return double(s.samples[frame * ch + channel]); };
    // Muffled: a one-pole low-pass, its cutoff from the full band down to
    // about 1 kHz (k = 1 - e^(-2 pi fc / rate)).
    const bool muffled = c.muffle > 0;
    const double cutoff = 20000.0 * std::pow(1000.0 / 20000.0, double(c.muffle));
    const double k = 1 - std::exp(-2 * 3.14159265358979 * cutoff / double(_outputRate));
    for (int i = 0; i < frames; ++i) {
        // Ended (not looping): done, or with a reverb its tail first, the
        // reverb fed silence.
        bool silent = false;
        if (c.position >= double(end)) {
            if (!c.loop) {
                if (c.reverb && c.tailLeft < 0)
                    c.tailLeft = std::floor(double(c.reverb->params.tail) * double(_outputRate));
                if (!c.reverb || c.tailLeft < 1) {
                    c.done = true;
                    break;
                }
                c.tailLeft -= 1;
                silent = true;
            } else {
                c.position = double(loopStart) + std::fmod(c.position - double(loopStart), double(end - loopStart));
            }
        }
        // Linear interpolation towards the next frame (the loop's first,
        // when looping past its end; else the last holds).
        double l = 0;
        double r = 0;
        if (!silent) {
            const size_t f = size_t(c.position);
            const size_t g = f + 1 < end ? f + 1 : (c.loop ? loopStart : f);
            const double t = c.position - double(f);
            l = sample(f, 0) * (1 - t) + sample(g, 0) * t;
            r = ch > 1 ? sample(f, 1) * (1 - t) + sample(g, 1) * t : l;
        }
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
        l *= scaleL;
        r *= scaleR;
        if (c.reverb)
            reverbFrame(*c.reverb, l, r);
        _accumulator[size_t(i) * 2] += int32_t(std::lround(l));
        _accumulator[size_t(i) * 2 + 1] += int32_t(std::lround(r));
        c.position += step;
    }
}

// As Miles' reverb filter on a sample: fed what the sample mixes, its two
// sides summed (a sound in the middle twice as strongly as one at a side),
// delayed, damped, through the combs side by side, then the all-passes in
// turn (the second's input a quarter of the first's output on the left,
// 0.15 on the right); out, the dry part less the wet's share and the wet.
void Mixer::reverbFrame(Channel::ReverbState &rv, double &l, double &r) {
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
    const uint32_t step = uint32_t((uint64_t(s.rate()) << 16) / uint64_t(_outputRate));
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
        _accumulator[size_t(i) * 2] += int32_t(l * gainL >> 16);
        _accumulator[size_t(i) * 2 + 1] += int32_t(r * gainR >> 16);
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
    std::lock_guard<std::mutex> lock(s._mutex);
    const size_t ch = size_t(s._channels);
    const size_t available = (s._samples.size() - s._read) / ch;
    if (c.waiting) {
        if (available < std::max<size_t>(c.prebuffer, 2) && !s._ended)
            return;
        c.waiting = false;
    }
    const double step = double(s._rate) / double(_outputRate);
    const int16_t *base = s._samples.data() + s._read;
    auto sample = [&](size_t frame, size_t channel) { // the last frame held, once the stream has ended
        return double(base[std::min(frame, available - 1) * ch + channel]);
    };
    for (int i = 0; i < frames; ++i) {
        const size_t f = size_t(c.position);
        if (f >= available || (f + 1 == available && !s._ended)) { // run dry: wait for more, or end
            if (s._ended)
                c.done = true;
            else
                c.waiting = true;
            break;
        }
        const double t = c.position - double(f);
        const double l = sample(f, 0) * (1 - t) + sample(f + 1, 0) * t;
        const double r = ch > 1 ? sample(f, 1) * (1 - t) + sample(f + 1, 1) * t : l;
        _accumulator[size_t(i) * 2] += int32_t(std::lround(l * double(gainL) / 65536.0));
        _accumulator[size_t(i) * 2 + 1] += int32_t(std::lround(r * double(gainR) / 65536.0));
        c.position += step;
    }
    const size_t used = std::min(size_t(c.position), available);
    s._read += used * ch;
    c.position -= double(used);
}

void Mixer::mix(int16_t *out, int frames) {
    if (frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    _accumulator.assign(size_t(frames) * 2, 0);
    for (Channel &c : _channels) {
        if (c.done || c.paused || _allPaused)
            continue;
        // In 1/65536ths: the sound's volume x its type's x the master
        // volume (as recorded at their defaults).
        int64_t gain = int64_t(c.volume) * _typeVolume[int(c.type)];
        if (_masterVolume != kMaxVolume)
            gain = gain * _masterVolume / kMaxVolume;
        if (c.output)
            mixOutput(c, frames, gain);
        else if (c.queue)
            mixQueue(c, frames, gain);
        else if (c.stream)
            mixStream(c, frames, gain);
        else
            mixSound(c, frames, gain);
    }
    for (size_t i = 0; i < _accumulator.size(); ++i)
        out[i] = int16_t(std::clamp<int32_t>(_accumulator[i], -32768, 32767));
    _channels.erase(std::remove_if(_channels.begin(), _channels.end(), [](const Channel &c) { return c.done; }),
                    _channels.end());
}

Error startMixerOutput(System &sys, Mixer &mixer) {
    return sys.startAudio([&mixer](int16_t *out, int frames) { mixer.mix(out, frames); });
}

Surround surroundFromSetting(const std::string &value) {
    if (value == "prologic")
        return Surround::kProLogic;
    if (value == "passive")
        return Surround::kPassive;
    if (value == "headphones")
        return Surround::kHeadphones;
    return Surround::kOff;
}

Error startMixerOutput(System &sys, Mixer &mixer, Surround surround, int *channels) {
    int count = 2;
    if (surround == Surround::kProLogic || surround == Surround::kPassive) {
        const int device = sys.audioDeviceChannels();
        count = device >= 6 ? 6 : device >= 4 ? 4 : 2;
    }
    if (channels)
        *channels = 2;
    if (surround == Surround::kOff || (count == 2 && surround != Surround::kHeadphones))
        return startMixerOutput(sys, mixer);
    struct State {
        ProLogicDecoder decoder;
        std::vector<int16_t> stereo;
        std::vector<float> decoded;
    };
    ProLogicOptions options;
    options.active = surround != Surround::kPassive;
    auto state = std::make_shared<State>(State{ProLogicDecoder(mixer.outputRate(), options), {}, {}});
    auto callback = [&mixer, state, count](int16_t *out, int frames) {
        state->stereo.resize(size_t(frames) * 2);
        state->decoded.resize(size_t(frames) * 4);
        mixer.mix(state->stereo.data(), frames);
        state->decoder.process(state->stereo.data(), state->decoded.data(), frames);
        layOutChannels(state->decoded.data(), frames, count, out);
    };
    if (Error err = sys.startAudioChannels(count, callback)) {
        if (count == 2)
            return err;
        return startMixerOutput(sys, mixer); // the device wouldn't open that way: stereo as mixed
    }
    if (channels)
        *channels = count;
    return Error::success();
}

void SoundQueue::push(std::shared_ptr<const Sound> sound, int tag) {
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.push_back({std::move(sound), tag});
}

int SoundQueue::playing() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.empty() ? -1 : _queue.front().tag;
}

size_t SoundQueue::queued() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _queue.size();
}

void SoundQueue::read(int16_t *out, int frames, int rate) {
    std::lock_guard<std::mutex> lock(_mutex);
    for (int i = 0; i < frames; ++i) {
        while (!_queue.empty() && _position >= double(_queue.front().sound->frames())) {
            _position -= double(_queue.front().sound->frames());
            _queue.pop_front();
        }
        if (_queue.empty()) {
            out[2 * i] = out[2 * i + 1] = 0;
            continue;
        }
        const Sound &s = *_queue.front().sound;
        const size_t n = s.frames();
        const size_t ch = size_t(s.channels);
        const size_t f = size_t(_position);
        const double t = _position - double(f);
        // Linear interpolation towards the frame after: this sound's, or
        // the next one's first.
        const Sound *after = &s;
        size_t g = f + 1;
        if (g >= n) {
            if (_queue.size() > 1 && _queue[1].sound->frames() && _queue[1].sound->channels == s.channels) {
                after = _queue[1].sound.get();
                g = 0;
            } else {
                g = f;
            }
        }
        for (size_t c = 0; c < 2; ++c) {
            const size_t k = std::min(c, ch - 1);
            const double v = s.samples[f * ch + k] * (1 - t) + after->samples[g * ch + k] * t;
            out[2 * size_t(i) + c] = static_cast<int16_t>(std::clamp<long>(std::lround(v), -32768L, 32767L));
        }
        _position += double(s.rate) / double(rate);
    }
}

} // namespace reluva::audio
