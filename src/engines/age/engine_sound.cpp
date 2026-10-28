// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/engine_sound.h"

#include "common/audio/decode.h"

#include <algorithm>
#include <cctype>
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

std::string lowerCase(std::string s) {
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
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
    out.hornVolume = head.size() > 1 ? number(head, 1) : 1;
    out.clutch = head.size() > 4 ? head[4] : "";
    out.clutchVolume = head.size() > 5 ? number(head, 5) : 1;
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

Error readSurfaceSounds(std::string_view csv, SurfaceSounds& out) {
    // A heading and the tunnel's surface; then for each surface a heading,
    // its line, a heading for its skids and their lines (as many as it
    // says).
    out = SurfaceSounds{};
    const auto lines = linesOf(csv);
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto f = fields(lines[i]);
        if (f.empty() || f[0].empty())
            continue;
        const std::string head = lowerCase(f[0]);
        if (head == "tunnel sound index" && i + 1 < lines.size()) {
            out.tunnel = int(number(fields(lines[++i]), 0));
        } else if (head == "surface wave" && i + 1 < lines.size()) {
            const auto v = fields(lines[++i]);
            SurfaceSound s;
            s.wave = v.empty() ? "" : v[0];
            s.maxSpeed = std::max(1.0f, number(v, 1));
            s.minVolume = number(v, 2);
            s.maxVolume = number(v, 3);
            s.minPitch = number(v, 4);
            s.maxPitch = number(v, 5);
            s.minSkidVolume = number(v, 6);
            s.maxSkidVolume = number(v, 7);
            const int skids = int(number(v, 8));
            if (i + 1 < lines.size()) {
                const auto next = fields(lines[i + 1]);
                if (!next.empty() && lowerCase(next[0]).rfind("skid wave", 0) == 0)
                    ++i;
            }
            for (int k = 0; k < skids && i + 1 < lines.size(); ++k) {
                const auto w = fields(lines[++i]);
                if (!w.empty() && !w[0].empty())
                    s.skids.push_back({w[0], number(w, 1), number(w, 2)});
            }
            out.surfaces.push_back(s);
        }
    }
    if (out.surfaces.empty())
        return Error::fail("no surfaces");
    return Error::success();
}

Error readImpactSounds(std::string_view csv, std::vector<ImpactSounds>& out) {
    // "***", "Banger name,Num samples,ID", its line, a heading, its samples;
    // ENDOFDATA last.
    out.clear();
    const auto lines = linesOf(csv);
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto f = fields(lines[i]);
        if (f.empty() || lowerCase(f[0]) != "banger name" || i + 1 >= lines.size())
            continue;
        const auto b = fields(lines[++i]);
        if (b.empty() || b[0] == "ENDOFDATA")
            break;
        ImpactSounds s;
        s.name = b[0];
        s.id = int(number(b, 2));
        const int count = int(number(b, 1));
        if (i + 1 < lines.size())
            ++i; // the samples' heading
        for (int k = 0; k < count && i + 1 < lines.size(); ++k) {
            const auto v = fields(lines[++i]);
            if (v.empty() || v[0].empty())
                continue;
            s.samples.push_back(
                {v[0], number(v, 1), number(v, 2), number(v, 3), number(v, 4), v.size() > 5 ? number(v, 5) : 1});
        }
        out.push_back(s);
    }
    if (out.empty())
        return Error::fail("no impact sounds");
    return Error::success();
}

CarSounds::SoundPtr CarSounds::load(const std::string& wave) const {
    if (wave.empty() || wave == "NOSOUND" || !files_)
        return nullptr;
    std::vector<uint8_t> data;
    for (const char* folder : {"", "engines/", "horns/", "surfaces/", "impacts/", "suspension/"})
        if (!files_->read("aud/aud22/" + std::string(folder) + wave + ".22k.wav", data) ||
            (!*folder && !files_->read("aud/aud11/" + wave + ".11k.wav", data))) {
            auto sound = std::make_shared<audio::Sound>();
            if (!audio::decodeSound(data, *sound))
                return sound;
        }
    return nullptr;
}

Error CarSounds::start(audio::Mixer& mixer, const SearchSet& files, const std::string& car, bool wet) {
    stop();
    mixer_ = &mixer;
    files_ = &files;
    std::vector<uint8_t> data;
    auto text = [&](const std::string& name) {
        return files.read(name, data) ? std::string() : std::string(data.begin(), data.end());
    };
    Error first = readEngineSound(text("aud/cardata/player/" + car + ".csv"), car_);
    horn_ = load(car_.horn);
    clutch_ = load(car_.clutch);
    lastGear_ = 1;
    if (Error err = readSurfaceSounds(
            text(std::string("aud/cardata/player/default_surface") + (wet ? "wet" : "dry") + ".csv"), surfaces_);
        err && !first)
        first = err;
    rolling_.clear();
    skidding_.clear();
    for (const SurfaceSound& s : surfaces_.surfaces) {
        rolling_.push_back(load(s.wave));
        skidding_.emplace_back();
        for (const SkidSample& k : s.skids)
            skidding_.back().push_back(load(k.wave));
    }
    std::vector<ImpactSounds> impacts;
    if (Error err = readImpactSounds(text("aud/cardata/player/default_impacts.csv"), impacts); err && !first)
        first = err;
    wall_ = impacts.empty() ? ImpactSounds{} : impacts.front(); // WALL, the city's walls and other cars
    wallSounds_.clear();
    for (const ImpactSample& s : wall_.samples)
        wallSounds_.push_back(load(s.wave));
    // suspensionaudio.csv: a heading, then the sample, velocities and volumes.
    const auto lines = linesOf(text("aud/cardata/player/suspensionaudio.csv"));
    if (lines.size() > 1) {
        const auto f = fields(lines[1]);
        suspension_ = f.empty() ? nullptr : load(f[0]);
        bumpMin_ = number(f, 1);
        bumpMax_ = number(f, 2);
        bumpMinVolume = number(f, 3);
        bumpMaxVolume = number(f, 4);
    }
    return first;
}

void CarSounds::loop(audio::SoundHandle& handle, const SoundPtr& sound, float volume, float pitch) {
    if (volume <= 0.01f || !sound) {
        if (handle != audio::kNoSound)
            mixer_->stop(handle);
        handle = audio::kNoSound;
        return;
    }
    if (handle == audio::kNoSound)
        handle = mixer_->play(sound, audio::SoundType::Effects, 0, 0, true);
    mixer_->setVolume(handle, int(std::clamp(volume, 0.0f, 1.0f) * float(audio::kMaxVolume)));
    mixer_->setPitch(handle, pitch);
}

void CarSounds::update(float dt, const CarSim& car, bool horn) {
    if (!mixer_)
        return;
    // The horn, held; the clutch on changing gear.
    loop(hornPlaying_, horn_, horn ? car_.hornVolume : 0, 1);
    if (car.gear != lastGear_ && clutch_)
        mixer_->play(clutch_, audio::SoundType::Effects, int(car_.clutchVolume * float(audio::kMaxVolume)));
    lastGear_ = car.gear;

    // The tyres: on the surface most of the wheels touching are on.
    int counts[256] = {};
    float slip = 0;
    int touching = 0;
    for (const CarSim::Wheel& w : car.wheels)
        if (w.contact) {
            ++counts[w.surface.sound];
            slip = std::max(slip, w.slip);
            ++touching;
        }
    int surface = -1;
    if (touching) {
        surface = int(std::max_element(std::begin(counts), std::end(counts)) - std::begin(counts));
        if (size_t(surface) >= surfaces_.surfaces.size())
            surface = 0;
    }
    const float speed = length(car.velocity);
    if (surface != rollSurface_) {
        loop(rollPlaying_, nullptr, 0, 1); // another recording
        rollSurface_ = surface;
    }
    if (surface >= 0 && !surfaces_.surfaces.empty()) {
        const SurfaceSound& s = surfaces_.surfaces[size_t(surface)];
        const float f = std::clamp(speed / s.maxSpeed, 0.0f, 1.0f);
        loop(rollPlaying_, rolling_[size_t(surface)], speed < 1 ? 0 : s.minVolume + (s.maxVolume - s.minVolume) * f,
             s.minPitch + (s.maxPitch - s.minPitch) * f);
        // The skid whose slip range takes in the tyres', louder as they
        // slide more.
        int sample = -1;
        for (size_t k = 0; k < s.skids.size(); ++k)
            if (slip >= s.skids[k].minSlip && slip <= s.skids[k].maxSlip)
                sample = int(k);
        if (surface != skidSurface_ || sample != skidSample_) {
            loop(skidPlaying_, nullptr, 0, 1);
            skidSurface_ = surface;
            skidSample_ = sample;
        }
        if (sample >= 0 && speed > 2) {
            const float from = s.skids.front().minSlip;
            const float g = std::clamp((slip - from) / std::max(0.01f, 1 - from), 0.0f, 1.0f);
            loop(skidPlaying_, skidding_[size_t(surface)][size_t(sample)],
                 s.minSkidVolume + (s.maxSkidVolume - s.minSkidVolume) * g, 1);
        } else {
            loop(skidPlaying_, nullptr, 0, 1);
        }
    } else {
        loop(rollPlaying_, nullptr, 0, 1);
        loop(skidPlaying_, nullptr, 0, 1);
    }

    // Knocks: the WALL sample for how hard (an impulse, kg m/s, read as the
    // table's force), louder within its range.
    impactWait_ = std::max(0.0f, impactWait_ - dt);
    if (impactWait_ <= 0 && !wall_.samples.empty() && car.impact >= wall_.samples.front().minForce) {
        for (size_t k = 0; k < wall_.samples.size(); ++k) {
            const ImpactSample& s = wall_.samples[k];
            if (car.impact < s.minForce || car.impact > s.maxForce || !wallSounds_[k])
                continue;
            const float g = std::clamp((car.impact - s.minForce) / std::max(1.0f, s.maxForce - s.minForce), 0.0f, 1.0f);
            const audio::SoundHandle h =
                mixer_->play(wallSounds_[k], audio::SoundType::Effects,
                             int((s.minVolume + (s.maxVolume - s.minVolume) * g) * float(audio::kMaxVolume)));
            if (h != audio::kNoSound)
                mixer_->setPitch(h, s.pitch);
            impactWait_ = 0.25f;
            break;
        }
    }

    // The suspension bottoming out or a wheel landing.
    bumpWait_ = std::max(0.0f, bumpWait_ - dt);
    float bump = 0;
    for (const CarSim::Wheel& w : car.wheels)
        bump = std::max(bump, w.bump);
    if (suspension_ && bumpWait_ <= 0 && bump >= bumpMin_) {
        const float g = std::clamp((bump - bumpMin_) / std::max(0.01f, bumpMax_ - bumpMin_), 0.0f, 1.0f);
        mixer_->play(suspension_, audio::SoundType::Effects,
                     int((bumpMinVolume + (bumpMaxVolume - bumpMinVolume) * g) * float(audio::kMaxVolume)));
        bumpWait_ = 0.3f;
    }
}

void CarSounds::stop() {
    if (mixer_)
        for (audio::SoundHandle* h : {&hornPlaying_, &rollPlaying_, &skidPlaying_})
            if (*h != audio::kNoSound)
                mixer_->stop(*h);
    hornPlaying_ = rollPlaying_ = skidPlaying_ = audio::kNoSound;
    rollSurface_ = skidSurface_ = skidSample_ = -1;
    mixer_ = nullptr;
}

} // namespace reluva::age
