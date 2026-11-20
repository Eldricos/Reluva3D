// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/menu_audio.h"

#include <array>
#include <chrono>

namespace reluva::botf {

namespace {

// The menu's music: music.snd's piece 0, five segments.
constexpr size_t kMenuPiece = 0;

// sfx.snd's entries, found in a recording of the game by their sound.
// Several entries hold the same sound (50, 55 and 97; 66, 137 and 142; 87
// and 107): which one the game plays isn't known, and the first is taken.
constexpr size_t kClickEffect = 87;      // most screens' buttons; opening Options and Load Game
constexpr size_t kSetupClickEffect = 66; // the game setup screen's controls
constexpr size_t kBackEffect = 50;       // back to the main menu; the effects volume's sample
constexpr size_t kFameEffect = 75;       // opening the Hall of Fame
constexpr size_t kVideoEffect = 162;     // the video volume's sample
// english.snd's: opening the game setup screen; a saved game chosen; and
// the voice volume's samples, one at random (these three heard).
constexpr size_t kSetupSpeech = 1362;
constexpr size_t kGameChosenSpeech = 323;
constexpr std::array<size_t, 3> kVoiceSamples = {351, 621, 725};

// Effects and speech share _decoded's keys: speech's are offset by this.
constexpr size_t kSpeechKeys = 1u << 20;

// The game's volumes run 0-127 (Stbof.ini, the sliders), the mixer's to
// kMaxVolume, as recorded.
int mixerVolume(int volume) {
    return volume * audio::kMaxVolume / GameOptions::kMaxVolume;
}

} // namespace

int nextSegment(int count, int last, std::mt19937 &random) {
    if (count <= 1)
        return 0;
    std::uniform_int_distribution<int> pick(0, last >= 0 && last < count ? count - 2 : count - 1);
    const int n = pick(random);
    return last >= 0 && n >= last ? n + 1 : n;
}

MenuAudio::MenuAudio(System &system)
    : _sys(system), _random(uint32_t(std::chrono::steady_clock::now().time_since_epoch().count())) {
    _mixer = std::make_unique<audio::Mixer>(_sys.audioRate());
    _output = !audio::startMixerOutput(_sys, *_mixer);
}

MenuAudio::~MenuAudio() {
    if (_output)
        _sys.stopAudio();
}

void MenuAudio::open(const GameFiles &files) {
    if (const auto p = files.find("sfx.snd"))
        _haveEffects = !_effects.open(*p, false);
    if (const auto p = files.find(files.speechFile()))
        _haveSpeech = !_speech.open(*p, false);
    if (const auto p = files.find("music.snd"))
        _haveMusic = !_music.open(*p, true) && _music.pieces().size() > kMenuPiece;
}

void MenuAudio::setVolumes(const GameOptions &options) {
    _mixer->setTypeVolume(audio::SoundType::kSpeech, mixerVolume(options.volume[0]));
    _mixer->setTypeVolume(audio::SoundType::kMusic, mixerVolume(options.volume[2]));
    _mixer->setTypeVolume(audio::SoundType::kEffects, mixerVolume(options.volume[3]));
    _videoVolume = mixerVolume(options.volume[1]);
}

void MenuAudio::startMusic() {
    if (!_output || !_haveMusic || _queue)
        return;
    _queue = std::make_shared<audio::SoundQueue>();
    update();
    _mixer->playOutput(_queue, audio::SoundType::kMusic);
}

void MenuAudio::update() {
    if (!_queue)
        return;
    // Two segments ahead: the next is decoded well before it's needed.
    const std::vector<size_t> &segments = _music.pieces()[kMenuPiece];
    while (_queue->queued() < 2) {
        _lastSegment = nextSegment(int(segments.size()), _lastSegment, _random);
        auto sound = std::make_shared<audio::Sound>();
        if (_music.decode(segments[size_t(_lastSegment)], kDefaultAdpcmRate, *sound))
            return;
        _queue->push(std::move(sound), _lastSegment);
    }
}

void MenuAudio::effect(size_t entry, int volume) {
    if (!_output || !_haveEffects || entry >= _effects.entries().size())
        return;
    auto &sound = _decoded[entry];
    if (!sound) {
        auto s = std::make_shared<audio::Sound>();
        if (_effects.decode(entry, kDefaultAdpcmRate, *s))
            return;
        sound = std::move(s);
    }
    _mixer->play(sound, audio::SoundType::kEffects, volume);
}

void MenuAudio::speech(size_t entry) {
    if (!_output || !_haveSpeech || entry >= _speech.entries().size())
        return;
    auto &sound = _decoded[kSpeechKeys + entry];
    if (!sound) {
        auto s = std::make_shared<audio::Sound>();
        if (_speech.decode(entry, kDefaultAdpcmRate, *s))
            return;
        sound = std::move(s);
    }
    _mixer->play(sound, audio::SoundType::kSpeech);
}

void MenuAudio::play(UiSound sound) {
    switch (sound) {
    case UiSound::kClick:
        effect(kClickEffect);
        break;
    case UiSound::kSetupClick:
        effect(kSetupClickEffect);
        break;
    case UiSound::kGameChosen:
        speech(kGameChosenSpeech);
        break;
    case UiSound::kVoiceVolume:
        speech(kVoiceSamples[std::uniform_int_distribution<size_t>(0, kVoiceSamples.size() - 1)(_random)]);
        break;
    case UiSound::kVideoVolume:
        // At the video volume; the mixer has no type for movies' sound, so
        // the effects' volume scales it too.
        effect(kVideoEffect, _videoVolume);
        break;
    case UiSound::kEffectsVolume:
        effect(kBackEffect);
        break;
    }
}

void MenuAudio::play(Move move) {
    switch (move) {
    case Move::kToSetup:
        speech(kSetupSpeech);
        break;
    case Move::kToOptions:
    case Move::kToLoad:
    case Move::kToSave:
        effect(kClickEffect);
        break;
    case Move::kToHallOfFame:
        effect(kFameEffect);
        break;
    case Move::kToMultiplayer:
        break; // silent, in the recording
    case Move::kToMenu:
        effect(kBackEffect);
        break;
    }
}

} // namespace reluva::botf
