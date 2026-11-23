// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/sound_page.h"

#include "audio/wav.h"
#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"

#include <memory>

namespace reluva::storm3d {

namespace {

constexpr Color kBackground = {24, 24, 32};
constexpr Color kText = {200, 200, 200};
constexpr Color kSelected = {255, 220, 120};
constexpr Color kSelectedBand = {60, 50, 30};

// The kind of sound by its folder: sounds/music, sounds/voiceover...
audio::SoundType typeOf(const std::string &name) {
    const std::string n = normalizeArchiveName(name);
    if (n.find("/music/") != std::string::npos || n.rfind("wav/", 0) == 0)
        return audio::SoundType::kMusic;
    if (n.find("/voiceover/") != std::string::npos)
        return audio::SoundType::kSpeech;
    return audio::SoundType::kEffects;
}

} // namespace

SoundPage::SoundPage(System &sys, Surface &screen, const GameFiles &files, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _files(files), _mixer(mixer) {
    for (const std::string &name : files.list())
        if (text::hasSuffixIgnoreCase(name, ".wav"))
            _names.push_back(name);
}

void SoundPage::handleKey(uint32_t key) {
    const size_t before = _current;
    switch (key) {
    case key::kUp:
        _current = stepIndex(_current, -1, _names.size());
        break;
    case key::kDown:
        _current = stepIndex(_current, 1, _names.size());
        break;
    case key::kPageUp:
        _current = stepIndex(_current, -20, _names.size());
        break;
    case key::kPageDown:
        _current = stepIndex(_current, 20, _names.size());
        break;
    case key::kHome:
        _current = 0;
        break;
    case key::kEnd:
        _current = _names.empty() ? 0 : _names.size() - 1;
        break;
    case key::kSpace:
        if (_playing != audio::kNoSound && _mixer && _mixer->isPlaying(_playing))
            stop();
        else
            play();
        draw();
        return;
    case 'a':
        _auto = !_auto;
        break;
    default:
        return;
    }
    if (_current != before) {
        stop();
        _about.clear();
        if (_auto)
            play();
    }
    draw();
}

void SoundPage::update(uint64_t) {
    // The line saying it plays is redrawn once the sound ends.
    if (_playing != audio::kNoSound && _mixer && !_mixer->isPlaying(_playing)) {
        _playing = audio::kNoSound;
        draw();
    }
}

void SoundPage::play() {
    stop();
    if (_names.empty())
        return;
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = _files.read(_names[_current], data); err || (err = audio::decodeWav(data, *sound))) {
        _about = "not read: " + err.message;
        return;
    }
    _about = text::format("%d Hz, %d-bit, %s, %.2f s", sound->rate, sound->sourceBits,
                          sound->channels == 2 ? "stereo" : "mono", double(sound->frames()) / sound->rate);
    if (_mixer)
        _playing = _mixer->play(sound, typeOf(_names[_current]));
}

void SoundPage::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundPage::draw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    if (_names.empty())
        return;
    // The list, the current sound in the middle.
    const int rows = (_screen.h - 2 * font::kLineHeight - 8) / font::kLineHeight;
    const size_t first = _current > size_t(rows / 2) ? _current - size_t(rows / 2) : 0;
    for (int r = 0; r < rows && first + size_t(r) < _names.size(); ++r) {
        const size_t i = first + size_t(r);
        const int y = 4 + r * font::kLineHeight;
        if (i == _current)
            fillRect(_screen, 0, y - 1, _screen.w, font::kLineHeight, kSelectedBand);
        font::drawText(_screen, 8, y, _names[i], i == _current ? kSelected : kText);
    }
    const bool playing = _playing != audio::kNoSound;
    const std::string line = std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " +
                             _names[_current] + (_about.empty() ? "" : "  " + _about) + (playing ? "  playing" : "") +
                             (_auto ? "  auto" : "") + (_mixer ? "" : "  (no sound output)");
    drawStatus(_screen, line);
    _sys.setWindowTitle("Reluva viewer - Sounds - " + line);
}

} // namespace reluva::storm3d
