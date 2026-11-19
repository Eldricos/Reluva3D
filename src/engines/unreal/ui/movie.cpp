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

MoviePlayer::MoviePlayer(ViewContext &ctx) : Page(ctx), _files(findMovies(ctx.gameFolder)) {
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
    for (size_t i = 0; i < _files.size(); ++i)
        if (lower(_files[i].filename().string()) == lower(name)) {
            _index = i;
            play(i);
            return playing();
        }
    return false;
}

void MoviePlayer::play(size_t index) {
    stop();
    if (index >= _files.size())
        return;
    std::vector<uint8_t> data;
    {
        std::ifstream in(_files[index], std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(in), {});
    }
    auto movie = std::make_unique<avi::Decoder>();
    if (data.empty() || !movie->load(std::make_unique<MemoryReadStream>(std::move(data)))) {
        _status = "not an AVI file with a video stream";
        draw();
        return;
    }
    if (!movie->canDecode()) {
        _status = "its video isn't Cinepak";
        draw();
        return;
    }
    _movie = std::move(movie);
    _shown = -1;
    _startMs = 0;
    // The sound as recorded: 16-bit little-endian, or 8-bit unsigned.
    if (_ctx.mixer && _movie->hasAudio() && _movie->audioRate() > 0) {
        auto sound = std::make_shared<audio::Sound>();
        sound->rate = _movie->audioRate();
        sound->channels = std::max(1, _movie->audioChannels());
        sound->sourceBits = _movie->audioBits();
        const std::vector<uint8_t> &pcm = _movie->audio();
        if (_movie->audioBits() == 8)
            for (uint8_t b : pcm)
                sound->samples.push_back(int16_t((int(b) - 128) * 256));
        else
            for (size_t i = 0; i + 1 < pcm.size(); i += 2)
                sound->samples.push_back(int16_t(pcm[i] | pcm[i + 1] << 8));
        _audioRate = sound->rate;
        _sound = _ctx.mixer->play(sound, audio::SoundType::kMusic);
    }
    clear(0, 0, 0);
    setTitle(_files[index].filename().string());
}

void MoviePlayer::stop() {
    if (_ctx.mixer && _sound != audio::kNoSound)
        _ctx.mixer->stop(_sound);
    _sound = audio::kNoSound;
    if (_movie) {
        _movie.reset();
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
    if (_files.empty())
        return;
    if (ev.key == key::kLeft || ev.key == key::kRight) {
        _index = (_index + (ev.key == key::kLeft ? _files.size() - 1 : 1)) % _files.size();
        _status.clear();
        draw();
    } else if (go) {
        play(_index);
    }
}

// The frame due now: by the sound's place while it plays, else by the clock
// since the movie started.
void MoviePlayer::update(uint64_t nowMs) {
    if (!_movie)
        return;
    if (_startMs == 0)
        _startMs = nowMs ? nowMs : 1;
    double seconds = double(nowMs - _startMs) / 1000.0;
    if (_ctx.mixer && _sound != audio::kNoSound && _ctx.mixer->isPlaying(_sound) && _audioRate > 0)
        seconds = _ctx.mixer->position(_sound) / double(_audioRate);
    const int due = int(seconds * 1e6 / double(std::max<uint32_t>(_movie->frameMicros(), 1)));
    if (due >= _movie->frameCount()) {
        if (!_ctx.mixer || _sound == audio::kNoSound || !_ctx.mixer->isPlaying(_sound))
            stop();
        return;
    }
    if (due <= _shown)
        return;
    // Frames build on those before, so every one up to it is decoded.
    while (_shown < due) {
        if (!_movie->decodeNextFrame()) {
            _status = "frame " + std::to_string(_shown + 1) + " doesn't decode";
            stop();
            return;
        }
        _shown = _movie->currentFrame();
    }
    showPicture();
}

// The picture, scaled to fit the screen and centred on black.
void MoviePlayer::showPicture() {
    const Surface &pic = _movie->surface();
    Surface &s = _ctx.screen;
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
    if (_files.empty()) {
        text(4, 4, "No movies found");
        return;
    }
    const int lh = font::kLineHeight;
    const std::string name = _files[_index].filename().string();
    setTitle(name);
    text(4, 4, name + "  (" + std::to_string(_index + 1) + " of " + std::to_string(_files.size()) + ")");
    text(4, 4 + lh, "Space or Enter plays it; Space, Enter or Esc stops it.", {150, 200, 255});
    if (!_status.empty())
        text(4, 4 + 3 * lh, _status);
}

} // namespace reluva::unreal
