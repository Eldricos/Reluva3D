// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/audio/engine_sound.h"

#include "audio/decode.h"

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
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

float number(const std::vector<std::string> &f, size_t i) {
    return i < f.size() ? std::strtof(f[i].c_str(), nullptr) : 0;
}

float ramp(float x, float a, float b) {
    if (b <= a)
        return x >= a ? 1.0f : 0.0f;
    return std::clamp((x - a) / (b - a), 0.0f, 1.0f);
}

} // namespace

Error readEngineSound(std::string_view csv, EngineSoundSetup &out) {
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

void engineSampleMix(const EngineSample &s, float rpm, float throttle, float &volume, float &pitch) {
    const float fade = ramp(rpm, s.fadeInStart, s.fadeInEnd) * (1 - ramp(rpm, s.fadeOutStart, s.fadeOutEnd));
    volume = fade * (s.minVolume + (s.maxVolume - s.minVolume) * std::clamp(throttle, 0.0f, 1.0f));
    pitch = s.minPitch + (s.maxPitch - s.minPitch) * ramp(rpm, s.pitchStart, s.pitchEnd);
}

Error EngineSound::start(audio::Mixer &mixer, const SearchSet &files, const std::string &car) {
    stop();
    std::vector<uint8_t> data;
    const std::string table = "aud/cardata/player/" + car + ".csv";
    if (Error err = files.read(table, data))
        return err;
    if (Error err = readEngineSound(std::string(data.begin(), data.end()), _setup))
        return Error::fail(table + ": " + err.message);
    _mixer = &mixer;
    for (const EngineSample &s : _setup.samples) {
        auto sound = std::make_shared<audio::Sound>();
        bool ok = (!files.read("aud/aud22/engines/" + s.wave + ".22k.wav", data) ||
                   !files.read("aud/aud11/" + s.wave + ".11k.wav", data)) &&
                  !audio::decodeSound(data, *sound);
        _playing.push_back(ok ? mixer.play(sound, audio::SoundType::kEffects, 0, 0, true) : audio::kNoSound);
    }
    return Error::success();
}

void EngineSound::update(float rpm, float throttle, float gain, float pan) {
    for (size_t i = 0; i < _playing.size(); ++i) {
        if (_playing[i] == audio::kNoSound)
            continue;
        float volume, pitch;
        engineSampleMix(_setup.samples[i], rpm, throttle, volume, pitch);
        _mixer->setVolume(_playing[i], int(volume * gain * audio::kMaxVolume));
        _mixer->setPan(_playing[i], int(std::clamp(pan, -1.0f, 1.0f) * audio::kMaxPan));
        _mixer->setPitch(_playing[i], pitch);
    }
}

void EngineSound::stop() {
    if (_mixer)
        for (audio::SoundHandle h : _playing)
            if (h != audio::kNoSound)
                _mixer->stop(h);
    _playing.clear();
    _mixer = nullptr;
}

Error readSirenTable(std::string_view csv, std::vector<SirenSample> &out) {
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

Error SirenSound::load(audio::Mixer &mixer, const SearchSet &files, const std::string &table) {
    stop();
    _samples.clear();
    _sounds.clear();
    std::vector<uint8_t> data;
    if (Error err = files.read(table, data))
        return err;
    if (Error err = readSirenTable(std::string(data.begin(), data.end()), _samples))
        return Error::fail(table + ": " + err.message);
    _mixer = &mixer;
    for (const SirenSample &s : _samples) {
        auto sound = std::make_shared<audio::Sound>();
        const bool ok = (!files.read("aud/aud22/sirens/" + s.wave + ".22k.wav", data) ||
                         !files.read("aud/aud11/" + s.wave + ".11k.wav", data)) &&
                        !audio::decodeSound(data, *sound);
        _sounds.push_back(ok ? sound : nullptr);
    }
    return Error::success();
}

void SirenSound::update(float dt, float volume) {
    if (!_mixer || _samples.empty())
        return;
    if (volume <= 0) {
        stop();
        return;
    }
    _left -= dt;
    if (_current < 0 || _left <= 0) {
        // On to the next sample (the first to start with).
        int next = 0;
        if (_current >= 0) {
            const SirenSample &s = _samples[size_t(_current)];
            next = s.next.empty() ? _current : s.next[_turns++ % s.next.size()].second;
        }
        _current = std::clamp(next, 0, int(_samples.size()) - 1);
        const SirenSample &s = _samples[size_t(_current)];
        _left = s.next.empty() ? 1e9f : std::max(0.2f, s.next[_turns % s.next.size()].first);
        if (_playing != audio::kNoSound)
            _mixer->stop(_playing);
        const auto &sound = _sounds[size_t(_current)];
        _playing = sound ? _mixer->play(sound, audio::SoundType::kEffects, 0, 0, true) : audio::kNoSound;
    }
    if (_playing != audio::kNoSound)
        _mixer->setVolume(_playing, int(std::clamp(volume, 0.0f, 1.0f) * _samples[size_t(_current)].volume *
                                        float(audio::kMaxVolume)));
}

void SirenSound::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
    _current = -1;
    _left = 0;
}

Error readSurfaceSounds(std::string_view csv, SurfaceSounds &out) {
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

Error readImpactSounds(std::string_view csv, std::vector<ImpactSounds> &out) {
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

CarSounds::SoundPtr CarSounds::load(const std::string &wave) const {
    if (wave.empty() || wave == "NOSOUND" || !_files)
        return nullptr;
    std::vector<uint8_t> data;
    for (const char *folder : {"", "engines/", "horns/", "surfaces/", "impacts/", "suspension/"})
        if (!_files->read("aud/aud22/" + std::string(folder) + wave + ".22k.wav", data) ||
            (!*folder && !_files->read("aud/aud11/" + wave + ".11k.wav", data))) {
            auto sound = std::make_shared<audio::Sound>();
            if (!audio::decodeSound(data, *sound))
                return sound;
        }
    return nullptr;
}

Error CarSounds::start(audio::Mixer &mixer, const SearchSet &files, const std::string &car, bool wet) {
    stop();
    _mixer = &mixer;
    _files = &files;
    std::vector<uint8_t> data;
    auto text = [&](const std::string &name) {
        return files.read(name, data) ? std::string() : std::string(data.begin(), data.end());
    };
    Error first = readEngineSound(text("aud/cardata/player/" + car + ".csv"), _car);
    _horn = load(_car.horn);
    _clutch = load(_car.clutch);
    _lastGear = 1;
    if (Error err = readSurfaceSounds(
            text(std::string("aud/cardata/player/default_surface") + (wet ? "wet" : "dry") + ".csv"), _surfaces);
        err && !first)
        first = err;
    _rolling.clear();
    _skidding.clear();
    for (const SurfaceSound &s : _surfaces.surfaces) {
        _rolling.push_back(load(s.wave));
        _skidding.emplace_back();
        for (const SkidSample &k : s.skids)
            _skidding.back().push_back(load(k.wave));
    }
    std::vector<ImpactSounds> impacts;
    if (Error err = readImpactSounds(text("aud/cardata/player/default_impacts.csv"), impacts); err && !first)
        first = err;
    _wall = impacts.empty() ? ImpactSounds{} : impacts.front(); // WALL, the city's walls and other cars
    _wallSounds.clear();
    for (const ImpactSample &s : _wall.samples)
        _wallSounds.push_back(load(s.wave));
    // suspensionaudio.csv: a heading, then the sample, velocities and volumes.
    const auto lines = linesOf(text("aud/cardata/player/suspensionaudio.csv"));
    if (lines.size() > 1) {
        const auto f = fields(lines[1]);
        _suspension = f.empty() ? nullptr : load(f[0]);
        _bumpMin = number(f, 1);
        _bumpMax = number(f, 2);
        _bumpMinVolume = number(f, 3);
        _bumpMaxVolume = number(f, 4);
    }
    return first;
}

void CarSounds::loop(audio::SoundHandle &handle, const SoundPtr &sound, float volume, float pitch) {
    if (volume <= 0.01f || !sound) {
        if (handle != audio::kNoSound)
            _mixer->stop(handle);
        handle = audio::kNoSound;
        return;
    }
    if (handle == audio::kNoSound)
        handle = _mixer->play(sound, audio::SoundType::kEffects, 0, 0, true);
    _mixer->setVolume(handle, int(std::clamp(volume, 0.0f, 1.0f) * float(audio::kMaxVolume)));
    _mixer->setPitch(handle, pitch);
}

void CarSounds::update(float dt, const CarSim &car, bool horn) {
    if (!_mixer)
        return;
    // The horn, held; the clutch on changing gear.
    loop(_hornPlaying, _horn, horn ? _car.hornVolume : 0, 1);
    if (car.gear != _lastGear && _clutch)
        _mixer->play(_clutch, audio::SoundType::kEffects, int(_car.clutchVolume * float(audio::kMaxVolume)));
    _lastGear = car.gear;

    // The tyres: on the surface most of the wheels touching are on.
    int counts[256] = {};
    float slip = 0;
    int touching = 0;
    for (const CarSim::Wheel &w : car.wheels)
        if (w.contact) {
            ++counts[w.surface.sound];
            slip = std::max(slip, w.slip);
            ++touching;
        }
    int surface = -1;
    if (touching) {
        surface = int(std::max_element(std::begin(counts), std::end(counts)) - std::begin(counts));
        if (size_t(surface) >= _surfaces.surfaces.size())
            surface = 0;
    }
    const float speed = length(car.velocity);
    if (surface != _rollSurface) {
        loop(_rollPlaying, nullptr, 0, 1); // another recording
        _rollSurface = surface;
    }
    if (surface >= 0 && !_surfaces.surfaces.empty()) {
        const SurfaceSound &s = _surfaces.surfaces[size_t(surface)];
        const float f = std::clamp(speed / s.maxSpeed, 0.0f, 1.0f);
        loop(_rollPlaying, _rolling[size_t(surface)], speed < 1 ? 0 : s.minVolume + (s.maxVolume - s.minVolume) * f,
             s.minPitch + (s.maxPitch - s.minPitch) * f);
        // The skid whose slip range takes in the tyres', louder as they
        // slide more.
        int sample = -1;
        for (size_t k = 0; k < s.skids.size(); ++k)
            if (slip >= s.skids[k].minSlip && slip <= s.skids[k].maxSlip)
                sample = int(k);
        if (surface != _skidSurface || sample != _skidSample) {
            loop(_skidPlaying, nullptr, 0, 1);
            _skidSurface = surface;
            _skidSample = sample;
        }
        if (sample >= 0 && speed > 2) {
            const float from = s.skids.front().minSlip;
            const float g = std::clamp((slip - from) / std::max(0.01f, 1 - from), 0.0f, 1.0f);
            loop(_skidPlaying, _skidding[size_t(surface)][size_t(sample)],
                 s.minSkidVolume + (s.maxSkidVolume - s.minSkidVolume) * g, 1);
        } else {
            loop(_skidPlaying, nullptr, 0, 1);
        }
    } else {
        loop(_rollPlaying, nullptr, 0, 1);
        loop(_skidPlaying, nullptr, 0, 1);
    }

    // Knocks: the WALL sample for how hard (an impulse, kg m/s, read as the
    // table's force), louder within its range.
    _impactWait = std::max(0.0f, _impactWait - dt);
    if (_impactWait <= 0 && !_wall.samples.empty() && car.impact >= _wall.samples.front().minForce) {
        for (size_t k = 0; k < _wall.samples.size(); ++k) {
            const ImpactSample &s = _wall.samples[k];
            if (car.impact < s.minForce || car.impact > s.maxForce || !_wallSounds[k])
                continue;
            const float g = std::clamp((car.impact - s.minForce) / std::max(1.0f, s.maxForce - s.minForce), 0.0f, 1.0f);
            const audio::SoundHandle h =
                _mixer->play(_wallSounds[k], audio::SoundType::kEffects,
                             int((s.minVolume + (s.maxVolume - s.minVolume) * g) * float(audio::kMaxVolume)));
            if (h != audio::kNoSound)
                _mixer->setPitch(h, s.pitch);
            _impactWait = 0.25f;
            break;
        }
    }

    // The suspension bottoming out or a wheel landing.
    _bumpWait = std::max(0.0f, _bumpWait - dt);
    float bump = 0;
    for (const CarSim::Wheel &w : car.wheels)
        bump = std::max(bump, w.bump);
    if (_suspension && _bumpWait <= 0 && bump >= _bumpMin) {
        const float g = std::clamp((bump - _bumpMin) / std::max(0.01f, _bumpMax - _bumpMin), 0.0f, 1.0f);
        _mixer->play(_suspension, audio::SoundType::kEffects,
                     int((_bumpMinVolume + (_bumpMaxVolume - _bumpMinVolume) * g) * float(audio::kMaxVolume)));
        _bumpWait = 0.3f;
    }
}

void CarSounds::stop() {
    if (_mixer)
        for (audio::SoundHandle *h : {&_hornPlaying, &_rollPlaying, &_skidPlaying})
            if (*h != audio::kNoSound)
                _mixer->stop(*h);
    _hornPlaying = _rollPlaying = _skidPlaying = audio::kNoSound;
    _rollSurface = _skidSurface = _skidSample = -1;
    _mixer = nullptr;
}

} // namespace reluva::age
