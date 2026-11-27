// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/music.h"

#include "audio/decode.h"

#include <cmath>
#include <utility>

namespace reluva::stawayteam {

namespace {

// The song playing as another's queued: down to -33.33 dB over the fade,
// evenly in dB (0x4d5cb0, -3333 at 0x621ba0).
int fadeVolume(uint64_t gone, uint64_t fade) {
    const float t = fade ? float(gone) / float(fade) : 1.0f;
    return int(float(audio::kMaxVolume) * std::pow(10.0f, -33.33f * t / 20.0f));
}

} // namespace

MusicQueue::MusicQueue(const Archive &data, audio::Mixer *mixer) : _data(data), _mixer(mixer) {
}

MusicQueue::~MusicQueue() {
    if (_mixer && _handle != audio::kNoSound)
        _mixer->stop(_handle);
}

void MusicQueue::play(const std::string &song) {
    _queued.reset();
    _failures = 0;
    start(song);
}

void MusicQueue::queue(const std::string &song, uint64_t fadeMs, uint64_t now) {
    _queued = Queued{song, now, fadeMs};
}

void MusicQueue::stop() {
    _queued.reset();
    if (_mixer && _handle != audio::kNoSound)
        _mixer->stop(_handle);
    _handle = audio::kNoSound;
    _failures = _randomSongs.size() + 1;
}

void MusicQueue::setRandomSongs(const std::vector<std::string> &songs) {
    // Each mission keeps its own shuffled list (0x507460).
    if (songs == _randomSongs)
        return;
    _randomSongs = songs;
    _playlist.clear();
    _playlistNext = 0;
}

void MusicQueue::start(const std::string &song) {
    if (_mixer && _handle != audio::kNoSound)
        _mixer->stop(_handle);
    _handle = audio::kNoSound;
    _sound.reset();
    _current = song;
    std::vector<uint8_t> bytes;
    auto sound = std::make_shared<audio::Sound>();
    if (!_mixer || song.empty() || song == "!None.ogg" || _data.read("Music/" + song, bytes) ||
        audio::decodeSound(bytes, *sound))
        return;
    _sound = std::move(sound);
    _handle = _mixer->play(_sound, audio::SoundType::kMusic, audio::kMaxVolume, 0, false);
}

std::string MusicQueue::next() {
    // The next of the Random Songs (0x5bcbd0, 0x507460); the list played
    // through, shuffled again: each in turn swapped with one chosen by
    // chance, and one that would come straight after itself put last
    // (0x507380). Without them, the last song again (0x5bcbeb).
    if (_randomSongs.empty())
        return _current;
    if (_playlistNext >= _playlist.size()) {
        const std::string previous = _playlist.empty() ? std::string() : _playlist.back();
        _playlist = _randomSongs;
        for (size_t i = 0; i < _playlist.size(); ++i) {
            _random = _random * 1103515245u + 12345u;
            std::swap(_playlist[i], _playlist[(_random >> 16) % _playlist.size()]);
        }
        if (!previous.empty() && _playlist.front() == previous)
            std::swap(_playlist.front(), _playlist.back());
        _playlistNext = 0;
    }
    return _playlist[_playlistNext++];
}

void MusicQueue::update(uint64_t now) {
    if (!_mixer)
        return;
    if (_queued) {
        const uint64_t gone = now - std::min(now, _queued->start);
        if (gone < _queued->fade) {
            if (_handle != audio::kNoSound)
                _mixer->setVolume(_handle, fadeVolume(gone, _queued->fade));
            return;
        }
        // Started at full once the fade's over, whatever was playing
        // (0x4d5640).
        const std::string song = std::move(_queued->song);
        _queued.reset();
        _failures = 0;
        start(song);
        return;
    }
    if (_handle != audio::kNoSound && _mixer->isPlaying(_handle))
        return;
    // Songs that won't play aren't tried over and over.
    if (_handle == audio::kNoSound && _failures > _randomSongs.size())
        return;
    start(next());
    _failures = _handle == audio::kNoSound ? _failures + 1 : 0;
}

} // namespace reluva::stawayteam
