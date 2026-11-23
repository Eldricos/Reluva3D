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
    /** The music's groups: the first while the camera moves, the second over the globe. */
    static constexpr size_t kPathGroup = 0;
    static constexpr size_t kGlobeGroup = 1;

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
    /** Cuts the music over to one of its groups; the same group goes on. */
    void setMusicGroup(size_t group);
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

    const SearchSet *_files = nullptr;
    audio::Mixer *_mixer = nullptr;

    SoundMap _music;
    std::string _musicBankFile;
    SoundBank _musicBank;
    bool _musicBankRead = false;
    size_t _group = kPathGroup;
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
