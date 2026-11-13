// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/render/movie_player.h"

#include <algorithm>

namespace reluva::awe {

Error MoviePlayer::open(std::vector<uint8_t> data, uint64_t nowMs, bool sound) {
    stop();
    movie_.reset();
    soundtrack_.reset();
    frame_ = image::Image();
    next_ = 0;
    paused_ = false;
    error_.clear();
    auto movie = std::make_unique<bink::Movie>();
    if (Error e = movie->open(std::move(data)))
        return e;
    // The soundtrack, decoded whole and played alongside.
    if (sound && movie->audioRate() > 0) {
        auto track = std::make_shared<audio::Sound>();
        if (Error ae = movie->decodeAllAudio(track->samples)) {
            error_ = "sound: " + ae.message;
        } else {
            track->rate = movie->audioRate();
            track->channels = movie->audioChannels();
            track->format = 0;
            soundtrack_ = track;
        }
    }
    movie_ = std::move(movie);
    startMs_ = nowMs;
    playSound(0);
    return Error::success();
}

double MoviePlayer::fps() const {
    return movie_ && movie_->rateDenominator() ? double(movie_->rateNumerator()) / movie_->rateDenominator() : 0;
}

void MoviePlayer::playSound(double fromSeconds) {
    if (!soundtrack_ || !mixer_)
        return;
    std::shared_ptr<audio::Sound> s = soundtrack_;
    if (fromSeconds > 0) {
        // The rest of it, from that time.
        s = std::make_shared<audio::Sound>(*soundtrack_);
        const size_t from = std::min(s->samples.size(), size_t(fromSeconds * s->rate) * size_t(s->channels));
        s->samples.erase(s->samples.begin(), s->samples.begin() + ptrdiff_t(from));
    }
    sound_ = mixer_->play(s, audio::SoundType::Speech);
}

void MoviePlayer::stop() {
    if (mixer_ && sound_ != audio::kNoSound)
        mixer_->stop(sound_);
    sound_ = audio::kNoSound;
}

void MoviePlayer::pause(bool paused, uint64_t nowMs) {
    if (!movie_ || paused == paused_)
        return;
    paused_ = paused;
    if (paused) {
        stop();
    } else {
        // Carry on from the frame shown, the sound from the same time.
        startMs_ = nowMs - uint64_t(seconds() * 1000.0);
        playSound(seconds());
    }
}

double MoviePlayer::seconds() const {
    return fps() > 0 ? double(next_) / fps() : 0;
}

bool MoviePlayer::update(uint64_t nowMs) {
    if (!movie_ || paused_ || movie_->frameCount() == 0 || fps() <= 0)
        return false;
    const size_t due = size_t(double(nowMs - std::min(nowMs, startMs_)) * fps() / 1000.0);
    bool drawn = false;
    while (next_ <= due && next_ < movie_->frameCount()) {
        bool more = false;
        if (Error e = movie_->nextFrame(more))
            error_ = "frame " + std::to_string(next_) + ": " + e.message;
        next_++;
        drawn = true;
    }
    if (drawn) {
        // As the game's binkw32.dll converts its frames.
        movie_->toRgba(frame_.rgba, bink::Movie::Rgb::Binkw32);
        frame_.w = movie_->width();
        frame_.h = movie_->height();
    }
    return drawn;
}

bool MoviePlayer::done(uint64_t nowMs) const {
    if (!movie_ || fps() <= 0)
        return true;
    // The last frame shown for its time.
    const double end = double(movie_->frameCount()) * 1000.0 / fps();
    return next_ >= movie_->frameCount() && double(nowMs - std::min(nowMs, startMs_)) >= end;
}

} // namespace reluva::awe
