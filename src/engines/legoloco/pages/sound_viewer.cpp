// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "audio/decode.h"
#include "audio/wav.h"
#include "common/archive.h"
#include "engines/legoloco/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace reluva::legoloco {

namespace {

constexpr int kListTop = 40;
constexpr int kRowHeight = 12;

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f s", s);
    return buf;
}

} // namespace

SoundPage::SoundPage(System &sys, Surface &screen, const GameFiles &files, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _files(files), _mixer(mixer) {
    const auto &entries = files.resources().entries();
    for (size_t i = 0; i < entries.size(); ++i)
        if (normalizeArchiveName(entries[i].name).ends_with(".wav"))
            _entries.push_back(i);
    if (!_entries.empty())
        select(0);
}

void SoundPage::handleKey(uint32_t key) {
    const size_t n = _entries.size();
    size_t next = _current;
    if (key == key::kDown)
        next = (_current + 1) % n;
    else if (key == key::kUp)
        next = (_current + n - 1) % n;
    else if (key == key::kPageDown)
        next = std::min(n - 1, _current + 20);
    else if (key == key::kPageUp)
        next = _current >= 20 ? _current - 20 : 0;
    else if (key == key::kHome)
        next = 0;
    else if (key == key::kEnd)
        next = n - 1;
    else if (key == key::kSpace || key == key::kReturn) {
        if (_playing != audio::kNoSound)
            stop();
        else
            play();
        draw();
        return;
    } else if (key == 'a') {
        _autoPlay = !_autoPlay;
        draw();
        return;
    } else {
        return;
    }
    select(next);
    draw();
}

void SoundPage::select(size_t index) {
    stop();
    _current = index;
    _sound.reset();
    _error.clear();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    Error err = _files.resources().readEntry(_entries[_current], data);
    if (!err)
        err = audio::decodeWav(data, *sound);
    if (err)
        _error = err.message;
    else
        _sound = std::move(sound);
    if (_autoPlay)
        play();
}

void SoundPage::play() {
    stop();
    if (_mixer && _sound && !_sound->samples.empty())
        _playing = _mixer->play(_sound, audio::SoundType::kEffects);
}

void SoundPage::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundPage::update(uint64_t nowMs) {
    // While a sound plays, its progress a few times a second; once it ends,
    // once more to show it stopped.
    if (_playing == audio::kNoSound || nowMs - _lastDrawMs < 100)
        return;
    _lastDrawMs = nowMs;
    if (_mixer && !_mixer->isPlaying(_playing))
        _playing = audio::kNoSound;
    draw();
}

void SoundPage::draw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {20, 24, 36});
    if (_entries.empty())
        return;
    const Color text{220, 220, 220};
    const Color hilite{255, 220, 120};
    const auto &entries = _files.resources().entries();

    std::string info;
    if (!_error.empty()) {
        info = _error;
    } else if (_sound) {
        const double length = _sound->rate ? double(_sound->frames()) / _sound->rate : 0;
        info = std::string(audio::formatName(_sound->format)) + ", " + std::to_string(_sound->rate) + " Hz, " +
               (_sound->channels == 2 ? "stereo" : "mono") + ", " + seconds(length);
        if (_mixer && _playing != audio::kNoSound && _mixer->isPlaying(_playing))
            info += "  playing " + seconds(double(_mixer->position(_playing)) / _sound->rate);
    }
    font::drawText(_screen, 8, 8, entries[_entries[_current]].name, hilite);
    font::drawText(_screen, 8, 20, info, text);

    const int rows = (_screen.h - kListTop - 30) / kRowHeight;
    size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    first = std::min(first, _entries.size() > size_t(rows) ? _entries.size() - size_t(rows) : 0);
    for (int r = 0; r < rows && first + size_t(r) < _entries.size(); ++r) {
        const size_t i = first + size_t(r);
        const int y = kListTop + r * kRowHeight;
        if (i == _current)
            fillRect(_screen, 4, y - 2, _screen.w - 8, kRowHeight, {50, 60, 90});
        font::drawText(_screen, 12, y, entries[_entries[i]].name, i == _current ? hilite : text);
    }
    drawStatus(_screen, "[" + std::to_string(_current + 1) + "/" + std::to_string(_entries.size()) +
                            "]  Up/Down select  " + "Space play/stop  A autoplay " + (_autoPlay ? "on" : "off") +
                            (_mixer ? "" : "  (no audio device)") + "  Tab page");
}

} // namespace reluva::legoloco
