// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/pages/viewers.h"

#include "audio/decode.h"
#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace reluva::clue {

namespace {

constexpr int kInfoH = 26;
constexpr int kRowH = 12;

} // namespace

SoundViewer::SoundViewer(System &sys, Surface &screen, const GameData &data, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _data(data), _mixer(mixer) {
    _files = _data.withExtension(".wav");
    for (auto &p : _data.withExtension(".mp3")) // ripped music may be MP3
        _files.push_back(p);
}

void SoundViewer::activate() {
    _info.clear();
    draw();
}

void SoundViewer::deactivate() {
    stop();
}

void SoundViewer::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundViewer::play() {
    stop();
    if (_files.empty())
        return;
    const auto &path = _files[size_t(_current)];
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto s = std::make_shared<audio::Sound>();
    if (Error err = audio::decodeSound(bytes, *s)) {
        _info = err.message;
        return;
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s, %d Hz, %s, %.2f s", audio::formatName(s->format), s->rate,
                  s->channels == 2 ? "stereo" : "mono", s->rate ? double(s->frames()) / s->rate : 0.0);
    _info = buf;
    _sound = s;
    if (!_mixer) {
        _info += "  (no audio output)";
        return;
    }
    // Music is the CD's tracks; the rest is speech (the suspects' lines)
    // and effects, which share the effects volume here.
    const std::string name = fs::toLower(path.filename().string());
    const auto type = name.rfind("track", 0) == 0 ? audio::SoundType::kMusic : audio::SoundType::kEffects;
    _playing = _mixer->play(_sound, type);
}

void SoundViewer::handleKey(uint32_t key) {
    if (_files.empty())
        return;
    const int n = int(_files.size());
    const int before = _current;
    switch (key) {
    case key::kRight:
        _current = std::min(n - 1, _current + 1);
        break;
    case key::kLeft:
        _current = std::max(0, _current - 1);
        break;
    case key::kPageDown:
        _current = std::min(n - 1, _current + 10);
        break;
    case key::kPageUp:
        _current = std::max(0, _current - 10);
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = n - 1;
        break;
    case key::kSpace:
    case key::kReturn:
        if (_mixer && _playing != audio::kNoSound && _mixer->isPlaying(_playing))
            stop();
        else
            play();
        break;
    case 'a':
        _autoPlay = !_autoPlay;
        break;
    default:
        return;
    }
    if (_current != before) {
        stop();
        _info.clear();
        if (_autoPlay)
            play();
    }
    draw();
}

void SoundViewer::update(uint64_t nowMs) {
    // Redraw now and then while playing, for the position.
    if (_playing != audio::kNoSound && nowMs - _lastDraw >= 100)
        draw();
}

void SoundViewer::draw() {
    _lastDraw = _sys.getMillis();
    fillRect(_screen, 0, 0, kScreenW, kScreenH, {24, 24, 32});
    if (_files.empty()) {
        drawInfo(_screen, "No sounds found");
        return;
    }
    const int rows = (kScreenH - kInfoH - 8) / kRowH;
    const int first = std::clamp(_current - rows / 2, 0, std::max(0, int(_files.size()) - rows));
    for (int i = 0; i < rows && first + i < int(_files.size()); ++i) {
        const int f = first + i;
        const std::filesystem::path &p = _files[size_t(f)];
        const std::string where = p.parent_path().filename().string();
        if (f == _current)
            fillRect(_screen, 0, 4 + i * kRowH - 1, kScreenW, kRowH, {60, 60, 100});
        font::drawText(_screen, 8, 4 + i * kRowH, p.filename().string(), {230, 230, 230});
        font::drawText(_screen, 400, 4 + i * kRowH, where, {150, 150, 170});
    }
    std::string state = _info;
    const bool on = _mixer && _playing != audio::kNoSound && _mixer->isPlaying(_playing);
    if (on && _sound && _sound->rate) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "  playing %.1f s", _mixer->position(_playing) / _sound->rate);
        state += buf;
    }
    if (_autoPlay)
        state += "  [autoplay]";
    drawInfo(_screen,
             _files[size_t(_current)].filename().string() + "   " + std::to_string(_current + 1) + "/" +
                 std::to_string(_files.size()),
             state);
    _sys.setWindowTitle(std::string(gameName(_data.language())) +
                        " sounds: " + _files[size_t(_current)].filename().string());
}

} // namespace reluva::clue
