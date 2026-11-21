// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include "audio/mixer.h"
#include "engines/idtech3/raven/random.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

/**
 * One piece of a dynamic music set, as `ext_data/dms.dat` describes it
 * (docs/engines/idtech3/games/jk2/systems/sp-music.md): its file, the
 * places it may be entered at, and its ways out into transition pieces.
 */
struct MusicPiece {
    std::string name; // its musicfiles entry, the file's name
    // Entry markers, by name in case-insensitive order: seconds into it.
    std::vector<std::pair<std::string, float>> markers;
    struct Exit {
        std::string nextFile;
        std::string nextMark; // a marker of the set's explore piece; may be empty
    };
    std::vector<Exit> exits;
    // The times (seconds) it may leave at, each with its exit, in order.
    std::vector<std::pair<float, int>> times;

    /** The marker's seconds, if there's one of that name (any case). */
    std::optional<float> marker(std::string_view name) const;
};

/** A level's dynamic music: its pieces and the folders they're in. */
struct MusicSet {
    std::string name;       // as asked for, lower case
    std::string folder;     // the level at the end of its `uses` chain
    std::string bossFolder; // the level its boss piece comes from
    std::optional<MusicPiece> explore;
    std::optional<MusicPiece> action;
    std::optional<MusicPiece> boss;
    MusicPiece death; // always music/death_music.mp3
    // What the reader reports but allows (a boss piece's exits), as
    // s_debugdynamic prints them.
    std::vector<std::string> notes;

    /**
     * The file of track `id` (DynamicMusic's numbering): the explore,
     * action, boss and death pieces 0 to 3, then the action pieces' exits'
     * files 4 to 7 and the explore piece's 8 to 11. Empty if it has none.
     */
    std::string path(int id) const;
};

/**
 * The set named `name` (a path's last part, made lower case) from
 * `dms`, dms.dat's text; `exists` says whether a file is there, as each
 * piece and transition must be. An error says why there's none.
 */
Error findMusicSet(std::string_view dms, std::string_view name, const std::function<bool(const std::string &)> &exists,
                   MusicSet &out);

/**
 * A findMusicSet error as s_debugdynamic prints it (0x42c650): under
 * dms.dat's heading, but a name with no set, which is only a warning
 * (0x42c860).
 */
std::string musicSetReport(const Error &error);
/** Whether a findMusicSet error is a set without both explore and action. */
bool musicSetIncomplete(const Error &error);

/**
 * Jedi Outcast's dynamic music playing (jk2sp.exe's, 0x427c00 on): a set's
 * pieces in memory, the one for the state the game asks for playing,
 * changing at the times the set allows, through its transition pieces,
 * each change a second's fade.
 */
class DynamicMusic {
public:
    // Track numbers: the pieces, the transitions, and no music.
    static constexpr int kExplore = 0;
    static constexpr int kAction = 1;
    static constexpr int kBoss = 2;
    static constexpr int kDeath = 3;
    static constexpr int kActionTransition = 4;  // to 7
    static constexpr int kExploreTransition = 8; // to 11
    static constexpr int kTracks = 12;
    static constexpr int kSilence = 13;

    explicit DynamicMusic(audio::Mixer &mixer) : _mixer(mixer) {}
    ~DynamicMusic() { stop(); }
    DynamicMusic(const DynamicMusic &) = delete;
    DynamicMusic &operator=(const DynamicMusic &) = delete;

    /**
     * Starts `set`, its tracks' sounds by number (none where it has no
     * such track): explore, fading in from its start. False (and nothing
     * playing) without both explore and action.
     */
    bool start(const MusicSet &set, const std::array<std::shared_ptr<const audio::Sound>, kTracks> &sounds,
               uint64_t nowMs);
    void stop();
    bool on() const { return _on; }
    /**
     * The state the game asks for, configstring 907's word: "silence",
     * "action", "boss" and "death" by their starts (any case; boss and
     * death only if the set has them), anything else explore.
     */
    void request(std::string_view state);
    /** A track asked for by number (S_SetDynamicMusicState, 0x4272c0). */
    void requestTrack(int id);
    /** Whether the set has a track's piece, loaded. */
    bool has(int id) const { return loaded(id); }
    /** The state asked for and the one playing, as s_info and s_dynamic print them (0x423120). */
    std::string status() const;
    /** A frame: the change asked for made when it may be, fades moved on. */
    void update(uint64_t nowMs);
    int current() const { return _current; }
    int requested() const { return _requested; }
    const MusicSet &set() const { return _set; }
    /** A track's name ("explore", "action_tr2", "silence"...; "<unknown>" for none). */
    static std::string trackName(int id);

    /**
     * Seconds the original's stream is decoded ahead of what's heard, from
     * which it measures where a piece has got to: its 16384 samples at the
     * output's rate (0x428009). 0: measured from what's heard.
     */
    double decodeLead = 0;
    /** s_debugdynamic: 1 prints each request and switch, 2 the state each frame too. */
    int debug = 0;
    /** Where those prints go. */
    std::function<void(const std::string &)> print;
    /** The C library's rand() the markers are drawn with (0x42d992); unset, its own. */
    std::function<int32_t()> rand;

private:
    struct Fade {
        audio::SoundHandle handle = audio::kNoSound;
        uint64_t startMs = 0;
        int target = 0; // 0 or 255
        int volume = 0; // where it started from
        float smoothed = 0.25f;
    };
    bool loaded(int id) const { return id >= 0 && id < kTracks && _sounds[size_t(id)]; }
    double seconds(int id) const;   // how far the current track has played
    double remaining(int id) const; // and how long it has left
    void switchTo(int id, bool instant, uint64_t nowMs);
    bool exitPoint(int state, int &track, float &entry) const;
    float randomMarker(int state);
    void setVolume(Fade &f, uint64_t nowMs);

    audio::Mixer &_mixer;
    MusicSet _set;
    std::array<std::shared_ptr<const audio::Sound>, kTracks> _sounds{};
    std::array<double, kTracks> _positions{}; // seconds each track stays at
    bool _on = false;
    int _current = kExplore;
    int _requested = kExplore;
    Fade _playing; // the current track
    Fade _fading;  // the one before, fading out
    // A transition playing: the track it hands over to, and where in it.
    bool _pending = false;
    int _next = kExplore;
    float _entry = 0;
    int _lastMarker = -1; // the random marker chosen last
    int _markerCount = 0;
    raven::CrtRandom _ownRandom;
};

} // namespace reluva::idtech3
