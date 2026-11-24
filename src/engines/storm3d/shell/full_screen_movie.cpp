// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/full_screen_movie.h"

#include <algorithm>
#include <cstring>

namespace reluva::storm3d {

Error FullScreenMovie::open(const GameFiles &files, const std::string &name) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    if (Error err = _movie.open(std::move(data)))
        return err;
    if (_movie.frameCount() == 0 || _movie.rateNumerator() == 0 || _movie.rateDenominator() == 0)
        return Error::fail(name + ": no frames to play");
    _sound.reset();
    if (_movie.audioRate() > 0) {
        auto sound = std::make_shared<audio::Sound>();
        sound->rate = _movie.audioRate();
        sound->channels = _movie.audioChannels();
        if (!_movie.decodeAllAudio(sound->samples) && !sound->samples.empty())
            _sound = sound;
        _movie.rewind();
    }
    _shown = -1;
    return Error::success();
}

bool FullScreenMovie::update(uint64_t millis) {
    const uint64_t since = millis > _start ? millis - _start : 0;
    const int64_t want = int64_t(since * _movie.rateNumerator() / (uint64_t(_movie.rateDenominator()) * 1000));
    if (want >= int64_t(_movie.frameCount()))
        return false;
    bool decoded = false;
    while (_shown < want) {
        bool more = true;
        if (_movie.nextFrame(more) || !more)
            return false;
        ++_shown;
        decoded = true;
    }
    if (decoded)
        _movie.toRgba(_rgba, bink::Movie::Rgb::kExchanged);
    return true;
}

void FullScreenMovie::draw(Surface &screen) const {
    if (screen.format != PixelFormat::kRGBA32)
        return;
    std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
    const int w = _movie.width();
    const int h = _movie.height();
    if (_rgba.empty() || w <= 0 || h <= 0)
        return;
    const int scale = std::max(1, std::min(screen.w / w, screen.h / h));
    const int x0 = (screen.w - w * scale) / 2;
    const int y0 = (screen.h - h * scale) / 2;
    for (int y = 0; y < h * scale; ++y) {
        const int sy = y0 + y;
        if (sy < 0 || sy >= screen.h)
            continue;
        uint8_t *row = screen.row(sy);
        for (int x = 0; x < w * scale; ++x) {
            const int sx = x0 + x;
            if (sx < 0 || sx >= screen.w)
                continue;
            std::memcpy(row + size_t(sx) * 4, &_rgba[(size_t(y / scale) * size_t(w) + size_t(x / scale)) * 4], 3);
            row[size_t(sx) * 4 + 3] = 255;
        }
    }
}

} // namespace reluva::storm3d
