// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/pages/viewers.h"

#include "audio/decode.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace reluva::botf {

namespace {

constexpr int kRates[] = {11025, 22050, 44100};
constexpr int kListTop = 40;
constexpr int kRowHeight = 12;

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f s", s);
    return buf;
}

} // namespace

SoundViewer::SoundViewer(System &sys, Surface &screen, std::vector<std::pair<std::string, const SndArchive *>> archives,
                         audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _archives(std::move(archives)), _mixer(mixer) {
    for (size_t a = 0; a < _archives.size(); ++a) {
        const SndArchive &snd = *_archives[a].second;
        const size_t n = snd.isMusic() ? snd.pieces().size() : snd.entries().size();
        for (size_t i = 0; i < n; ++i)
            _items.push_back({a, i});
    }
}

void SoundViewer::activate() {
    if (_sound == nullptr && _error.empty() && !_items.empty())
        decodeCurrent();
    redraw();
}

void SoundViewer::deactivate() {
    stop();
}

void SoundViewer::handleKey(uint32_t key) {
    if (_items.empty())
        return;
    const size_t n = _items.size();
    if (key == key::kSpace || key == key::kReturn || key == key::kKpEnter) {
        if (_mixer && _playing != audio::kNoSound && _mixer->isPlaying(_playing))
            stop();
        else
            play();
        redraw();
        return;
    }
    if (key == 'a') {
        _autoPlay = !_autoPlay;
        redraw();
        return;
    }
    if (key == 'r') {
        size_t r = 0;
        while (r < std::size(kRates) && kRates[r] != _rate)
            ++r;
        _rate = kRates[(r + 1) % std::size(kRates)];
        stop();
        decodeCurrent();
        if (_autoPlay)
            play();
        redraw();
        return;
    }
    size_t next = _current;
    if (key == key::kDown)
        next = (_current + 1) % n;
    else if (key == key::kUp)
        next = (_current + n - 1) % n;
    else if (key == key::kPageDown)
        next = std::min(_current + 20, n - 1);
    else if (key == key::kPageUp)
        next = _current >= 20 ? _current - 20 : 0;
    else if (key == key::kHome)
        next = 0;
    else if (key == key::kEnd)
        next = n - 1;
    else if (key == key::kRight || key == key::kLeft) {
        // The first item of the next or previous archive, wrapping round.
        const size_t archive = _items[_current].archive;
        const size_t target =
            key == key::kRight ? (archive + 1) % _archives.size() : (archive + _archives.size() - 1) % _archives.size();
        next = 0;
        while (next < n && _items[next].archive != target)
            ++next;
        if (next == n)
            return;
    } else
        return;
    select(next);
}

void SoundViewer::select(size_t index) {
    stop();
    _current = index;
    decodeCurrent();
    if (_autoPlay)
        play();
    redraw();
}

void SoundViewer::decodeCurrent() {
    _sound.reset();
    _error.clear();
    const Item &it = _items[_current];
    const SndArchive &snd = *_archives[it.archive].second;
    auto sound = std::make_shared<audio::Sound>();
    Error err = snd.isMusic() ? snd.decodePiece(it.index, _rate, *sound) : snd.decode(it.index, _rate, *sound);
    if (err)
        _error = err.message;
    else
        _sound = std::move(sound);
}

void SoundViewer::play() {
    stop();
    if (_mixer && _sound && !_sound->samples.empty()) {
        const SndArchive &snd = *_archives[_items[_current].archive].second;
        _playing = _mixer->play(_sound, snd.isMusic() ? audio::SoundType::kMusic : audio::SoundType::kEffects);
    }
}

void SoundViewer::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundViewer::update(uint64_t nowMs) {
    // While a sound plays, redraw its progress a few times a second; once
    // it ends, once more to show it stopped.
    if (_playing == audio::kNoSound || nowMs - _lastDrawMs < 100)
        return;
    _lastDrawMs = nowMs;
    if (_mixer && !_mixer->isPlaying(_playing))
        _playing = audio::kNoSound;
    redraw();
}

std::string SoundViewer::describe(size_t item) const {
    const Item &it = _items[item];
    const auto &[name, snd] = _archives[it.archive];
    char buf[160];
    if (snd->isMusic()) {
        const auto &piece = snd->pieces()[it.index];
        size_t bytes = 0;
        for (size_t e : piece)
            bytes += snd->entries()[e].size;
        std::snprintf(buf, sizeof buf, "%s  piece %2zu  %2zu segment%s  %9zu bytes", name.c_str(), it.index,
                      piece.size(), piece.size() == 1 ? " " : "s", bytes);
    } else {
        const SndArchive::Entry &e = snd->entries()[it.index];
        std::snprintf(buf, sizeof buf, "%s  %4zu  offset %9u  size %8u  flags %u", name.c_str(), it.index, e.offset,
                      e.size, e.flags);
    }
    return buf;
}

void SoundViewer::redraw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, {20, 24, 36});
    if (_items.empty())
        return;
    const Color text{220, 220, 220};
    const Color dim{140, 140, 160};
    const Color hilite{255, 220, 120};

    // The current sound: what it is and how far it has played.
    std::string info;
    if (!_error.empty())
        info = _error;
    else if (_sound) {
        const double length = _sound->rate ? double(_sound->frames()) / _sound->rate : 0;
        info = std::string(audio::formatName(_sound->format)) + ", " + std::to_string(_sound->rate) + " Hz, " +
               (_sound->channels == 2 ? "stereo" : "mono") + ", " + seconds(length);
        if (_mixer && _playing != audio::kNoSound && _mixer->isPlaying(_playing))
            info += "  playing " + seconds(_mixer->position(_playing) / _sound->rate);
    }
    font::drawText(_screen, 8, 8, describe(_current), hilite);
    font::drawText(_screen, 8, 20, info, text);

    // The list around the current item, with archive changes marked.
    const int rows = (_screen.h - kListTop - 30) / kRowHeight;
    size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    first = std::min(first, _items.size() > size_t(rows) ? _items.size() - size_t(rows) : 0);
    for (int r = 0; r < rows && first + size_t(r) < _items.size(); ++r) {
        const size_t i = first + size_t(r);
        const int y = kListTop + r * kRowHeight;
        if (i == _current)
            fillRect(_screen, 4, y - 2, _screen.w - 8, kRowHeight, {50, 60, 90});
        font::drawText(_screen, 12, y, describe(i), i == _current ? hilite : text);
    }

    const std::string help = "Up/Down: select  Left/Right: archive  Space: play/stop  A: autoplay " +
                             std::string(_autoPlay ? "on" : "off") + "  R: ADPCM rate " + std::to_string(_rate) +
                             " Hz" + (_mixer ? "" : "  (no audio device)") + "  Tab: next page";
    font::drawText(_screen, 8, _screen.h - 16, help, dim);
    _sys.setWindowTitle("Reluva - Birth of the Federation - sounds " + std::to_string(_current + 1) + "/" +
                        std::to_string(_items.size()));
}

} // namespace reluva::botf
