// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/ambient.h"

#include "common/audio/decode.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace reluva::age {

namespace {

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ','))
        out.push_back(field);
    while (!out.empty() && out.back().empty())
        out.pop_back();
    return out;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        out.push_back(line);
    }
    return out;
}

float number(const std::vector<std::string>& f, size_t i) {
    return i < f.size() ? std::strtof(f[i].c_str(), nullptr) : 0.0f;
}

} // namespace

Error parseAmbience(const std::string& text, Ambience& out) {
    out = Ambience{};
    const std::vector<std::string> all = lines(text);
    // The distances' header, then their row.
    if (all.size() < 3 || all[0].rfind("Min distance", 0) != 0)
        return Error::fail("no distances");
    const std::vector<std::string> d = fields(all[1]);
    out.minDistance = number(d, 0);
    out.maxDistance = number(d, 1);
    out.priority = int(number(d, 2));
    out.area = int(number(d, 3));
    // The samples' header, the samples, and after VECTORPOINTS (and its
    // "x,y,z") the points.
    bool points = false;
    for (size_t i = 3; i < all.size(); ++i) {
        const std::vector<std::string> f = fields(all[i]);
        if (f.empty())
            continue;
        if (f[0] == "VECTORPOINTS") {
            points = true;
            continue;
        }
        if (points) {
            if (f.size() >= 3 && f[0] != "x")
                out.points.push_back({number(f, 0), number(f, 1), number(f, 2)});
            continue;
        }
        AmbientSample s;
        s.name = f[0];
        s.volume = number(f, 1);
        s.type = int(number(f, 2));
        s.low = number(f, 3);
        s.high = number(f, 4);
        s.active = f.size() < 6 || number(f, 5) != 0;
        out.samples.push_back(s);
    }
    if (out.samples.empty())
        return Error::fail("no samples");
    return Error::success();
}

std::vector<std::string> parseAmbientContainer(const std::string& text) {
    std::vector<std::string> out;
    const std::vector<std::string> all = lines(text);
    for (size_t i = 1; i < all.size(); ++i) // after "file names"
        if (const std::vector<std::string> f = fields(all[i]); !f.empty())
            out.push_back(f[0]);
    return out;
}

float ambientGain(const Ambience& a, float distance) {
    if (distance <= a.minDistance)
        return 1;
    if (distance >= a.maxDistance)
        return 0;
    return 1 - (distance - a.minDistance) / (a.maxDistance - a.minDistance);
}

Error AmbientSounds::load(const SearchSet& files, const std::string& map, audio::Mixer* mixer, uint32_t seed) {
    clear();
    random_.seed(seed);
    std::vector<uint8_t> data;
    if (Error err = files.read("aud/ambient/" + map + "ambientcontainer.csv", data))
        return err;
    for (const std::string& name : parseAmbientContainer(std::string(data.begin(), data.end()))) {
        Ambience a;
        if (files.read("aud/ambient/" + name + ".csv", data) ||
            parseAmbience(std::string(data.begin(), data.end()), a) || a.points.empty())
            continue;
        ambiences_.push_back(std::move(a));
    }
    mixer_ = mixer;
    for (size_t i = 0; i < ambiences_.size(); ++i)
        for (const AmbientSample& s : ambiences_[i].samples) {
            // Those played when something happens aren't the ambience's.
            if (!s.active || s.type == 3)
                continue;
            Voice v;
            v.ambience = i;
            v.sample = &s;
            if (mixer_) {
                auto sound = std::make_shared<audio::Sound>();
                if ((!files.read("aud/aud22/Amb3D/" + s.name + ".22k.wav", data) ||
                     !files.read("aud/aud22/" + s.name + ".22k.wav", data) ||
                     !files.read("aud/aud11/" + s.name + ".11k.wav", data)) &&
                    !audio::decodeSound(data, *sound))
                    v.sound = sound;
            }
            v.wait = std::uniform_real_distribution<float>(0, std::max(s.high, 0.1f))(random_);
            voices_.push_back(std::move(v));
        }
    return Error::success();
}

void AmbientSounds::clear() {
    if (mixer_)
        for (Voice& v : voices_)
            if (v.handle != audio::kNoSound)
                mixer_->stop(v.handle);
    voices_.clear();
    ambiences_.clear();
    mixer_ = nullptr;
}

void AmbientSounds::update(float dt, const Vec3& listener) {
    std::vector<float> gains;
    for (const Ambience& a : ambiences_) {
        float nearest = 1e30f;
        for (const Vec3& p : a.points)
            nearest = std::min(nearest, length(p - listener));
        gains.push_back(ambientGain(a, nearest));
    }
    for (Voice& v : voices_) {
        const float gain = gains[v.ambience] * v.sample->volume;
        const int volume = int(std::clamp(gain, 0.0f, 1.0f) * audio::kMaxVolume);
        if (v.sample->type == 0) {
            // A loop, while it's heard.
            if (!mixer_ || !v.sound)
                continue;
            if (gain > 0.01f && v.handle == audio::kNoSound)
                v.handle = mixer_->play(v.sound, audio::SoundType::Effects, volume, 0, true);
            else if (gain <= 0.01f && v.handle != audio::kNoSound) {
                mixer_->stop(v.handle);
                v.handle = audio::kNoSound;
            }
            if (v.handle != audio::kNoSound)
                mixer_->setVolume(v.handle, volume);
            continue;
        }
        // A one-shot, now and then while it's heard.
        if (mixer_ && v.handle != audio::kNoSound && !mixer_->isPlaying(v.handle))
            v.handle = audio::kNoSound;
        v.wait -= dt;
        if (v.wait > 0)
            continue;
        const float low = std::max(0.5f, v.sample->low), high = std::max(low, v.sample->high);
        v.wait = std::uniform_real_distribution<float>(low, high)(random_);
        if (gain > 0.01f && mixer_ && v.sound && v.handle == audio::kNoSound)
            v.handle = mixer_->play(v.sound, audio::SoundType::Effects, volume, 0, false);
    }
}

size_t AmbientSounds::playing() const {
    size_t n = 0;
    for (const Voice& v : voices_)
        if (v.handle != audio::kNoSound && (!mixer_ || mixer_->isPlaying(v.handle)))
            ++n;
    return n;
}

} // namespace reluva::age
