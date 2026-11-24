// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/pages/movie_page.h"

#include "common/events.h"
#include "common/text.h"
#include "graphics/font.h"
#include "image/image.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

constexpr Color kBackground = {0, 0, 0};

} // namespace

MoviePage::MoviePage(System &sys, Surface &screen, const GameFiles &files, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _files(files), _mixer(mixer) {
    for (const std::string &name : files.list())
        if (text::hasSuffixIgnoreCase(name, ".bik"))
            _names.push_back(name);
}

void MoviePage::activate() {
    if (!_movie && _error.empty())
        open();
    draw();
}

void MoviePage::handleKey(uint32_t key) {
    switch (key) {
    case key::kLeft:
    case key::kRight: {
        const size_t next = stepIndex(_current, key == key::kLeft ? -1 : 1, _names.size());
        if (next == _current)
            return;
        pause();
        _current = next;
        open();
        break;
    }
    case key::kSpace:
        if (!_movie)
            return;
        if (_playing) {
            pause();
        } else {
            bool more = true;
            // From the start again once it has ended.
            if (_movie->frameNumber() + 1 >= int(_movie->frameCount())) {
                _movie->rewind();
                _error.clear();
                if (Error err = _movie->nextFrame(more)) {
                    _error = err.message;
                    break;
                }
                _movie->toRgba(_rgba, bink::Movie::Rgb::kExchanged);
            }
            const uint64_t frameMillis =
                uint64_t(_movie->frameNumber()) * 1000 * _movie->rateDenominator() / _movie->rateNumerator();
            _startMillis = _sys.getMillis() - frameMillis;
            _playing = true;
            // The sound from where the picture is.
            if (_mixer && _sound && _sound->channels > 0) {
                auto rest = std::make_shared<audio::Sound>(*_sound);
                const size_t skip =
                    std::min(_sound->samples.size(),
                             size_t(frameMillis * uint64_t(_sound->rate) / 1000) * size_t(_sound->channels));
                rest->samples.erase(rest->samples.begin(), rest->samples.begin() + std::ptrdiff_t(skip));
                _soundHandle = _mixer->play(rest, audio::SoundType::kMusic);
            }
        }
        break;
    case key::kHome:
        if (!_movie)
            return;
        pause();
        open();
        break;
    default:
        return;
    }
    draw();
}

void MoviePage::update(uint64_t millis) {
    if (!_playing || !_movie)
        return;
    const int64_t due =
        int64_t((millis - _startMillis) * _movie->rateNumerator() / (uint64_t(_movie->rateDenominator()) * 1000));
    bool changed = false;
    bool more = true;
    while (_movie->frameNumber() < due && more) {
        if (Error err = _movie->nextFrame(more)) {
            _error = err.message;
            more = false;
        }
        changed = changed || more;
    }
    if (changed)
        _movie->toRgba(_rgba, bink::Movie::Rgb::kExchanged);
    if (!more)
        pause();
    if (changed || !more)
        draw();
}

void MoviePage::open() {
    _movie.reset();
    _sound.reset();
    _rgba.clear();
    _error.clear();
    if (_names.empty())
        return;
    std::vector<uint8_t> data;
    auto movie = std::make_unique<bink::Movie>();
    if (Error err = _files.read(_names[_current], data); err || (err = movie->open(std::move(data)))) {
        _error = err.message;
        return;
    }
    if (movie->audioRate() > 0) {
        auto sound = std::make_shared<audio::Sound>();
        sound->rate = movie->audioRate();
        sound->channels = movie->audioChannels();
        if (Error err = movie->decodeAllAudio(sound->samples))
            _error = "sound: " + err.message;
        else
            _sound = std::move(sound);
    }
    bool more = true;
    if (Error err = movie->nextFrame(more)) {
        _error = err.message;
        return;
    }
    movie->toRgba(_rgba, bink::Movie::Rgb::kExchanged);
    _movie = std::move(movie);
}

void MoviePage::pause() {
    _playing = false;
    if (_mixer && _soundHandle != audio::kNoSound)
        _mixer->stop(_soundHandle);
    _soundHandle = audio::kNoSound;
}

void MoviePage::draw() {
    fillRect(_screen, 0, 0, _screen.w, _screen.h, kBackground);
    if (_names.empty())
        return;
    std::string about = _error;
    if (_movie) {
        image::Image frame;
        frame.w = _movie->width();
        frame.h = _movie->height();
        frame.rgba = _rgba;
        if (frame.rgba.size() == size_t(frame.w) * frame.h * 4)
            drawFitted(_screen, frame, false, _screen.h - font::kLineHeight - 4);
        const double fps = double(_movie->rateNumerator()) / _movie->rateDenominator();
        about = text::format("%dx%d, frame %d/%u, %.2f fps, rev. %c", _movie->width(), _movie->height(),
                             _movie->frameNumber() + 1, _movie->frameCount(), fps, _movie->revision());
        if (_sound)
            about += text::format(", %d Hz %s", _sound->rate, _sound->channels == 2 ? "stereo" : "mono");
        if (!_error.empty())
            about += "  " + _error;
    }
    const std::string line = std::to_string(_current + 1) + "/" + std::to_string(_names.size()) + "  " +
                             _names[_current] + "  " + about + (_playing ? "  playing" : "");
    drawStatus(_screen, line);
    // The frame counter would make a title per frame: the title says which movie.
    _sys.setWindowTitle("Reluva viewer - Movies - " + std::to_string(_current + 1) + "/" +
                        std::to_string(_names.size()) + "  " + _names[_current] + (_playing ? "  playing" : ""));
}

} // namespace reluva::storm3d
