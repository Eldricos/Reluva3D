// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/music.h"

#include <sstream>

namespace reluva::age {

namespace {

constexpr float kIdleAfter = 4;  // seconds stopped before the idle music
constexpr float kStopped = 1.5f; // m/s: below this, stopped
constexpr float kMoving = 5;     // and above this, on the move again

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string f;
    while (std::getline(in, f, ','))
        out.push_back(f);
    return out;
}

} // namespace

std::vector<MusicSet> parseMusicSets(const std::string& text, bool race) {
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
        out.push_back(s);
    }
    return out;
}

Error GameMusic::start(const SearchSet& files, audio::Mixer& mixer, bool race, uint32_t seed) {
    stop();
    std::vector<uint8_t> data;
    const std::string list = race ? "aud/dmusic/csv_files/singlerace.csv" : "aud/dmusic/csv_files/singleroam.csv";
    if (Error err = files.read(list, data))
        return err;
    const std::vector<MusicSet> sets = parseMusicSets(std::string(data.begin(), data.end()), race);
    if (sets.empty())
        return Error::fail(list + ": no music");
    set_ = sets[seed % sets.size()];
    const SearchSet* source = &files;
    performance_ = std::make_shared<audio::DmPerformance>(
        mixer.outputRate(),
        [source](const std::string& name, std::vector<uint8_t>& out) {
            return !source->read("aud/dmusic/" + name, out);
        },
        seed);
    mixer_ = &mixer;
    // Every segment the tune may switch to, read now rather than mid-race.
    for (const std::string* seg : {&set_.back, &set_.idle, &set_.idleCops, &set_.cops, &set_.pause, &set_.results})
        if (!seg->empty())
            performance_->prepare(*seg + ".sgt");
    mixer.addStream(performance_, audio::SoundType::Music);
    still_ = 0;
    idle_ = wasChased_ = wasPaused_ = false;
    wanted_.clear();
    want(set_.start);
    return Error::success();
}

void GameMusic::stop() {
    if (mixer_ && performance_)
        mixer_->removeStream(performance_.get());
    performance_.reset();
    mixer_ = nullptr;
    wanted_.clear();
}

void GameMusic::want(const std::string& segment) {
    if (!performance_ || segment.empty() || segment == wanted_)
        return;
    wanted_ = segment;
    performance_->play(segment + ".sgt");
}

void GameMusic::update(float dt, const State& s) {
    if (!performance_)
        return;
    if (s.finished) {
        want(set_.results);
        return;
    }
    if (s.paused) {
        want(set_.pause);
        wasPaused_ = true;
        return;
    }
    still_ = s.speed < kStopped ? still_ + dt : 0;
    const bool wasIdle = idle_;
    if (still_ > kIdleAfter)
        idle_ = true;
    else if (s.speed > kMoving)
        idle_ = false;
    if (idle_) {
        want(s.chased ? set_.idleCops : set_.idle);
    } else if (s.chased) {
        want(set_.cops);
    } else if (wasIdle || wasChased_ || wasPaused_) {
        // On the move again, out of a chase or back from the pause.
        want(set_.back);
    }
    wasChased_ = s.chased;
    wasPaused_ = false;
}

} // namespace reluva::age
