// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/pages/video_viewer.h"

#include "graphics/font.h"

#include <algorithm>

namespace reluva::idtech3 {

namespace {

std::string extensionOf(const std::string &name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : normalizeArchiveName(name.substr(dot));
}

} // namespace

VideoViewer::VideoViewer(PageContext &ctx) : Page(ctx) {
    for (const std::string &n : ctx.files.list()) {
        const std::string ext = extensionOf(n);
        if (ext == ".roq" || ext == ".bik")
            _names.push_back(n);
    }
}

VideoViewer::~VideoViewer() {
    stop();
}

void VideoViewer::deactivate() {
    stop();
}

void VideoViewer::stop() {
    if (_sound != audio::kNoSound && _ctx.mixer)
        _ctx.mixer->stop(_sound);
    _sound = audio::kNoSound;
    _movie.reset();
}

void VideoViewer::play(int index) {
    stop();
    _index = index;
    _error.clear();
    _rgba.clear();
    _ended = false;
    _startMs = 0;
    if (_names.empty())
        return;
    const std::string &name = _names[size_t(index)];
    setTitle(name + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");
    std::vector<uint8_t> data;
    auto movie = std::make_unique<CinematicMovie>();
    Error err = _ctx.files.read(name, data);
    if (!err)
        err = movie->open(std::move(data), extensionOf(name));
    if (err) {
        _error = err.message;
        draw();
        return;
    }
    std::vector<int16_t> sound;
    int rate = 0;
    int channels = 0;
    if (!movie->decodeAllAudio(sound, rate, channels) && !sound.empty() && _ctx.mixer) {
        auto s = std::make_shared<audio::Sound>();
        s->rate = rate;
        s->channels = channels;
        s->format = 1;
        s->samples = std::move(sound);
        _sound = _ctx.mixer->play(s, audio::SoundType::kEffects, audio::kMaxVolume);
    }
    _movie = std::move(movie);
}

void VideoViewer::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _names.empty())
        return;
    const int n = int(_names.size());
    if (ev.key == key::kLeft)
        play((_index + n - 1) % n);
    else if (ev.key == key::kRight)
        play((_index + 1) % n);
    else if (ev.key == 'r' || ev.key == key::kSpace)
        play(_index);
}

void VideoViewer::update(uint64_t nowMs) {
    if (!_movie || _ended)
        return;
    // The frame due now, at the movie's rate from when it started.
    if (!_startMs)
        _startMs = nowMs;
    const int64_t due =
        int64_t((nowMs - _startMs) * _movie->rateNumerator() / (uint64_t(_movie->rateDenominator()) * 1000));
    bool decoded = false;
    while (_movie->frameNumber() < due) {
        bool more = false;
        if (Error err = _movie->nextFrame(more); err || !more) {
            if (err)
                _error = err.message;
            _ended = true;
            break;
        }
        decoded = true;
    }
    if (decoded)
        _movie->toRgba(_rgba);
    if (decoded || _ended)
        draw();
}

void VideoViewer::draw() {
    Surface &s = _ctx.screen;
    clear(0, 0, 0);
    const int lh = font::kLineHeight;
    if (_movie && !_rgba.empty()) {
        // Stretched over the screen below the caption.
        const int w = _movie->width();
        const int h = _movie->height();
        const int top = 2 * lh + 4;
        const int areaH = s.h - top;
        for (int y = 0; y < areaH; ++y) {
            const int sy = std::min(h - 1, y * h / areaH);
            uint8_t *row = s.row(top + y);
            for (int x = 0; x < s.w; ++x) {
                const int sx = std::min(w - 1, x * w / s.w);
                std::copy_n(&_rgba[(size_t(sy) * size_t(w) + size_t(sx)) * 4], 4, row + x * 4);
            }
        }
    }
    const std::string name = _names.empty() ? std::string() : _names[size_t(_index)];
    std::string info = name;
    if (_movie)
        info += ": " + std::to_string(_movie->width()) + "x" + std::to_string(_movie->height()) + ", frame " +
                std::to_string(_movie->frameNumber() + 1) + (_ended ? " (the end)" : "");
    text(4, 2, info);
    text(4, 2 + lh, _error.empty() ? std::string("Left/Right: another video, R: again") : _error,
         _error.empty() ? Color{150, 200, 255} : Color{255, 120, 120});
}

} // namespace reluva::idtech3
