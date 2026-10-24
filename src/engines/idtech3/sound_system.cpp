// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How far sounds carry and how they're panned follows the GPL Quake III
// Arena sound system's behaviour (full volume within 80 units, 0.0008 of
// it lost a unit beyond, panned by the listener's right); this is new code
// written from that description.

#include "engines/idtech3/sound_system.h"

#include <algorithm>
#include <cmath>

namespace reluva::idtech3 {

namespace {
constexpr float kFullVolumeDistance = 80;
constexpr float kAttenuation = 0.0008f;
} // namespace

std::shared_ptr<const audio::Sound> SoundSystem::find(const std::string& name) {
    if (name.empty())
        return nullptr;
    if (auto sound = sounds_.get(name))
        return sound;
    // Without an extension: the formats the games use.
    if (name.find('.', name.rfind('/') == std::string::npos ? 0 : name.rfind('/')) == std::string::npos) {
        for (const char* ext : {".mp3", ".wav"})
            if (auto sound = sounds_.get(name + ext))
                return sound;
    }
    return nullptr;
}

std::pair<float, float> SoundSystem::spatialize(const Vec3f& origin) const {
    Vec3f d{origin[0] - listenerOrigin_[0], origin[1] - listenerOrigin_[1], origin[2] - listenerOrigin_[2]};
    float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > 1e-4f)
        for (float& c : d)
            c /= dist;
    const float volume = std::clamp(1 - std::max(dist - kFullVolumeDistance, 0.0f) * kAttenuation, 0.0f, 1.0f);
    // The listener's right is opposite its left axis.
    const Vec3f& left = listenerAxes_[1];
    const float pan = std::clamp(-(d[0] * left[0] + d[1] * left[1] + d[2] * left[2]), -1.0f, 1.0f);
    return {volume, dist > 1e-4f ? pan : 0.0f};
}

void SoundSystem::place(audio::SoundHandle handle, const std::optional<Vec3f>& origin, int entity) {
    // The listener's own sounds are heard as they are.
    if (!origin && entity == listener_) {
        mixer_.setVolume(handle, audio::kMaxVolume);
        mixer_.setPan(handle, 0);
        return;
    }
    Vec3f at = listenerOrigin_;
    if (origin)
        at = *origin;
    else if (const auto it = entities_.find(entity); it != entities_.end())
        at = it->second;
    const auto [volume, pan] = spatialize(at);
    mixer_.setVolume(handle, int(volume * float(audio::kMaxVolume)));
    mixer_.setPan(handle, int(pan * float(audio::kMaxPan)));
}

bool SoundSystem::allowed(int entity, bool local, const std::string& name, const std::optional<Vec3f>& origin) const {
    // Local sounds count as the listener's.
    const auto owner = [&](const Playing& p) { return p.local ? listener_ : p.entity; };
    const int who = local ? listener_ : entity;
    // Sounds placed at a spot (shots, impacts, often all on the world's
    // number) are only repeats at the same spot.
    auto samePlace = [&](const Playing& p) {
        if (p.origin.has_value() != origin.has_value())
            return false;
        if (!origin)
            return true;
        float d2 = 0;
        for (size_t k = 0; k < 3; ++k)
            d2 += ((*p.origin)[k] - (*origin)[k]) * ((*p.origin)[k] - (*origin)[k]);
        return d2 < 1;
    };
    for (const Playing& p : playing_)
        if (p.handle != audio::kNoSound && owner(p) == who && p.name == name && nowMs_ - p.startMs < 50 &&
            samePlace(p) && mixer_.isPlaying(p.handle))
            return false;
    return true;
}

void SoundSystem::startSound(const std::optional<Vec3f>& origin, int entity, int channel, const std::string& name) {
    const auto sound = find(name);
    if (!sound || !allowed(entity, false, name, origin))
        return;
    const audio::SoundHandle handle = mixer_.play(sound, audio::SoundType::Effects, 0);
    if (handle == audio::kNoSound)
        return;
    place(handle, origin, entity);
    playing_.push_back({handle, entity, channel, origin, false, name, nowMs_});
}

void SoundSystem::startLocalSound(const std::string& name, int channel) {
    const auto sound = find(name);
    if (!sound || !allowed(-1, true, name, std::nullopt))
        return;
    const audio::SoundHandle handle = mixer_.play(sound, audio::SoundType::Effects);
    if (handle != audio::kNoSound)
        playing_.push_back({handle, -1, channel, std::nullopt, true, name, nowMs_});
}

void SoundSystem::clearLoopingSounds() { loopsAdded_.clear(); }

void SoundSystem::addLoopingSound(int entity, const Vec3f& origin, const std::string& name) {
    loopsAdded_[{entity, name}] = origin;
}

void SoundSystem::updateEntityPosition(int entity, const Vec3f& origin) { entities_[entity] = origin; }

void SoundSystem::respatialize(int entity, const Vec3f& origin, const std::array<Vec3f, 3>& axes) {
    listener_ = entity;
    listenerOrigin_ = origin;
    listenerAxes_ = axes;
}

void SoundSystem::startBackgroundTrack(const std::string& intro, const std::string& loop) {
    stopBackgroundTrack();
    musicLoop_ = find(loop);
    auto first = find(intro);
    musicIntro_ = first != nullptr;
    if (!first)
        first = musicLoop_;
    if (first)
        music_ = mixer_.play(first, audio::SoundType::Music, audio::kMaxVolume, 0, !musicIntro_);
}

void SoundSystem::stopBackgroundTrack() {
    if (music_ != audio::kNoSound)
        mixer_.stop(music_);
    music_ = audio::kNoSound;
    musicLoop_.reset();
    musicIntro_ = false;
}

void SoundSystem::endFrame() {
    // Finished sounds go; the rest follow their entities and the listener.
    playing_.erase(std::remove_if(playing_.begin(), playing_.end(),
                                  [&](const Playing& p) {
                                      return p.handle == audio::kNoSound || !mixer_.isPlaying(p.handle);
                                  }),
                   playing_.end());
    for (const Playing& p : playing_)
        if (!p.local)
            place(p.handle, p.origin, p.entity);

    // Looping sounds: new ones start, those not added this frame stop.
    for (auto it = loops_.begin(); it != loops_.end();) {
        if (!loopsAdded_.count(it->first)) {
            mixer_.stop(it->second);
            it = loops_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [key, origin] : loopsAdded_) {
        auto it = loops_.find(key);
        if (it == loops_.end()) {
            const auto sound = find(key.second);
            if (!sound)
                continue;
            const audio::SoundHandle handle = mixer_.play(sound, audio::SoundType::Effects, 0, 0, true);
            if (handle == audio::kNoSound)
                continue;
            it = loops_.emplace(key, handle).first;
        }
        place(it->second, origin, key.first);
    }

    // The music: the loop once the intro is over.
    if (musicIntro_ && music_ != audio::kNoSound && !mixer_.isPlaying(music_)) {
        musicIntro_ = false;
        music_ = musicLoop_ ? mixer_.play(musicLoop_, audio::SoundType::Music, audio::kMaxVolume, 0, true)
                            : audio::kNoSound;
    }
}

void SoundSystem::setVolumes(float effects, float music) {
    mixer_.setTypeVolume(audio::SoundType::Effects, int(std::clamp(effects, 0.0f, 1.0f) * float(audio::kMaxVolume)));
    mixer_.setTypeVolume(audio::SoundType::Music, int(std::clamp(music, 0.0f, 1.0f) * float(audio::kMaxVolume)));
}

void SoundSystem::stopAll() {
    for (const Playing& p : playing_)
        if (p.handle != audio::kNoSound)
            mixer_.stop(p.handle);
    playing_.clear();
    for (const auto& [key, handle] : loops_)
        mixer_.stop(handle);
    loops_.clear();
    loopsAdded_.clear();
    stopBackgroundTrack();
}

} // namespace reluva::idtech3
