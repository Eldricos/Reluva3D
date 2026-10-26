// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/movie_player.h"

#include "common/video/bink_audio.h"

#include <algorithm>

namespace reluva::awe {

Error MoviePlayer::open(std::vector<uint8_t> data, uint64_t nowMs, bool sound) {
    stop();
    file_.reset();
    video_.reset();
    soundtrack_.reset();
    frame_ = image::Image();
    next_ = 0;
    paused_ = false;
    error_.clear();
    auto file = std::make_unique<video::BinkFile>();
    auto video = std::make_unique<video::BinkVideoDecoder>();
    Error e = file->open(std::move(data));
    if (!e)
        e = video->init(file->width(), file->height(), file->revision(), file->flags());
    if (e)
        return e;
    // The soundtrack, decoded whole and played alongside.
    if (sound && !file->audioTracks().empty()) {
        const video::BinkAudioTrack& t = file->audioTracks()[0];
        video::BinkAudioDecoder dec;
        auto track = std::make_shared<audio::Sound>();
        if (Error ae = dec.init(t.sampleRate, t.channels, t.dct)) {
            error_ = "sound: " + ae.message;
        } else {
            for (size_t i = 0; i < file->frameCount(); i++) {
                video::BinkFile::Frame f;
                if (!file->frame(i, f) && !f.audio.empty() && f.audio[0].size)
                    dec.decode(f.audio[0].data, f.audio[0].size, track->samples, f.audioBytes[0]);
            }
            track->rate = t.sampleRate;
            track->channels = t.channels;
            track->format = 0;
            soundtrack_ = track;
        }
    }
    file_ = std::move(file);
    video_ = std::move(video);
    startMs_ = nowMs;
    playSound(0);
    return Error::success();
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
    if (!file_ || paused == paused_)
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
    return file_ && file_->fps() > 0 ? double(next_) / file_->fps() : 0;
}

bool MoviePlayer::update(uint64_t nowMs) {
    if (!file_ || paused_ || file_->frameCount() == 0 || file_->fps() <= 0)
        return false;
    const size_t due = size_t(double(nowMs - std::min(nowMs, startMs_)) * file_->fps() / 1000.0);
    bool drawn = false;
    while (next_ <= due && next_ < file_->frameCount()) {
        video::BinkFile::Frame f;
        Error e = file_->frame(next_, f);
        if (!e)
            e = video_->decode(f.video.data, f.video.size);
        if (e)
            error_ = "frame " + std::to_string(next_) + ": " + e.message;
        next_++;
        drawn = true;
    }
    if (drawn) {
        video_->toRgba(frame_.rgba);
        frame_.w = video_->width();
        frame_.h = video_->height();
    }
    return drawn;
}

bool MoviePlayer::done(uint64_t nowMs) const {
    if (!file_)
        return true;
    if (file_->fps() <= 0)
        return true;
    // The last frame shown for its time.
    const double end = double(file_->frameCount()) * 1000.0 / file_->fps();
    return next_ >= file_->frameCount() && double(nowMs - std::min(nowMs, startMs_)) >= end;
}

} // namespace reluva::awe
