// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How far sounds carry and how they're panned follows the GPL Quake III
// Arena sound system's behaviour (full volume within 80 units, 0.0008 of
// it lost a unit beyond, each ear given half plus or minus half of how far
// to that side it is); this is new code written from that description.

#include "engines/idtech3/audio/sound_system.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::idtech3 {

namespace {
constexpr float kFullVolumeDistance = 80;
constexpr float kAttenuation = 0.0008f;
constexpr float kSeparation = 0.5f;
} // namespace

std::shared_ptr<const audio::Sound> SoundSystem::find(const std::string &name) {
    if (name.empty())
        return nullptr;
    if (auto sound = _sounds.find(name))
        return sound;
    _missing.insert(name);
    return nullptr;
}

std::pair<float, float> SoundSystem::spatialize(const Vec3f &origin) const {
    Vec3f d{origin[0] - _listenerOrigin[0], origin[1] - _listenerOrigin[1], origin[2] - _listenerOrigin[2]};
    float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > 1e-4f)
        for (float &c : d)
            c /= dist;
    const float volume = std::clamp(1 - std::max(dist - kFullVolumeDistance, 0.0f) * kAttenuation, 0.0f, 1.0f);
    // The listener's right is opposite its left axis.
    const Vec3f &left = _listenerAxes[1];
    const float pan = std::clamp(-(d[0] * left[0] + d[1] * left[1] + d[2] * left[2]), -1.0f, 1.0f);
    return {volume, dist > 1e-4f ? pan : 0.0f};
}

float SoundSystem::place(audio::SoundHandle handle, const std::optional<Vec3f> &origin, int entity,
                         std::optional<float> ownVolume) {
    // The listener's own sounds are heard as they are.
    if (!origin && entity == _listener) {
        _mixer.setVolume(handle, audio::kMaxVolume);
        _mixer.setPan(handle, 0);
        return 1;
    }
    // At its spot, or where its entity was last said to be: the map's
    // origin for one never placed, as the original keeps it (not at the
    // listener, which would be heard everywhere).
    Vec3f at{};
    if (origin)
        at = *origin;
    else if (const auto it = _entities.find(entity); it != _entities.end())
        at = it->second;
    auto [volume, pan] = spatialize(at);
    if (ownVolume)
        volume = std::clamp(*ownVolume, 0.0f, 1.0f);
    // Each ear as the original's mixer has it (s_separation 0.5): half in
    // each for a sound straight ahead or right here, all in one ear for one
    // straight to that side. As the mixer's volume (the louder ear) and
    // pan (how much the other is turned down).
    const float right = volume * std::max(0.0f, kSeparation + (1 - kSeparation) * pan);
    const float left = volume * std::max(0.0f, kSeparation - (1 - kSeparation) * pan);
    const float loudest = std::max(left, right);
    const float balance = loudest <= 0 ? 0 : right >= left ? 1 - left / right : -(1 - right / left);
    _mixer.setVolume(handle, int(loudest * float(audio::kMaxVolume)));
    _mixer.setPan(handle, int(balance * float(audio::kMaxPan)));
    return volume;
}

bool SoundSystem::allowed(int entity, bool local, const std::string &name, const std::optional<Vec3f> &origin) const {
    // Local sounds count as the listener's.
    const auto owner = [&](const Playing &p) { return p.local ? _listener : p.entity; };
    const int who = local ? _listener : entity;
    // Sounds placed at a spot (shots, impacts, often all on the world's
    // number) are only repeats at the same spot.
    auto samePlace = [&](const Playing &p) {
        if (p.origin.has_value() != origin.has_value())
            return false;
        if (!origin)
            return true;
        float d2 = 0;
        for (size_t k = 0; k < 3; ++k)
            d2 += ((*p.origin)[k] - (*origin)[k]) * ((*p.origin)[k] - (*origin)[k]);
        return d2 < 1;
    };
    for (const Playing &p : _playing)
        if (p.handle != audio::kNoSound && owner(p) == who && p.name == name && _nowMs - p.startMs < 50 &&
            samePlace(p) && _mixer.isPlaying(p.handle))
            return false;
    return true;
}

void SoundSystem::startSound(const std::optional<Vec3f> &origin, int entity, int channel, const std::string &name) {
    const auto sound = find(name);
    const bool ok = sound && allowed(entity, false, name, origin);
    char where[64];
    if (origin)
        std::snprintf(where, sizeof where, "at %.0f %.0f %.0f", (*origin)[0], (*origin)[1], (*origin)[2]);
    else
        std::snprintf(where, sizeof where, "following it");
    const std::string what =
        name + " (entity " + std::to_string(entity) + ", channel " + std::to_string(channel) + ", " + where + "): ";
    if (!ok) {
        note(name, entity, what + (!sound ? "not found" : "skipped as a repeat"));
        return;
    }
    const audio::SoundHandle handle = _mixer.play(sound, audio::SoundType::kEffects, 0);
    if (handle == audio::kNoSound)
        return;
    char volume[48];
    std::snprintf(volume, sizeof volume, "played at volume %.2f (listener %d)", place(handle, origin, entity),
                  _listener);
    note(name, entity, what + volume);
    _playing.push_back({handle, entity, channel, origin, false, name, _nowMs, std::nullopt});
}

void SoundSystem::startLocalSound(const std::string &name, int channel) {
    const auto sound = find(name);
    const bool ok = sound && allowed(-1, true, name, std::nullopt);
    note(name, -1,
         name + " (local, channel " + std::to_string(channel) + "): " +
             (!sound ? "not found"
              : ok   ? "played"
                     : "skipped as a repeat"));
    if (!ok)
        return;
    const audio::SoundHandle handle = _mixer.play(sound, audio::SoundType::kEffects);
    if (handle != audio::kNoSound)
        _playing.push_back({handle, -1, channel, std::nullopt, true, name, _nowMs, std::nullopt});
}

void SoundSystem::note(const std::string &name, int entity, std::string line) {
    // Each sound asked for in the last two seconds, by name and entity:
    // how often, and what became of the last.
    _starts.push_back({_nowMs, name, entity, std::move(line)});
    while (!_starts.empty() && _starts.front().ms + 2000 < _nowMs)
        _starts.pop_front();
}

std::vector<std::string> SoundSystem::recent() const {
    struct Summary {
        int count = 0;
        const std::string *last = nullptr;
        uint64_t lastMs = 0;
    };
    std::map<std::pair<std::string, int>, Summary> by;
    for (const Start &s : _starts) {
        Summary &sum = by[{s.name, s.entity}];
        ++sum.count;
        sum.last = &s.line;
        sum.lastMs = s.ms;
    }
    // The most recent first.
    std::vector<std::pair<uint64_t, std::string>> lines;
    for (const auto &[key, sum] : by)
        lines.emplace_back(sum.lastMs, std::to_string(sum.count) + " in 2 s, last " + *sum.last);
    std::sort(lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (size_t i = 0; i < lines.size() && i < 6; ++i)
        out.push_back(lines[i].second);
    return out;
}

std::vector<std::string> SoundSystem::loopingNow() const {
    // Every loop the client game asked for this frame, and whether it plays.
    std::vector<std::string> out;
    for (const auto &[key, origin] : _loopsAdded)
        out.push_back((key.second.empty() ? std::string("(no name)") : key.second) + " (entity " +
                      std::to_string(key.first) + (_loops.count(key) ? ")" : ", not found)"));
    return out;
}

void SoundSystem::clearLoopingSounds() {
    _loopsAdded.clear();
}

void SoundSystem::addLoopingSound(int entity, const Vec3f &origin, const std::string &name) {
    _loopsAdded[{entity, name}] = origin;
}

void SoundSystem::updateEntityPosition(int entity, const Vec3f &origin) {
    _entities[entity] = origin;
}

void SoundSystem::addAmbientLoop(int entity, const Vec3f &origin, float volume, const std::string &name) {
    _ambientAdded[{entity, name}] = {origin, volume};
}

void SoundSystem::startAmbientSound(int entity, const Vec3f &origin, float volume, const std::string &name) {
    const auto sound = find(name);
    note(name, entity, name + " (ambient, entity " + std::to_string(entity) + "): " + (sound ? "played" : "not found"));
    if (!sound)
        return;
    const audio::SoundHandle handle = _mixer.play(sound, audio::SoundType::kEffects, 0);
    if (handle == audio::kNoSound)
        return;
    place(handle, origin, entity, volume);
    _playing.push_back({handle, entity, 0, origin, false, name, _nowMs, volume});
}

void SoundSystem::respatialize(int entity, const Vec3f &origin, const std::array<Vec3f, 3> &axes) {
    _listener = entity;
    _listenerOrigin = origin;
    _listenerAxes = axes;
}

void SoundSystem::startBackgroundTrack(const std::string &intro, const std::string &loop) {
    stopBackgroundTrack();
    _musicLoop = find(loop);
    auto first = find(intro);
    _musicIntro = first != nullptr;
    if (!first)
        first = _musicLoop;
    if (first)
        _music = _mixer.play(first, audio::SoundType::kMusic, audio::kMaxVolume, 0, !_musicIntro);
}

void SoundSystem::stopBackgroundTrack() {
    if (_music != audio::kNoSound)
        _mixer.stop(_music);
    _music = audio::kNoSound;
    _musicLoop.reset();
    _musicIntro = false;
}

void SoundSystem::endFrame() {
    // Finished sounds go; the rest follow their entities and the listener.
    _playing.erase(
        std::remove_if(_playing.begin(), _playing.end(),
                       [&](const Playing &p) { return p.handle == audio::kNoSound || !_mixer.isPlaying(p.handle); }),
        _playing.end());
    for (const Playing &p : _playing)
        if (!p.local)
            place(p.handle, p.origin, p.entity, p.volume);

    // Looping sounds: new ones start, those not added this frame stop.
    for (auto it = _loops.begin(); it != _loops.end();) {
        if (!_loopsAdded.count(it->first)) {
            _mixer.stop(it->second);
            it = _loops.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &[key, origin] : _loopsAdded) {
        auto it = _loops.find(key);
        if (it == _loops.end()) {
            const auto sound = find(key.second);
            if (!sound)
                continue;
            const audio::SoundHandle handle = _mixer.play(sound, audio::SoundType::kEffects, 0, 0, true);
            if (handle == audio::kNoSound)
                continue;
            it = _loops.emplace(key, handle).first;
        }
        place(it->second, origin, key.first);
    }
    for (auto it = _ambientLoops.begin(); it != _ambientLoops.end();) {
        if (!_ambientAdded.count(it->first)) {
            _mixer.stop(it->second);
            it = _ambientLoops.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto &[key, at] : _ambientAdded) {
        auto it = _ambientLoops.find(key);
        if (it == _ambientLoops.end()) {
            const auto sound = find(key.second);
            if (!sound)
                continue;
            const audio::SoundHandle handle = _mixer.play(sound, audio::SoundType::kEffects, 0, 0, true);
            if (handle == audio::kNoSound)
                continue;
            it = _ambientLoops.emplace(key, handle).first;
        }
        place(it->second, at.first, key.first, at.second);
    }
    _ambientAdded.clear();

    // The music: the loop once the intro is over.
    if (_musicIntro && _music != audio::kNoSound && !_mixer.isPlaying(_music)) {
        _musicIntro = false;
        _music = _musicLoop ? _mixer.play(_musicLoop, audio::SoundType::kMusic, audio::kMaxVolume, 0, true)
                            : audio::kNoSound;
    }
}

void SoundSystem::setVolumes(float effects, float music) {
    _mixer.setTypeVolume(audio::SoundType::kEffects, int(std::clamp(effects, 0.0f, 1.0f) * float(audio::kMaxVolume)));
    _mixer.setTypeVolume(audio::SoundType::kMusic, int(std::clamp(music, 0.0f, 1.0f) * float(audio::kMaxVolume)));
}

void SoundSystem::stopAll() {
    stopSounds();
    stopBackgroundTrack();
}

void SoundSystem::stopSounds() {
    for (const Playing &p : _playing)
        if (p.handle != audio::kNoSound)
            _mixer.stop(p.handle);
    _playing.clear();
    for (const auto &[key, handle] : _loops)
        _mixer.stop(handle);
    _loops.clear();
    _loopsAdded.clear();
    for (const auto &[key, handle] : _ambientLoops)
        _mixer.stop(handle);
    _ambientLoops.clear();
    _ambientAdded.clear();
}

} // namespace reluva::idtech3
