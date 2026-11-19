// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "audio/directmusic.h"
#include "audio/mixer.h"

#include <memory>
#include <string>
#include <vector>

namespace reluva::age {

/**
 * One tune's segments by game state: a row of
 * aud/dmusic/csv_files/singlerace.csv or singleroam.csv.
 */
struct MusicSet {
    std::string start;
    std::string back;
    std::string idle;
    std::string idleCops;
    std::string cops;
    std::string pause;
    std::string results;
    // The big-air motif: its style, its name in it, its band.
    std::string airStyle;
    std::string airMotif;
    std::string airBand;
};
/**
 * The rows of one of those files (the race file's columns: start,
 * return, idle, idle with cops, cop chase, pause, results; the cruising
 * file's: start, return, idle, cop chase, idle with cops, pause).
 */
std::vector<MusicSet> parseMusicSets(const std::string &text, bool race);

/**
 * The game's music: one tune's segments, switched (at the next measure)
 * by what's going on: started, stopped a while, chased by the police,
 * back on the move, paused, the results. See docs/engines/age/games/mm2/systems/music.md.
 */
class GameMusic {
public:
    ~GameMusic() { stop(); }

    /**
     * A tune picked from the race or cruising list (`seed` picks which).
     * The same tune asked for again while it plays (a race restarted)
     * plays on, as the game's does.
     */
    Error start(const SearchSet &files, audio::Mixer &mixer, bool race, uint32_t seed);
    /**
     * The CD player's tracks (Reluva's: the game's player played an audio
     * CD): every tune of the race list, then of the cruising list, by its
     * start segment's name less "Start" ("Enemy", "SunRoof"...).
     */
    static std::vector<std::string> trackNames(const SearchSet &files);
    /**
     * Plays one of trackNames()' tunes.
     * @return A failure if there is no such track.
     */
    Error startTrack(const SearchSet &files, audio::Mixer &mixer, size_t track);
    /** The menus' tune: aud/dmusic/csv_files/ui.csv's segment (UI.sgt). */
    Error startMenu(const SearchSet &files, audio::Mixer &mixer);
    /** Whether the menus' tune is the one playing. */
    bool playingMenu() const { return _menu && _performance; }
    /** Stops the music; start() then plays its tune from the beginning. */
    void stop();

    /** What's going on in the game, as the music follows it. */
    struct State {
        float speed = 0;     // the player's, m/s
        bool chased = false; // police after the player
        bool paused = false;
        bool finished = false;    // the race is over: the results
        bool lastStretch = false; // a race's final lap, or only the finish left
    };
    /**
     * Switches to the segment for the state: the results, the pause, idle
     * (with or without the police) after 4 s stopped, the chase
     * (and a race's last stretch), or the return to the road after any of those.
     */
    void update(float dt, const State &state);
    /** A big jump: the tune's big-air motif over it. */
    void bigAir();

    /** The segment playing or about to (for tests and the page's notes). */
    const std::string &segment() const { return _wanted; }

private:
    void want(const std::string &segment);

    audio::Mixer *_mixer = nullptr;
    std::shared_ptr<audio::DmPerformance> _performance;
    audio::SoundHandle _handle = audio::kNoSound; // its stream in the mixer
    MusicSet _set;
    std::string _wanted;
    float _still = 0; // seconds stopped
    bool _idle = false;
    bool _wasChased = false;
    bool _wasPaused = false;
    bool _menu = false;
    bool _race = false;
    uint32_t _seed = 0;
};

} // namespace reluva::age
