// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/sound_viewer.h"

#include "audio/decode.h"
#include "engines/stawayteam/pages/draw.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

SoundViewer::SoundViewer(System &sys, Surface &screen, const Archive &data, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _data(data), _mixer(mixer),
      _files(data, [](const std::string &name) { return name.ends_with(".ogg") || name.ends_with(".wav"); }) {
}

void SoundViewer::activate() {
    if (!_sound && _error.empty())
        load();
    draw();
}

void SoundViewer::load() {
    stop();
    _error.clear();
    _sound.reset();
    std::vector<uint8_t> bytes;
    if (Error err = _data.read(_files.current(), bytes)) {
        _error = err.message;
        return;
    }
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = audio::decodeSound(bytes, *sound)) {
        _error = err.message;
        return;
    }
    _sound = std::move(sound);
}

void SoundViewer::play() {
    stop();
    if (!_mixer || !_sound)
        return;
    // Music by its folder; voices and effects as effects.
    const bool music = _files.current().starts_with("Music/");
    _playing = _mixer->play(_sound, music ? audio::SoundType::kMusic : audio::SoundType::kEffects);
    _startMs = _nowMs;
}

void SoundViewer::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundViewer::handleKey(uint32_t key) {
    if (key == ' ') {
        if (_playing != audio::kNoSound && _mixer && _mixer->isPlaying(_playing))
            stop();
        else
            play();
    } else if (key == 'a') {
        _autoPlay = !_autoPlay;
    } else if (_files.handleKey(key)) {
        load();
        if (_autoPlay)
            play();
    } else {
        return;
    }
    draw();
}

void SoundViewer::update(uint64_t nowMs) {
    _nowMs = nowMs;
    // While a sound plays, its time a few times a second; once it ends,
    // once more to show it stopped.
    if (_playing == audio::kNoSound || nowMs - _drawnMs < 200)
        return;
    if (_mixer && !_mixer->isPlaying(_playing))
        _playing = audio::kNoSound;
    draw();
}

void SoundViewer::draw() {
    _drawnMs = _nowMs;
    clearPage(_screen);
    // The sounds about the one chosen, a line each, it in the middle.
    constexpr int kLineHeight = 10;
    const int lines = (_screen.h - kStatusHeight - 40) / kLineHeight;
    const size_t first = _files.index() > size_t(lines / 2) ? _files.index() - size_t(lines / 2) : 0;
    for (int i = 0; i < lines && first + size_t(i) < _files.size(); ++i) {
        const bool chosen = first + size_t(i) == _files.index();
        if (chosen)
            fill(_screen, 0, 2 + i * kLineHeight, _screen.w, kLineHeight, {70, 70, 100});
        font::drawText(_screen, 4, 3 + i * kLineHeight, _files.name(first + size_t(i)),
                       chosen ? Color{255, 255, 255} : Color{170, 170, 180});
    }
    std::string status = _error;
    if (_sound) {
        const double seconds = _sound->rate ? double(_sound->frames()) / _sound->rate : 0;
        char buf[200];
        std::snprintf(buf, sizeof buf, "%s, %d Hz, %s, %.2f s%s%s", audio::formatName(_sound->format), _sound->rate,
                      _sound->channels == 2 ? "stereo" : "mono", seconds,
                      _playing != audio::kNoSound ? "; playing" : (_mixer ? "" : "; no sound output"),
                      _autoPlay ? "; A: plays as chosen" : "");
        status = buf;
        if (_playing != audio::kNoSound && seconds > 0) {
            // How far it has played, as a bar.
            const double done = std::min(1.0, double(_nowMs - _startMs) / 1000.0 / seconds);
            const int barW = _screen.w - 40;
            const int barY = _screen.h - kStatusHeight - 24;
            fill(_screen, 20, barY, barW, 12, {70, 70, 80});
            fill(_screen, 20, barY, int(barW * done), 12, {200, 170, 60});
        }
    }
    drawStatus(_screen, _files.position(), status);
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - sounds (Left/Right, F: next folder, Space: play/stop, A: "
                        "play as chosen, Tab: next page)");
}

} // namespace reluva::stawayteam
