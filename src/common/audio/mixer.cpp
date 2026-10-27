// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/mixer.h"

#include <algorithm>
#include <cmath>

namespace reluva::audio {

Mixer::Mixer(int outputRate) : outputRate_(std::max(outputRate, 1)) {}

Mixer::Channel* Mixer::find(SoundHandle handle) {
    for (Channel& c : channels_)
        if (c.handle == handle && !c.done)
            return &c;
    return nullptr;
}

const Mixer::Channel* Mixer::find(SoundHandle handle) const {
    for (const Channel& c : channels_)
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
    channels_.push_back({handle, std::move(sound), type, std::clamp(volume, 0, kMaxVolume),
                         std::clamp(pan, -kMaxPan, kMaxPan), loop, 0, false, nullptr});
    return handle;
}

void SoundStream::push(const int16_t* samples, size_t count) {
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

SoundHandle Mixer::playStream(std::shared_ptr<SoundStream> stream, SoundType type, int volume, int pan, int prebufferMs) {
    if (!stream || stream->rate() <= 0 || stream->channels() < 1 || stream->channels() > 2)
        return kNoSound;
    std::lock_guard<std::mutex> lock(mutex_);
    const SoundHandle handle = nextHandle_++;
    if (nextHandle_ == kNoSound)
        nextHandle_ = 1;
    Channel c{handle, nullptr, type, std::clamp(volume, 0, kMaxVolume), std::clamp(pan, -kMaxPan, kMaxPan), false, 0, false, nullptr};
    c.prebuffer = size_t(std::max(prebufferMs, 0)) * size_t(stream->rate()) / 1000;
    c.stream = std::move(stream);
    channels_.push_back(std::move(c));
    return handle;
}

void Mixer::stop(SoundHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->done = true;
}

void Mixer::stopType(SoundType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel& c : channels_)
        if (c.type == type)
            c.done = true;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Channel& c : channels_)
        c.done = true;
}

bool Mixer::isPlaying(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(handle) != nullptr;
}

bool Mixer::isTypePlaying(SoundType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(channels_.begin(), channels_.end(), [&](const Channel& c) { return c.type == type && !c.done; });
}

double Mixer::position(SoundHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Channel* c = find(handle);
    return c ? c->position : 0;
}

void Mixer::setVolume(SoundHandle handle, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->volume = std::clamp(volume, 0, kMaxVolume);
}

void Mixer::setPan(SoundHandle handle, int pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->pan = std::clamp(pan, -kMaxPan, kMaxPan);
}

void Mixer::setTypeVolume(SoundType type, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    typeVolume_[int(type)] = std::clamp(volume, 0, kMaxVolume);
}

int Mixer::typeVolume(SoundType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return typeVolume_[int(type)];
}

void Mixer::mix(int16_t* out, int frames) {
    if (frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    accumulator_.assign(size_t(frames) * 2, 0);
    for (Channel& c : channels_) {
        if (c.done)
            continue;
        if (c.stream) {
            const int64_t gain = int64_t(c.volume) * typeVolume_[int(c.type)];
            mixStream(c, frames, c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain,
                      c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain);
            continue;
        }
        const Sound& s = *c.sound;
        const size_t n = s.frames(), ch = size_t(s.channels);
        const double step = double(s.rate) / double(outputRate_);
        // Gains in 1/65536ths: volume x type volume, and a linear pan that
        // keeps the louder side at full level.
        const int64_t gain = int64_t(c.volume) * typeVolume_[int(c.type)];
        const int64_t gainL = c.pan > 0 ? gain * (kMaxPan - c.pan) / kMaxPan : gain;
        const int64_t gainR = c.pan < 0 ? gain * (kMaxPan + c.pan) / kMaxPan : gain;
        auto sample = [&](size_t frame, size_t channel) { return double(s.samples[frame * ch + channel]); };
        for (int i = 0; i < frames; ++i) {
            if (c.position >= double(n)) {
                if (!c.loop) {
                    c.done = true;
                    break;
                }
                c.position = std::fmod(c.position, double(n));
            }
            // Linear interpolation towards the next frame (the first, when
            // looping past the end; else the last holds).
            const size_t f = size_t(c.position);
            const size_t g = f + 1 < n ? f + 1 : (c.loop ? 0 : f);
            const double t = c.position - double(f);
            const double l = sample(f, 0) * (1 - t) + sample(g, 0) * t;
            const double r = ch > 1 ? sample(f, 1) * (1 - t) + sample(g, 1) * t : l;
            accumulator_[size_t(i) * 2] += int32_t(std::lround(l * double(gainL) / 65536.0));
            accumulator_[size_t(i) * 2 + 1] += int32_t(std::lround(r * double(gainR) / 65536.0));
            c.position += step;
        }
    }
    for (size_t i = 0; i < accumulator_.size(); ++i)
        out[i] = int16_t(std::clamp<int32_t>(accumulator_[i], -32768, 32767));
    channels_.erase(std::remove_if(channels_.begin(), channels_.end(), [](const Channel& c) { return c.done; }),
                    channels_.end());
}

// A stream's next frames, resampled as a sound's are; silence while it
// waits for enough to be queued.
void Mixer::mixStream(Channel& c, int frames, int64_t gainL, int64_t gainR) {
    SoundStream& s = *c.stream;
    std::lock_guard<std::mutex> lock(s.mutex_);
    const size_t ch = size_t(s.channels_);
    const size_t available = (s.samples_.size() - s.read_) / ch;
    if (c.waiting) {
        if (available < std::max<size_t>(c.prebuffer, 2) && !s.ended_)
            return;
        c.waiting = false;
    }
    const double step = double(s.rate_) / double(outputRate_);
    const int16_t* base = s.samples_.data() + s.read_;
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

Error startMixerOutput(System& sys, Mixer& mixer) {
    return sys.startAudio([&mixer](int16_t* out, int frames) { mixer.mix(out, frames); });
}

} // namespace reluva::audio
