// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/pages/movie_page.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::nfs::nfs2se {

using namespace page;

MoviePage::MoviePage(const GameFiles &files, audio::Mixer *mixer) : _files(files), _mixer(mixer) {
    _movies = files.withExtensions({".dct"});
}

void MoviePage::stopSound() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void MoviePage::activate(System &sys) {
    _nowMs = sys.getMillis();
    if (_movie >= 0 && !_paused)
        seek(_shown, _nowMs);
}

void MoviePage::deactivate() {
    stopSound();
    _paused = true;
}

void MoviePage::load(int movie) {
    stopSound();
    _movie = std::clamp(movie, 0, static_cast<int>(_movies.size()) - 1);
    _loaded = {};
    _audio.reset();
    _error.clear();
    _shown = -1;
    _picture = {};
    std::vector<uint8_t> data;
    if (Error err = _files.read(_movies[size_t(_movie)], data)) {
        _error = err.message;
        return;
    }
    if (Error err = _loaded.open(std::move(data))) {
        _error = err.message;
        return;
    }
    if (_loaded.hasAudio()) {
        auto sound = std::make_shared<audio::Sound>();
        if (!_loaded.decodeAudio(*sound))
            _audio = std::move(sound);
    }
    seek(0, _nowMs);
}

void MoviePage::showFrame(int frame) {
    frame = std::clamp(frame, 0, static_cast<int>(_loaded.frames().size()) - 1);
    if (frame == _shown && !_picture.pixels.empty())
        return;
    // Every frame stands alone.
    if (Error err = _loaded.decodeFrame(_decoder, size_t(frame))) {
        _error = "frame " + std::to_string(frame) + ": " + err.message;
        _shown = -1;
        return;
    }
    _shown = frame;
    _decoder.toSurface(_picture);
}

void MoviePage::seek(int frame, uint64_t nowMs) {
    if (!_error.empty() || _loaded.frames().empty())
        return;
    stopSound();
    showFrame(frame);
    _startFrame = std::max(_shown, 0);
    _startMs = nowMs;
    if (_paused || !_mixer || !_audio)
        return;
    // The sound from this frame on.
    auto rest = std::make_shared<audio::Sound>(*_audio);
    const size_t skip = std::min(rest->frames(), static_cast<size_t>(_startFrame * frameMs() * _audio->rate / 1000));
    rest->samples.erase(rest->samples.begin(),
                        rest->samples.begin() + static_cast<std::ptrdiff_t>(skip * rest->channels));
    _playing = _mixer->play(rest, audio::SoundType::kMusic);
}

void MoviePage::handleKey(uint32_t k, uint16_t) {
    if (_movie < 0)
        load(0);
    const int step = static_cast<int>(std::lround(5000 / frameMs()));
    switch (k) {
    case key::kUp:
        load(_movie - 1);
        break;
    case key::kDown:
        load(_movie + 1);
        break;
    case key::kHome:
        seek(0, _nowMs);
        break;
    case key::kLeft:
        seek(_shown - step, _nowMs);
        break;
    case key::kRight:
        seek(_shown + step, _nowMs);
        break;
    case key::kSpace:
        _paused = !_paused;
        if (_paused)
            stopSound();
        else
            seek(_shown + 1 >= static_cast<int>(_loaded.frames().size()) ? 0 : _shown, _nowMs);
        break;
    case key::kPageUp:
        _paused = true;
        stopSound();
        showFrame(_shown - 1);
        break;
    case key::kPageDown:
        _paused = true;
        stopSound();
        showFrame(_shown + 1);
        break;
    default:
        break;
    }
}

bool MoviePage::tick(uint64_t nowMs) {
    _nowMs = nowMs;
    if (_movie < 0 || _paused || !_error.empty() || _loaded.frames().empty())
        return false;
    // The sound is the clock while it plays; without it, the time.
    double elapsedMs;
    if (_playing != audio::kNoSound) {
        if (!_mixer->isPlaying(_playing)) {
            _playing = audio::kNoSound;
            elapsedMs = 1e12; // the sound ended: so has the movie
        } else {
            elapsedMs = _mixer->position(_playing) * 1000.0 / _audio->rate;
        }
    } else {
        elapsedMs = double(nowMs - _startMs);
    }
    const int last = static_cast<int>(_loaded.frames().size()) - 1;
    const int target = _startFrame + static_cast<int>(elapsedMs / frameMs());
    if (target >= last) {
        showFrame(last);
        _paused = true;
        stopSound();
        return true;
    }
    if (target == _shown)
        return false;
    showFrame(target);
    return true;
}

void MoviePage::draw(Surface &s) {
    if (_movie < 0)
        load(0);
    frame(s,
          "Movies  " + std::to_string(_movie + 1) + "/" + std::to_string(_movies.size()) + "  " +
              _movies[size_t(_movie)].name,
          "Up/Down: movie  Space: pause  Left/Right: 5 s  PgUp/PgDn: frame");
    int y = 8 + font::kLineHeight * 2;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    const int count = static_cast<int>(_loaded.frames().size());
    char info[160];
    std::snprintf(info, sizeof info, "%dx%d  %.0f fps  frame %d/%d  scale %d  %.1f s / %.1f s  %s%s", _loaded.width(),
                  _loaded.height(), _loaded.framesPerSecond(), _shown + 1, count, _decoder.quantiserScale(),
                  std::max(_shown, 0) * frameMs() / 1000, count * frameMs() / 1000, _paused ? "paused" : "playing",
                  _mixer ? (_audio ? "" : "  (no sound)") : "  (no audio output)");
    text(s, kMargin, y, info, kTextColor);
    y += font::kLineHeight + 4;
    // The picture, as large as fits (nearest pixel), then the position.
    const int areaW = s.w - 2 * kMargin;
    const int areaH = s.h - y - font::kLineHeight * 2 - 12;
    if (_picture.w > 0 && _picture.h > 0) {
        const double scale = std::min(double(areaW) / _picture.w, double(areaH) / _picture.h);
        const int w = static_cast<int>(_picture.w * scale);
        const int h = static_cast<int>(_picture.h * scale);
        const int x0 = kMargin + (areaW - w) / 2;
        for (int dy = 0; dy < h; ++dy) {
            const uint8_t *src = _picture.row(std::min(_picture.h - 1, static_cast<int>(dy / scale)));
            uint8_t *dst = s.row(y + dy) + x0 * 4;
            for (int dx = 0; dx < w; ++dx)
                std::copy_n(src + std::min(_picture.w - 1, static_cast<int>(dx / scale)) * 4, 4, dst + dx * 4);
        }
        y += h + 6;
    }
    fill(s, kMargin, y, areaW, 6, kLabelColor);
    if (count > 0)
        fill(s, kMargin, y, areaW * std::max(_shown + 1, 0) / count, 6, kTitleColor);
}

} // namespace reluva::nfs::nfs2se
