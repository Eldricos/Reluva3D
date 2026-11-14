// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/cinematic.h"

#include "common/fs.h"

namespace reluva::idtech3 {

Error CinematicMovie::open(std::vector<uint8_t> file, std::string_view extension) {
    _bink.reset();
    _roq.reset();
    if (fs::toLower(extension) == ".roq") {
        _file = file;
        _roq = std::make_unique<roq::Movie>();
        return _roq->open(std::move(file));
    }
    _bink = std::make_unique<bink::Movie>();
    return _bink->open(std::move(file));
}

int CinematicMovie::width() const {
    return _roq ? _roq->width() : _bink ? _bink->width() : 0;
}

int CinematicMovie::height() const {
    return _roq ? _roq->height() : _bink ? _bink->height() : 0;
}

uint32_t CinematicMovie::rateNumerator() const {
    return _roq ? uint32_t(_roq->framesPerSecond()) : _bink ? _bink->rateNumerator() : 1;
}

uint32_t CinematicMovie::rateDenominator() const {
    return _bink ? _bink->rateDenominator() : 1;
}

Error CinematicMovie::nextFrame(bool &more) {
    more = false;
    if (_roq)
        return _roq->nextFrame(more);
    if (_bink)
        return _bink->nextFrame(more);
    return Error::success();
}

int CinematicMovie::frameNumber() const {
    return _roq ? _roq->frameNumber() : _bink ? _bink->frameNumber() : -1;
}

void CinematicMovie::toRgba(std::vector<uint8_t> &out) const {
    if (_roq)
        _roq->toRgba(out);
    else if (_bink)
        _bink->toRgba(out);
}

Error CinematicMovie::decodeAllAudio(std::vector<int16_t> &sound, int &rate, int &channels) {
    rate = channels = 0;
    if (_bink) {
        rate = _bink->audioRate();
        channels = _bink->audioChannels();
        return _bink->decodeAllAudio(sound);
    }
    if (!_roq || !_roq->audioChannels())
        return Error::success();
    rate = roq::Movie::kAudioRate;
    channels = _roq->audioChannels();
    roq::Movie pass;
    if (Error err = pass.open(_file))
        return err;
    for (bool more = true; more;)
        if (Error err = pass.nextFrame(more, &sound))
            return err;
    return Error::success();
}

} // namespace reluva::idtech3
