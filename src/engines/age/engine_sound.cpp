// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/engine_sound.h"

#include "common/audio/decode.h"

#include <algorithm>
#include <cstdlib>

namespace reluva::age {

namespace {

std::vector<std::string> fields(std::string_view line) {
    std::vector<std::string> out;
    size_t at = 0;
    while (true) {
        const size_t comma = line.find(',', at);
        std::string f(line.substr(at, comma == std::string_view::npos ? std::string_view::npos : comma - at));
        while (!f.empty() && (f.back() == '\r' || f.back() == ' '))
            f.pop_back();
        out.push_back(f);
        if (comma == std::string_view::npos)
            return out;
        at = comma + 1;
    }
}

std::vector<std::string> linesOf(std::string_view csv) {
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < csv.size()) {
        size_t eol = csv.find('\n', at);
        if (eol == std::string_view::npos)
            eol = csv.size();
        lines.emplace_back(csv.substr(at, eol - at));
        at = eol + 1;
    }
    return lines;
}

float number(const std::vector<std::string>& f, size_t i) {
    return i < f.size() ? std::strtof(f[i].c_str(), nullptr) : 0;
}

float ramp(float x, float a, float b) {
    if (b <= a)
        return x >= a ? 1.0f : 0.0f;
    return std::clamp((x - a) / (b - a), 0.0f, 1.0f);
}

} // namespace

Error readEngineSound(std::string_view csv, EngineSoundSetup& out) {
    out = EngineSoundSetup{};
    // Line 1: headings; 2: horn, its volume, flags, the number of engine
    // samples, the clutch sound; 3: headings; then one line per sample.
    const std::vector<std::string> lines = linesOf(csv);
    if (lines.size() < 3)
        return Error::fail("too short for an engine sound table");
    const auto head = fields(lines[1]);
    out.horn = head.empty() ? "" : head[0];
    const int count = int(number(head, 3));
    for (int i = 0; i < count && size_t(3 + i) < lines.size(); ++i) {
        const auto f = fields(lines[size_t(3 + i)]);
        if (f.empty() || f[0].empty())
            continue;
        EngineSample s;
        s.wave = f[0];
        s.minVolume = number(f, 1);
        s.maxVolume = number(f, 2);
        s.fadeInStart = number(f, 3);
        s.fadeInEnd = number(f, 4);
        s.fadeOutStart = number(f, 5);
        s.fadeOutEnd = number(f, 6);
        s.minPitch = number(f, 7);
        s.maxPitch = number(f, 8);
        s.pitchStart = number(f, 9);
        s.pitchEnd = number(f, 10);
        out.samples.push_back(s);
    }
    if (out.samples.empty())
        return Error::fail("no engine samples");
    return Error::success();
}

void engineSampleMix(const EngineSample& s, float rpm, float throttle, float& volume, float& pitch) {
    const float fade = ramp(rpm, s.fadeInStart, s.fadeInEnd) * (1 - ramp(rpm, s.fadeOutStart, s.fadeOutEnd));
    volume = fade * (s.minVolume + (s.maxVolume - s.minVolume) * std::clamp(throttle, 0.0f, 1.0f));
    pitch = s.minPitch + (s.maxPitch - s.minPitch) * ramp(rpm, s.pitchStart, s.pitchEnd);
}

Error EngineSound::start(audio::Mixer& mixer, const SearchSet& files, const std::string& car) {
    stop();
    std::vector<uint8_t> data;
    const std::string table = "aud/cardata/player/" + car + ".csv";
    if (Error err = files.read(table, data))
        return err;
    if (Error err = readEngineSound(std::string(data.begin(), data.end()), setup_))
        return Error::fail(table + ": " + err.message);
    mixer_ = &mixer;
    for (const EngineSample& s : setup_.samples) {
        auto sound = std::make_shared<audio::Sound>();
        bool ok = (!files.read("aud/aud22/engines/" + s.wave + ".22k.wav", data) ||
                   !files.read("aud/aud11/" + s.wave + ".11k.wav", data)) &&
                  !audio::decodeSound(data, *sound);
        playing_.push_back(ok ? mixer.play(sound, audio::SoundType::Effects, 0, 0, true) : audio::kNoSound);
    }
    return Error::success();
}

void EngineSound::update(float rpm, float throttle) {
    for (size_t i = 0; i < playing_.size(); ++i) {
        if (playing_[i] == audio::kNoSound)
            continue;
        float volume, pitch;
        engineSampleMix(setup_.samples[i], rpm, throttle, volume, pitch);
        mixer_->setVolume(playing_[i], int(volume * audio::kMaxVolume));
        mixer_->setPitch(playing_[i], pitch);
    }
}

void EngineSound::stop() {
    if (mixer_)
        for (audio::SoundHandle h : playing_)
            if (h != audio::kNoSound)
                mixer_->stop(h);
    playing_.clear();
    mixer_ = nullptr;
}

Error readSirenTable(std::string_view csv, std::vector<SirenSample>& out) {
    // Anything before the first "Sample name," line (the player's tables
    // start with an explosion), then for each sample its name and volume,
    // and its "play time, next index" pairs, each under a heading line.
    out.clear();
    const auto lines = linesOf(csv);
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto f = fields(lines[i]);
        if (f.empty())
            continue;
        if (f[0] == "Sample name" && i + 1 < lines.size()) {
            const auto name = fields(lines[++i]);
            if (name.empty() || name[0].empty())
                continue;
            SirenSample s;
            s.wave = name[0];
            s.volume = name.size() > 1 && !name[1].empty() ? number(name, 1) : 1;
            out.push_back(s);
        } else if (f[0] == "play time" && i + 1 < lines.size() && !out.empty()) {
            const auto v = fields(lines[++i]);
            out.back().next.push_back({number(v, 0), int(number(v, 1))});
        }
    }
    if (out.empty())
        return Error::fail("no siren samples");
    return Error::success();
}

Error SirenSound::load(audio::Mixer& mixer, const SearchSet& files, const std::string& table) {
    stop();
    samples_.clear();
    sounds_.clear();
    std::vector<uint8_t> data;
    if (Error err = files.read(table, data))
        return err;
    if (Error err = readSirenTable(std::string(data.begin(), data.end()), samples_))
        return Error::fail(table + ": " + err.message);
    mixer_ = &mixer;
    for (const SirenSample& s : samples_) {
        auto sound = std::make_shared<audio::Sound>();
        const bool ok = (!files.read("aud/aud22/sirens/" + s.wave + ".22k.wav", data) ||
                         !files.read("aud/aud11/" + s.wave + ".11k.wav", data)) &&
                        !audio::decodeSound(data, *sound);
        sounds_.push_back(ok ? sound : nullptr);
    }
    return Error::success();
}

void SirenSound::update(float dt, float volume) {
    if (!mixer_ || samples_.empty())
        return;
    if (volume <= 0) {
        stop();
        return;
    }
    left_ -= dt;
    if (current_ < 0 || left_ <= 0) {
        // On to the next sample (the first to start with).
        int next = 0;
        if (current_ >= 0) {
            const SirenSample& s = samples_[size_t(current_)];
            next = s.next.empty() ? current_ : s.next[turns_++ % s.next.size()].second;
        }
        current_ = std::clamp(next, 0, int(samples_.size()) - 1);
        const SirenSample& s = samples_[size_t(current_)];
        left_ = s.next.empty() ? 1e9f : std::max(0.2f, s.next[turns_ % s.next.size()].first);
        if (playing_ != audio::kNoSound)
            mixer_->stop(playing_);
        const auto& sound = sounds_[size_t(current_)];
        playing_ = sound ? mixer_->play(sound, audio::SoundType::Effects, 0, 0, true) : audio::kNoSound;
    }
    if (playing_ != audio::kNoSound)
        mixer_->setVolume(playing_, int(std::clamp(volume, 0.0f, 1.0f) * samples_[size_t(current_)].volume *
                                        float(audio::kMaxVolume)));
}

void SirenSound::stop() {
    if (mixer_ && playing_ != audio::kNoSound)
        mixer_->stop(playing_);
    playing_ = audio::kNoSound;
    current_ = -1;
    left_ = 0;
}

} // namespace reluva::age
