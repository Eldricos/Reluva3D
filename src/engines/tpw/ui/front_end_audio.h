// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "audio/stream.h"
#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/sdt.h"
#include "engines/tpw/formats/sound_map.h"
#include "engines/tpw/sound_effects.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::tpw {

/**
 * The front end's music and the advisor's speech: the Intro World's
 * music, piece after piece from one of its groups, and the advisor's three
 * lines at their times. See
 * docs/engines/tpw/games/simcoaster/systems/front-end.md#the-music-and-the-speech.
 */
class FrontEndAudio {
public:
    static constexpr const char *kMusicFolder = "levels/wIntro/Music";
    /**
     * The values the music follows its groups' links by *(guessed)*: one
     * in group 1's range (0 to 10) while the camera moves, one in group
     * 2's (11 to 26) over the globe, as the recording's music goes.
     */
    static constexpr int kPathValue = 0;
    static constexpr int kGlobeValue = 20;

    /** Reads the music's category and the speech's. Fails if either can't be read. */
    Error load(const SearchSet &files);

    /**
     * Where it plays; nullptr (the default) to play nothing, only keep
     * count of what it would. The mixer must outlive it, or be unset first.
     */
    void setMixer(audio::Mixer *mixer);

    /**
     * Brings it to a time, in milliseconds from the start of the camera's
     * path: starts the advisor's line due, if its time to show hasn't
     * passed, and keeps the music's next pieces queued.
     */
    void update(uint32_t ms);
    /**
     * The value the music's next piece follows its group's links by, 0 to
     * 100; with `cut`, the piece playing gives way to one of the group it
     * leads to at once, if that's another.
     */
    void setMusicValue(int value, bool cut);
    /** The group the music's pieces come from now, from 0. */
    size_t musicGroup() const { return _group; }
    /** Stops the line the advisor is saying; with `forGood`, he says no more. */
    void stopSpeech(bool forGood);
    /** Stops the music and the speech. */
    void stop();

    /** The advisor's lines started, by their numbers, in order. */
    const std::vector<uint32_t> &linesStarted() const { return _linesStarted; }
    /** The music's pieces queued, by their entries in the bank, in order. */
    const std::vector<uint32_t> &piecesQueued() const { return _piecesQueued; }

private:
    void refill();
    uint16_t roll();
    size_t follow(size_t group) const;

    const SearchSet *_files = nullptr;
    audio::Mixer *_mixer = nullptr;

    SoundMap _music;
    std::string _musicBankFile;
    SoundBank _musicBank;
    bool _musicBankRead = false;
    size_t _group = 0;
    int _value = kPathValue;
    bool _started = false; // a piece queued: the next follows a link
    std::shared_ptr<audio::QueuingStream> _stream;
    audio::SoundHandle _musicHandle = audio::kNoSound;
    std::vector<uint32_t> _piecesQueued;

    SoundEffects _speech;
    bool _speechLoaded = false;
    audio::SoundHandle _line = audio::kNoSound;
    size_t _nextLine = 0;
    bool _speechOver = false;
    std::vector<uint32_t> _linesStarted;
    uint32_t _seed = 1;
};

} // namespace reluva::tpw
