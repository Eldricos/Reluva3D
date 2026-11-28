// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/panel_movie.h"

namespace reluva::storm3d {

Error PanelMovie::open(const GameFiles &files, const std::string &name) {
    _open = false;
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    if (Error err = _movie.open(std::move(data)))
        return err;
    if (_movie.frameCount() == 0 || _movie.rateNumerator() == 0 || _movie.rateDenominator() == 0)
        return Error::fail(name + ": no frames to play");
    _open = true;
    _shown = -1;
    return Error::success();
}

std::shared_ptr<audio::Sound> PanelMovie::sound() {
    if (!_open || _movie.audioRate() <= 0)
        return nullptr;
    auto sound = std::make_shared<audio::Sound>();
    sound->rate = _movie.audioRate();
    sound->channels = _movie.audioChannels();
    const bool failed = bool(_movie.decodeAllAudio(sound->samples));
    _movie.rewind();
    _shown = -1;
    return failed || sound->samples.empty() ? nullptr : sound;
}

void PanelMovie::restart(uint64_t millis) {
    _start = millis;
    _shown = -1;
    _movie.rewind();
}

bool PanelMovie::update(uint64_t millis) {
    if (!_open)
        return false;
    const uint64_t since = millis > _start ? millis - _start : 0;
    const int64_t want = int64_t(since * _movie.rateNumerator() / (uint64_t(_movie.rateDenominator()) * 1000));
    const int64_t count = int64_t(_movie.frameCount());
    // Far behind (the menu was away a while): from the loop it's in now.
    if (want - _shown > count) {
        _shown = want - want % count - 1;
        _movie.rewind();
    }
    bool decoded = false;
    while (_shown < want) {
        if (_shown >= 0 && (_shown + 1) % count == 0)
            _movie.rewind();
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

} // namespace reluva::storm3d
