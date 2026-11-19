// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/audio/music.h"

#include <sstream>

namespace reluva::age {

namespace {

constexpr float kIdleAfter = 4;  // seconds stopped before the idle music
constexpr float kStopped = 1.5f; // m/s: below this, stopped
constexpr float kMoving = 5;     // and above this, on the move again

std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string f;
    while (std::getline(in, f, ','))
        out.push_back(f);
    return out;
}

} // namespace

std::vector<MusicSet> parseMusicSets(const std::string &text, bool race) {
    std::vector<MusicSet> out;
    std::istringstream in(text);
    std::string line;
    std::getline(in, line); // the header
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        const std::vector<std::string> f = fields(line);
        if (f.size() < 6 || f[0].empty())
            continue;
        MusicSet s;
        s.start = f[0];
        s.back = f[1];
        s.idle = f[2];
        if (race) {
            s.idleCops = f[3];
            s.cops = f[4];
            s.pause = f[5];
            s.results = f.size() > 6 ? f[6] : std::string();
        } else {
            s.cops = f[3];
            s.idleCops = f[4];
            s.pause = f[5];
        }
        // Then the big-air motif's style, name and band (after the results
        // in the race file).
        const size_t air = race ? 7 : 6;
        if (f.size() > air + 2) {
            s.airStyle = f[air];
            s.airMotif = f[air + 1];
            s.airBand = f[air + 2];
        }
        out.push_back(s);
    }
    return out;
}

namespace {

std::vector<MusicSet> readSets(const SearchSet &files, bool race) {
    std::vector<uint8_t> data;
    const std::string list = race ? "aud/dmusic/csv_files/singlerace.csv" : "aud/dmusic/csv_files/singleroam.csv";
    if (files.read(list, data))
        return {};
    return parseMusicSets(std::string(data.begin(), data.end()), race);
}

} // namespace

std::vector<std::string> GameMusic::trackNames(const SearchSet &files) {
    std::vector<std::string> names;
    for (bool race : {true, false})
        for (const MusicSet &s : readSets(files, race)) {
            std::string n = s.start;
            if (n.size() > 5 && n.compare(n.size() - 5, 5, "Start") == 0)
                n.resize(n.size() - 5);
            names.push_back(n);
        }
    return names;
}

Error GameMusic::startTrack(const SearchSet &files, audio::Mixer &mixer, size_t track) {
    const std::vector<MusicSet> race = readSets(files, true);
    const bool isRace = track < race.size();
    const std::vector<MusicSet> sets = isRace ? race : readSets(files, false);
    const size_t index = isRace ? track : track - race.size();
    if (index >= sets.size())
        return Error::fail("no such track");
    return start(files, mixer, isRace, uint32_t(index));
}

Error GameMusic::start(const SearchSet &files, audio::Mixer &mixer, bool race, uint32_t seed) {
    if (_performance && !_menu && _mixer == &mixer && race == _race && seed == _seed) {
        _still = 0;
        _idle = _wasPaused = false;
        return Error::success();
    }
    stop();
    _race = race;
    _seed = seed;
    const std::vector<MusicSet> sets = readSets(files, race);
    if (sets.empty())
        return Error::fail(std::string(race ? "singlerace" : "singleroam") + ".csv: no music");
    _set = sets[seed % sets.size()];
    const SearchSet *source = &files;
    _performance = std::make_shared<audio::DmPerformance>(
        mixer.outputRate(),
        [source](const std::string &name, std::vector<uint8_t> &out) {
            return !source->read("aud/dmusic/" + name, out);
        },
        seed);
    _mixer = &mixer;
    // Every segment the tune may switch to, read now rather than mid-race.
    for (const std::string *seg : {&_set.back, &_set.idle, &_set.idleCops, &_set.cops, &_set.pause, &_set.results})
        if (!seg->empty())
            _performance->prepare(*seg + ".sgt");
    mixer.addStream(_performance, audio::SoundType::kMusic);
    _still = 0;
    _idle = _wasChased = _wasPaused = false;
    _wanted.clear();
    want(_set.start);
    return Error::success();
}

Error GameMusic::startMenu(const SearchSet &files, audio::Mixer &mixer) {
    stop();
    // "Music segment" then its name.
    std::string segment = "UI";
    std::vector<uint8_t> data;
    if (!files.read("aud/dmusic/csv_files/ui.csv", data)) {
        const std::vector<std::string> lines = [&] {
            std::vector<std::string> out;
            std::istringstream in(std::string(data.begin(), data.end()));
            for (std::string l; std::getline(in, l);) {
                while (!l.empty() && (l.back() == '\r' || l.back() == ' '))
                    l.pop_back();
                if (!l.empty())
                    out.push_back(l);
            }
            return out;
        }();
        if (lines.size() >= 2)
            segment = lines[1];
    }
    const SearchSet *source = &files;
    _performance = std::make_shared<audio::DmPerformance>(
        mixer.outputRate(),
        [source](const std::string &name, std::vector<uint8_t> &out) {
            return !source->read("aud/dmusic/" + name, out);
        },
        1);
    _mixer = &mixer;
    mixer.addStream(_performance, audio::SoundType::kMusic);
    _wanted = segment;
    _menu = true;
    return _performance->play(segment + ".sgt");
}

void GameMusic::stop() {
    if (_mixer && _performance)
        _mixer->removeStream(_performance.get());
    _performance.reset();
    _mixer = nullptr;
    _menu = false;
    _wanted.clear();
}

void GameMusic::want(const std::string &segment) {
    if (!_performance || segment.empty() || segment == _wanted)
        return;
    _wanted = segment;
    _performance->play(segment + ".sgt");
}

void GameMusic::bigAir() {
    if (_performance && !_menu && !_set.airStyle.empty() && !_set.airMotif.empty())
        _performance->playMotif(_set.airStyle + ".sty", _set.airMotif,
                                _set.airBand.empty() ? "" : _set.airBand + ".bnd");
}

void GameMusic::update(float dt, const State &s) {
    if (!_performance)
        return;
    if (s.finished) {
        want(_set.results);
        return;
    }
    if (s.paused) {
        want(_set.pause);
        _wasPaused = true;
        return;
    }
    _still = s.speed < kStopped ? _still + dt : 0;
    const bool wasIdle = _idle;
    if (_still > kIdleAfter)
        _idle = true;
    else if (s.speed > kMoving)
        _idle = false;
    if (_idle) {
        want(s.chased ? _set.idleCops : _set.idle);
    } else if (s.chased || s.lastStretch) {
        // The race's last stretch speeds up too: the chase segment (see
        // docs/engines/age-footage.md).
        want(_set.cops);
    } else if (wasIdle || _wasChased || _wasPaused) {
        // On the move again, out of a chase or back from the pause.
        want(_set.back);
    }
    _wasChased = s.chased || s.lastStretch;
    _wasPaused = false;
}

} // namespace reluva::age
