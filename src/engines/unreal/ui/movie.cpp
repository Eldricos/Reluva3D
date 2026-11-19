// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Movies page: the game's AVI movies (The Fallen has one, its intro:
// Cinepak video and PCM sound). The sound plays through the mixer and the
// pictures follow it, so they stay together however the frames are paced.

#include "engines/unreal/ui/movie.h"

#include "graphics/font.h"

#include "audio/sound.h"
#include "common/stream.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <cctype>
#include <system_error>

namespace reluva::unreal {

namespace {

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The game folder's "movies" (in any case), and the AVI files in it.
std::vector<std::filesystem::path> findMovies(const std::filesystem::path &game) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (game.empty() || !std::filesystem::is_directory(game, ec))
        return out;
    for (const auto &dir : std::filesystem::directory_iterator(game, ec)) {
        if (!dir.is_directory(ec) || lower(dir.path().filename().string()) != "movies")
            continue;
        for (const auto &f : std::filesystem::directory_iterator(dir.path(), ec))
            if (f.is_regular_file(ec) && lower(f.path().extension().string()) == ".avi")
                out.push_back(f.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

MoviePlayer::MoviePlayer(ViewContext &ctx) : Page(ctx), files_(findMovies(ctx.gameFolder)) {
}

MoviePlayer::~MoviePlayer() {
    stop();
}

void MoviePlayer::activate() {
    draw();
}

void MoviePlayer::deactivate() {
    stop();
}

bool MoviePlayer::play(const std::string &name) {
    for (size_t i = 0; i < files_.size(); ++i)
        if (lower(files_[i].filename().string()) == lower(name)) {
            index_ = i;
            play(i);
            return playing();
        }
    return false;
}

void MoviePlayer::play(size_t index) {
    stop();
    if (index >= files_.size())
        return;
    std::vector<uint8_t> data;
    {
        std::ifstream in(files_[index], std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), {});
    }
    auto movie = std::make_unique<avi::Decoder>();
    if (data.empty() || !movie->load(std::make_unique<MemoryReadStream>(std::move(data)))) {
        status_ = "not an AVI file with a video stream";
        draw();
        return;
    }
    if (!movie->canDecode()) {
        status_ = "its video isn't Cinepak";
        draw();
        return;
    }
    movie_ = std::move(movie);
    shown_ = -1;
    startMs_ = 0;
    // The sound as recorded: 16-bit little-endian, or 8-bit unsigned.
    if (ctx_.mixer && movie_->hasAudio() && movie_->audioRate() > 0) {
        auto sound = std::make_shared<audio::Sound>();
        sound->rate = movie_->audioRate();
        sound->channels = std::max(1, movie_->audioChannels());
        sound->sourceBits = movie_->audioBits();
        const std::vector<uint8_t> &pcm = movie_->audio();
        if (movie_->audioBits() == 8)
            for (uint8_t b : pcm)
                sound->samples.push_back(int16_t((int(b) - 128) * 256));
        else
            for (size_t i = 0; i + 1 < pcm.size(); i += 2)
                sound->samples.push_back(int16_t(pcm[i] | pcm[i + 1] << 8));
        audioRate_ = sound->rate;
        sound_ = ctx_.mixer->play(sound, audio::SoundType::kMusic);
    }
    clear(0, 0, 0);
    setTitle(files_[index].filename().string());
}

void MoviePlayer::stop() {
    if (ctx_.mixer && sound_ != audio::kNoSound)
        ctx_.mixer->stop(sound_);
    sound_ = audio::kNoSound;
    if (movie_) {
        movie_.reset();
        draw();
    }
}

void MoviePlayer::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    const bool go = ev.key == key::kSpace || ev.key == key::kReturn || ev.key == key::kKpEnter;
    if (playing()) {
        if (go || ev.key == key::kEscape)
            stop();
        return;
    }
    if (files_.empty())
        return;
    if (ev.key == key::kLeft || ev.key == key::kRight) {
        index_ = (index_ + (ev.key == key::kLeft ? files_.size() - 1 : 1)) % files_.size();
        status_.clear();
        draw();
    } else if (go) {
        play(index_);
    }
}

// The frame due now: by the sound's place while it plays, else by the clock
// since the movie started.
void MoviePlayer::update(uint64_t nowMs) {
    if (!movie_)
        return;
    if (startMs_ == 0)
        startMs_ = nowMs ? nowMs : 1;
    double seconds = double(nowMs - startMs_) / 1000.0;
    if (ctx_.mixer && sound_ != audio::kNoSound && ctx_.mixer->isPlaying(sound_) && audioRate_ > 0)
        seconds = ctx_.mixer->position(sound_) / double(audioRate_);
    const int due = int(seconds * 1e6 / double(std::max<uint32_t>(movie_->frameMicros(), 1)));
    if (due >= movie_->frameCount()) {
        if (!ctx_.mixer || sound_ == audio::kNoSound || !ctx_.mixer->isPlaying(sound_))
            stop();
        return;
    }
    if (due <= shown_)
        return;
    // Frames build on those before, so every one up to it is decoded.
    while (shown_ < due) {
        if (!movie_->decodeNextFrame()) {
            status_ = "frame " + std::to_string(shown_ + 1) + " doesn't decode";
            stop();
            return;
        }
        shown_ = movie_->currentFrame();
    }
    showPicture();
}

// The picture, scaled to fit the screen and centred on black.
void MoviePlayer::showPicture() {
    const Surface &pic = movie_->surface();
    Surface &s = ctx_.screen;
    if (pic.w <= 0 || pic.h <= 0)
        return;
    const double scale = std::min(double(s.w) / pic.w, double(s.h) / pic.h);
    const int w = std::max(1, int(pic.w * scale));
    const int h = std::max(1, int(pic.h * scale));
    const int x0 = (s.w - w) / 2;
    const int y0 = (s.h - h) / 2;
    if (w != s.w || h != s.h)
        clear(0, 0, 0);
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(pic.h - 1, int(y / scale));
        const uint8_t *row = pic.pixels.data() + size_t(sy) * size_t(pic.pitch);
        uint8_t *out = s.row(y0 + y) + x0 * 4;
        for (int x = 0; x < w; ++x, out += 4) {
            const uint8_t *in = row + std::min(pic.w - 1, int(x / scale)) * 4;
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            out[3] = 255;
        }
    }
}

void MoviePlayer::draw() {
    clear(16, 18, 24);
    if (files_.empty()) {
        text(4, 4, "No movies found");
        return;
    }
    const int lh = font::kLineHeight;
    const std::string name = files_[index_].filename().string();
    setTitle(name);
    text(4, 4, name + "  (" + std::to_string(index_ + 1) + " of " + std::to_string(files_.size()) + ")");
    text(4, 4 + lh, "Space or Enter plays it; Space, Enter or Esc stops it.", {150, 200, 255});
    if (!status_.empty())
        text(4, 4 + 3 * lh, status_);
}

} // namespace reluva::unreal
