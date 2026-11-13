// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/mixer.h"
#include "audio/prologic.h"

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

SoundHandle Mixer::playOutput(std::shared_ptr<OutputStream> stream, SoundType type, int volume) {
    if (!stream)
        return kNoSound;
    std::lock_guard<std::mutex> lock(mutex_);
    const SoundHandle handle = nextHandle_++;
    if (nextHandle_ == kNoSound)
        nextHandle_ = 1;
    Channel c{handle, nullptr, nullptr, type, std::clamp(volume, 0, kMaxVolume), 0, true};
    c.output = std::move(stream);
    channels_.push_back(std::move(c));
    return handle;
}

void Mixer::setRate(SoundHandle handle, double factor) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->rate = std::clamp(factor, 0.05, 20.0);
}

void Mixer::mixOutput(Channel &c, int frames, int64_t gain) {
    outputBuffer_.assign(size_t(frames) * 2, 0);
    c.output->read(outputBuffer_.data(), frames, outputRate_);
    const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
    const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
    for (int i = 0; i < frames; ++i) {
        accumulator_[size_t(i) * 2] += int32_t(int64_t(outputBuffer_[size_t(i) * 2]) * gainL / 65536);
        accumulator_[size_t(i) * 2 + 1] += int32_t(int64_t(outputBuffer_[size_t(i) * 2 + 1]) * gainR / 65536);
    }
    c.position += double(frames);
}

void Mixer::setPan(SoundHandle handle, int pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel *c = find(handle))
        c->pan = std::clamp(pan, -kMaxPan, kMaxPan);
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
    const size_t n = s.frames();
    const size_t ch = size_t(s.channels);
    const double step = double(s.rate) * c.rate / double(outputRate_);
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
    for (int i = 0; i < frames; ++i) {
        if (c.position >= double(end)) {
            if (!c.loop) {
                c.done = true;
                break;
            }
            c.position = double(loopStart) + std::fmod(c.position - double(loopStart), double(end - loopStart));
        }
        // Linear interpolation towards the next frame (the loop's first,
        // when looping past its end; else the last holds).
        const size_t f = size_t(c.position);
        const size_t g = f + 1 < end ? f + 1 : (c.loop ? loopStart : f);
        const double t = c.position - double(f);
        const double l = sample(f, 0) * (1 - t) + sample(g, 0) * t;
        const double r = ch > 1 ? sample(f, 1) * (1 - t) + sample(g, 1) * t : l;
        accumulator_[size_t(i) * 2] += int32_t(std::lround(l * scaleL));
        accumulator_[size_t(i) * 2 + 1] += int32_t(std::lround(r * scaleR));
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
        if (c.output)
            mixOutput(c, frames, gain);
        else if (c.queue)
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

Surround surroundFromSetting(const std::string &value) {
    if (value == "prologic")
        return Surround::ProLogic;
    if (value == "passive")
        return Surround::Passive;
    if (value == "headphones")
        return Surround::Headphones;
    return Surround::Off;
}

Error startMixerOutput(System &sys, Mixer &mixer, Surround surround, int *channels) {
    int count = 2;
    if (surround == Surround::ProLogic || surround == Surround::Passive) {
        const int device = sys.audioDeviceChannels();
        count = device >= 6 ? 6 : device >= 4 ? 4 : 2;
    }
    if (channels)
        *channels = 2;
    if (surround == Surround::Off || (count == 2 && surround != Surround::Headphones))
        return startMixerOutput(sys, mixer);
    struct State {
        ProLogicDecoder decoder;
        std::vector<int16_t> stereo;
        std::vector<float> decoded;
    };
    ProLogicOptions options;
    options.active = surround != Surround::Passive;
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
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back({std::move(sound), tag});
}

int SoundQueue::playing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() ? -1 : queue_.front().tag;
}

size_t SoundQueue::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void SoundQueue::read(int16_t *out, int frames, int rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < frames; ++i) {
        while (!queue_.empty() && position_ >= double(queue_.front().sound->frames())) {
            position_ -= double(queue_.front().sound->frames());
            queue_.pop_front();
        }
        if (queue_.empty()) {
            out[2 * i] = out[2 * i + 1] = 0;
            continue;
        }
        const Sound &s = *queue_.front().sound;
        const size_t n = s.frames(), ch = size_t(s.channels);
        const size_t f = size_t(position_);
        const double t = position_ - double(f);
        // Linear interpolation towards the frame after: this sound's, or
        // the next one's first.
        const Sound *after = &s;
        size_t g = f + 1;
        if (g >= n) {
            if (queue_.size() > 1 && queue_[1].sound->frames() && queue_[1].sound->channels == s.channels) {
                after = queue_[1].sound.get();
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
        position_ += double(s.rate) / double(rate);
    }
}

} // namespace reluva::audio
