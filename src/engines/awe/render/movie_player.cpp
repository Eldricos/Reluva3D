// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/render/movie_player.h"

#include <algorithm>

namespace reluva::awe {

Error MoviePlayer::open(std::vector<uint8_t> data, uint64_t nowMs, bool sound) {
    stop();
    _movie.reset();
    _soundtrack.reset();
    _frame = image::Image();
    _next = 0;
    _paused = false;
    _error.clear();
    auto movie = std::make_unique<bink::Movie>();
    if (Error e = movie->open(std::move(data)))
        return e;
    // The soundtrack, decoded whole and played alongside.
    if (sound && movie->audioRate() > 0) {
        auto track = std::make_shared<audio::Sound>();
        if (Error ae = movie->decodeAllAudio(track->samples)) {
            _error = "sound: " + ae.message;
        } else {
            track->rate = movie->audioRate();
            track->channels = movie->audioChannels();
            track->format = 0;
            _soundtrack = track;
        }
    }
    _movie = std::move(movie);
    _startMs = nowMs;
    playSound(0);
    return Error::success();
}

double MoviePlayer::fps() const {
    return _movie && _movie->rateDenominator() ? double(_movie->rateNumerator()) / _movie->rateDenominator() : 0;
}

void MoviePlayer::playSound(double fromSeconds) {
    if (!_soundtrack || !_mixer)
        return;
    std::shared_ptr<audio::Sound> s = _soundtrack;
    if (fromSeconds > 0) {
        // The rest of it, from that time.
        s = std::make_shared<audio::Sound>(*_soundtrack);
        const size_t from = std::min(s->samples.size(), size_t(fromSeconds * s->rate) * size_t(s->channels));
        s->samples.erase(s->samples.begin(), s->samples.begin() + ptrdiff_t(from));
    }
    _sound = _mixer->play(s, audio::SoundType::kSpeech);
}

void MoviePlayer::stop() {
    if (_mixer && _sound != audio::kNoSound)
        _mixer->stop(_sound);
    _sound = audio::kNoSound;
}

void MoviePlayer::pause(bool paused, uint64_t nowMs) {
    if (!_movie || paused == _paused)
        return;
    _paused = paused;
    if (paused) {
        stop();
    } else {
        // Carry on from the frame shown, the sound from the same time.
        _startMs = nowMs - uint64_t(seconds() * 1000.0);
        playSound(seconds());
    }
}

double MoviePlayer::seconds() const {
    return fps() > 0 ? double(_next) / fps() : 0;
}

bool MoviePlayer::update(uint64_t nowMs) {
    if (!_movie || _paused || _movie->frameCount() == 0 || fps() <= 0)
        return false;
    const size_t due = size_t(double(nowMs - std::min(nowMs, _startMs)) * fps() / 1000.0);
    bool drawn = false;
    while (_next <= due && _next < _movie->frameCount()) {
        bool more = false;
        if (Error e = _movie->nextFrame(more))
            _error = "frame " + std::to_string(_next) + ": " + e.message;
        _next++;
        drawn = true;
    }
    if (drawn) {
        // As the game's binkw32.dll converts its frames.
        _movie->toRgba(_frame.rgba, bink::Movie::Rgb::kBinkw32);
        _frame.w = _movie->width();
        _frame.h = _movie->height();
    }
    return drawn;
}

bool MoviePlayer::done(uint64_t nowMs) const {
    if (!_movie || fps() <= 0)
        return true;
    // The last frame shown for its time.
    const double end = double(_movie->frameCount()) * 1000.0 / fps();
    return _next >= _movie->frameCount() && double(nowMs - std::min(nowMs, _startMs)) >= end;
}

} // namespace reluva::awe
