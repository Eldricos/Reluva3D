// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/pages/viewers.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;

} // namespace

VideoViewer::VideoViewer(System &sys, Surface &screen, const GameData &data, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _data(data), _mixer(mixer) {
    _files = _data.withExtension(".avi");
}

void VideoViewer::activate() {
    load();
    start();
}

void VideoViewer::deactivate() {
    stop();
    _running = false;
}

void VideoViewer::load() {
    stop();
    _running = false;
    _error.clear();
    _sound.reset();
    _drawnFrame = size_t(-1);
    if (_files.empty())
        return;
    const auto &path = _files[size_t(_current)];
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (Error err = _player.open(std::move(bytes))) {
        _error = err.message;
        return;
    }
    if (_player.hasAudio()) {
        auto s = std::make_shared<audio::Sound>();
        if (Error err = _player.audio(*s))
            _error = err.message;
        else
            _sound = s;
    }
    _sys.setWindowTitle(std::string(gameName(_data.language())) + " videos: " + path.filename().string());
}

void VideoViewer::start() {
    stop();
    if (!_error.empty() || _files.empty())
        return;
    _startMs = _sys.getMillis();
    _running = true;
    _drawnFrame = size_t(-1);
    if (_mixer && _sound)
        _playing = _mixer->play(_sound, audio::SoundType::kSpeech);
}

void VideoViewer::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void VideoViewer::handleKey(uint32_t key) {
    if (_files.empty())
        return;
    const int n = int(_files.size());
    switch (key) {
    case key::kRight:
        _current = (_current + 1) % n;
        load();
        start();
        break;
    case key::kLeft:
        _current = (_current + n - 1) % n;
        load();
        start();
        break;
    case key::kSpace:
    case key::kReturn:
        if (_running) {
            stop();
            _running = false;
        } else {
            start();
        }
        break;
    case 'z':
        _zoom = _zoom == 0 ? 1 : _zoom == 1 ? 2 : 0;
        break;
    default:
        return;
    }
    _drawnFrame = size_t(-1);
    draw();
}

void VideoViewer::update(uint64_t nowMs) {
    _now = nowMs;
    if (_running && _error.empty()) {
        const double t = double(nowMs - _startMs) / 1000.0;
        if (Error err = _player.seek(t)) {
            _error = err.message;
            stop();
            _running = false;
        } else if (_player.finished(t) && (_playing == audio::kNoSound || !_mixer || !_mixer->isPlaying(_playing))) {
            _running = false;
            _playing = audio::kNoSound;
        }
    }
    if (_player.frameShown() != _drawnFrame || !_error.empty())
        draw();
}

void VideoViewer::draw() {
    fillRect(_screen, 0, 0, kScreenW, kScreenH, {0, 0, 0});
    if (_files.empty()) {
        drawInfo(_screen, "No videos found");
        return;
    }
    const std::string name = _files[size_t(_current)].filename().string();
    if (!_error.empty()) {
        drawInfo(_screen, name, _error);
        return;
    }
    _drawnFrame = _player.frameShown();
    const image::Image &pic = _player.picture();
    // As large as fits (twice the size for 320x240, which then fills the
    // screen under the info line), or as Z chose; shrunk if it doesn't fit.
    const int areaH = kScreenH - kInfoH;
    double scale = _zoom ? _zoom : std::max(1, std::min(kScreenW / pic.w, kScreenH / pic.h));
    if (pic.w * scale > kScreenW || pic.h * scale > kScreenH)
        scale = std::min(double(kScreenW) / pic.w, double(areaH) / pic.h);
    const int w = int(pic.w * scale);
    const int h = int(pic.h * scale);
    const int x0 = (kScreenW - w) / 2;
    const int y0 = std::max(0, (h > areaH ? kScreenH - h : areaH - h) / 2);
    for (int y = 0; y < h && y0 + y < kScreenH; ++y) {
        const int sy = std::min(pic.h - 1, int(y / scale));
        uint8_t *d = _screen.row(y0 + y) + x0 * 4;
        for (int x = 0; x < w; ++x, d += 4) {
            const uint8_t *p = pic.pixel(std::min(pic.w - 1, int(x / scale)), sy);
            d[0] = p[0];
            d[1] = p[1];
            d[2] = p[2];
            d[3] = 255;
        }
    }
    const Movie &avi = _player;
    char line[160];
    std::snprintf(line, sizeof line, "%dx%d %s, frame %zu/%zu at %.0f a second%s%s", avi.width(), avi.height(),
                  avi.videoCodec().c_str(), _player.frameShown() + 1, avi.frameCount(), avi.framesPerSecond(),
                  _sound ? ", with sound" : "", _running ? "" : "  (stopped)");
    drawInfo(_screen, name + "   " + std::to_string(_current + 1) + "/" + std::to_string(_files.size()), line);
}

} // namespace reluva::clue
