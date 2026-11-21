// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/system.h"
#include "engines/botf/formats/snd_archive.h"
#include "engines/botf/game_files.h"
#include "engines/botf/ui/layout_screen.h"
#include "engines/botf/ui/options_screen.h"

#include <map>
#include <memory>
#include <random>

// The main menu's music and sounds: music.snd's first piece, its segments
// shuffled, under every screen the menu opens; sfx.snd's effects and
// english.snd's speech for the screens' controls and for going from one
// screen to another; and the options' volumes. Which sound goes with what
// was found in a recording of the game (docs/engines/botf/systems/sound.md).

namespace reluva::botf {

/**
 * The menu music's next segment after `last` (-1 at the start): 0 or 1 to
 * begin with, then any of 2, 3 and 4 but the one just played (recordings,
 * eight minutes of it).
 */
int nextSegment(int last, std::mt19937 &random);

/** The main menu's music and sounds. Without an audio device, or the archives, it's silent. */
class MenuAudio {
public:
    /** Starts the System's audio output, if there is one. */
    explicit MenuAudio(System &system);
    ~MenuAudio();
    MenuAudio(const MenuAudio &) = delete;
    MenuAudio &operator=(const MenuAudio &) = delete;

    /** Opens the archives `files` finds: sfx.snd, the speech archive, music.snd. */
    void open(const GameFiles &files);
    /** The music, speech and effects volumes, as the options set them. */
    void setVolumes(const GameOptions &options);
    /**
     * While a movie plays: its sound, which the player plays as music, at
     * the video volume. setVolumes() puts the music's back.
     */
    void setMovieVolume(const GameOptions &options);
    /** The mixer, for a movie's sound; nullptr without an audio device. */
    audio::Mixer *mixer() { return _output ? _mixer.get() : nullptr; }
    /** Starts the menu's music, if it isn't playing. */
    void startMusic();
    /** Keeps the music's segments queued; call every frame. */
    void update();

    /** Plays a screen control's sound. */
    void play(UiSound sound);
    /** Going to a screen from the main menu, or back to it. */
    enum class Move { kToSetup, kToEmpires, kToOptions, kToLoad, kToSave, kToHallOfFame, kToMultiplayer, kToMenu };
    /** Plays a move's sounds, effect and speech, as the game does. */
    void play(Move move);

private:
    void effect(size_t entry, int volume = audio::kMaxVolume);
    void speech(size_t entry);

    System &_sys;
    std::unique_ptr<audio::Mixer> _mixer;
    bool _output = false;
    SndArchive _effects;
    SndArchive _speech;
    SndArchive _music;
    bool _haveEffects = false;
    bool _haveSpeech = false;
    bool _haveMusic = false;
    std::map<size_t, std::shared_ptr<const audio::Sound>> _decoded; // effects and speech, by archive and entry
    std::shared_ptr<audio::SoundQueue> _queue;
    int _lastSegment = -1;
    int _videoVolume = audio::kMaxVolume;
    std::mt19937 _random;
};

} // namespace reluva::botf
