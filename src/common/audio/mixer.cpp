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
                         std::clamp(pan, -kMaxPan, kMaxPan), loop});
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

void Mixer::setPitch(SoundHandle handle, float pitch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Channel* c = find(handle))
        c->pitch = std::clamp(double(pitch), 0.05, 8.0);
}

void Mixer::setTypeVolume(SoundType type, int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    typeVolume_[int(type)] = std::clamp(volume, 0, kMaxVolume);
}

void Mixer::addStream(std::shared_ptr<Stream> stream, SoundType type) {
    if (!stream)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.push_back({std::move(stream), type});
}

void Mixer::removeStream(const Stream* stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    streams_.erase(
        std::remove_if(streams_.begin(), streams_.end(), [stream](const auto& s) { return s.first.get() == stream; }),
        streams_.end());
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
        const Sound& s = *c.sound;
        const size_t n = s.frames(), ch = size_t(s.channels);
        const double step = double(s.rate) / double(outputRate_) * c.pitch;
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
    for (const auto& [stream, type] : streams_) {
        streamBuffer_.assign(size_t(frames) * 2, 0.0f);
        stream->read(streamBuffer_.data(), frames);
        const double gain = 32767.0 * typeVolume_[int(type)] / kMaxVolume;
        for (size_t i = 0; i < accumulator_.size(); ++i)
            accumulator_[i] += int32_t(std::lround(double(streamBuffer_[i]) * gain));
    }
    // The balance: the far side quieter, linearly.
    if (balance_ != 0)
        for (size_t i = 0; i < accumulator_.size(); i += 2) {
            int32_t& side = accumulator_[balance_ > 0 ? i : i + 1];
            side = int32_t(int64_t(side) * (kMaxPan - std::abs(balance_)) / kMaxPan);
        }
    for (size_t i = 0; i < accumulator_.size(); ++i)
        out[i] = int16_t(std::clamp<int32_t>(accumulator_[i], -32768, 32767));
    channels_.erase(std::remove_if(channels_.begin(), channels_.end(), [](const Channel& c) { return c.done; }),
                    channels_.end());
}

void Mixer::setBalance(int balance) {
    std::lock_guard<std::mutex> lock(mutex_);
    balance_ = std::clamp(balance, -kMaxPan, kMaxPan);
}

Error startMixerOutput(System& sys, Mixer& mixer) {
    return sys.startAudio([&mixer](int16_t* out, int frames) { mixer.mix(out, frames); });
}

} // namespace reluva::audio
