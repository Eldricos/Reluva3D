// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/ui/movie.h"

#include "common/stream.h"

#include <algorithm>
#include <cmath>

namespace reluva::clue {

Error Movie::open(std::vector<uint8_t> data) {
    _data = std::move(data);
    _shown = 0;
    _picture = {};
    if (Error err = restart())
        return err;
    if (!_decoder.canDecode())
        return Error::fail("AVI video in " + videoCodec() + ", not Cinepak");
    return seek(0);
}

Error Movie::restart() {
    if (!_decoder.load(std::make_unique<MemoryReadStream>(_data)))
        return Error::fail("not an AVI file with a video stream");
    return Error::success();
}

std::string Movie::videoCodec() const {
    const uint32_t c = _decoder.videoCodec();
    std::string s;
    for (int i = 0; i < 4; i++)
        s += char(c >> (8 * i));
    return s;
}

double Movie::framesPerSecond() const {
    return _decoder.frameMicros() ? 1e6 / _decoder.frameMicros() : 0;
}

double Movie::durationSeconds() const {
    const double fps = framesPerSecond();
    return fps > 0 ? double(frameCount()) / fps : 0;
}

Error Movie::audio(audio::Sound &out) const {
    out = {};
    if (!hasAudio())
        return Error::fail("AVI file without sound");
    const int bits = _decoder.audioBits();
    const int channels = _decoder.audioChannels();
    if ((bits != 8 && bits != 16) || channels < 1 || channels > 2)
        return Error::fail("AVI sound isn't 8- or 16-bit PCM");
    out.rate = _decoder.audioRate();
    out.channels = channels;
    out.format = 1;
    const std::vector<uint8_t> &p = _decoder.audio();
    if (bits == 16)
        for (size_t i = 0; i + 1 < p.size(); i += 2)
            out.samples.push_back(int16_t(p[i] | p[i + 1] << 8));
    else
        for (uint8_t b : p)
            out.samples.push_back(int16_t((b - 128) << 8));
    // Whole frames of samples only.
    out.samples.resize(out.samples.size() / size_t(out.channels) * size_t(out.channels));
    return Error::success();
}

Error Movie::seek(double seconds) {
    if (frameCount() == 0)
        return Error::success();
    const int target = int(std::min(frameCount() - 1, size_t(std::max(0.0, std::floor(seconds * framesPerSecond())))));
    // Frames build on the ones before: going back starts again.
    if (target < _decoder.currentFrame())
        if (Error err = restart())
            return err;
    if (target == _decoder.currentFrame() && !_picture.rgba.empty())
        return Error::success();
    while (_decoder.currentFrame() < target)
        if (!_decoder.decodeNextFrame())
            return Error::fail("frame " + std::to_string(_decoder.currentFrame() + 1) + " can't be read");
    _shown = size_t(target);
    const Surface &s = _decoder.surface();
    _picture.w = s.w;
    _picture.h = s.h;
    _picture.rgba.resize(size_t(s.w) * size_t(s.h) * 4);
    for (int y = 0; y < s.h; y++)
        std::copy_n(s.row(y), size_t(s.w) * 4, _picture.rgba.data() + size_t(y) * size_t(s.w) * 4);
    return Error::success();
}

} // namespace reluva::clue
