// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/directmusic.h"
#include "common/audio/mixer.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

// One tune's segments by game state: a row of
// aud/dmusic/csv_files/singlerace.csv or singleroam.csv.
struct MusicSet {
    std::string start, back, idle, idleCops, cops, pause, results;
    // The big-air motif: its style, its name in it, its band.
    std::string airStyle, airMotif, airBand;
};
// The rows of one of those files (the race file's columns: start,
// return, idle, idle with cops, cop chase, pause, results; the cruising
// file's: start, return, idle, cop chase, idle with cops, pause).
std::vector<MusicSet> parseMusicSets(const std::string& text, bool race);

// The game's music: one tune's segments, switched (at the next measure)
// by what's going on: started, stopped a while, chased by the police,
// back on the move, paused, the results. See docs/engines/age-racing.md.
class GameMusic {
public:
    ~GameMusic() { stop(); }

    // A tune picked from the race or cruising list (`seed` picks which).
    Error start(const SearchSet& files, audio::Mixer& mixer, bool race, uint32_t seed);
    // The menus' tune: aud/dmusic/csv_files/ui.csv's segment (UI.sgt).
    Error startMenu(const SearchSet& files, audio::Mixer& mixer);
    bool playingMenu() const { return menu_ && performance_; }
    void stop();

    struct State {
        float speed = 0;     // the player's, m/s
        bool chased = false; // police after the player
        bool paused = false;
        bool finished = false; // the race is over: the results
    };
    void update(float dt, const State& state);
    // A big jump: the tune's big-air motif over it.
    void bigAir();

    // The segment playing or about to (for tests and the page's notes).
    const std::string& segment() const { return wanted_; }

private:
    void want(const std::string& segment);

    audio::Mixer* mixer_ = nullptr;
    std::shared_ptr<audio::DmPerformance> performance_;
    MusicSet set_;
    std::string wanted_;
    float still_ = 0; // seconds stopped
    bool idle_ = false, wasChased_ = false, wasPaused_ = false;
    bool menu_ = false;
};

} // namespace reluva::age
