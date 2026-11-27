// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The game's music, one song at a time, as the program plays it
 * (docs/engines/stawayteam/systems/maps.md#the-music): a song queued
 * fades the one playing out and starts once its fade is over; a song
 * ending is followed by the next of the mission's Random Songs, shuffled,
 * or with none, by itself again. Nothing else stops it.
 */
class MusicQueue {
public:
    /** Its songs from `data`'s Music/, played through `mixer` (nullptr: none); both must outlive it. */
    MusicQueue(const Archive &data, audio::Mixer *mixer);
    ~MusicQueue();

    /** `song` (of Music/) at once, the one playing stopped. */
    void play(const std::string &song);
    /** `song` after `fadeMs`, the one playing fading out meanwhile (0x4d5c00). */
    void queue(const std::string &song, uint64_t fadeMs, uint64_t now);
    /** The Random Songs that follow a song's end: the current mission's. */
    void setRandomSongs(const std::vector<std::string> &songs);
    /** The song stopped, and none to follow: the game's left. */
    void stop();
    /** Its fade moved on, and the next song started if none's playing. */
    void update(uint64_t now);

    /** The song last started, if any. */
    const std::string &current() const { return _current; }
    /** Whether a song's queued, its fade not over. */
    bool queued() const { return _queued.has_value(); }

private:
    struct Queued {
        std::string song;
        uint64_t start;
        uint64_t fade;
    };

    void start(const std::string &song);
    std::string next();

    const Archive &_data;
    audio::Mixer *_mixer;
    std::shared_ptr<audio::Sound> _sound;
    audio::SoundHandle _handle = audio::kNoSound;
    std::string _current;
    std::optional<Queued> _queued;
    std::vector<std::string> _randomSongs;
    // The Random Songs shuffled, and the next of them to play.
    std::vector<std::string> _playlist;
    size_t _playlistNext = 0;
    unsigned _random = 1;
    size_t _failures = 0; // songs in a row that wouldn't play
};

} // namespace reluva::stawayteam
