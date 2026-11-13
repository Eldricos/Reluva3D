// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/ui/movie.h"

#include "common/stream.h"

#include <algorithm>
#include <cmath>

namespace reluva::clue {

Error Movie::open(std::vector<uint8_t> data) {
    data_ = std::move(data);
    shown_ = 0;
    picture_ = {};
    if (Error err = restart())
        return err;
    if (!decoder_.canDecode())
        return Error::fail("AVI video in " + videoCodec() + ", not Cinepak");
    return seek(0);
}

Error Movie::restart() {
    if (!decoder_.load(std::make_unique<MemoryReadStream>(data_)))
        return Error::fail("not an AVI file with a video stream");
    return Error::success();
}

std::string Movie::videoCodec() const {
    const uint32_t c = decoder_.videoCodec();
    std::string s;
    for (int i = 0; i < 4; i++)
        s += char(c >> (8 * i));
    return s;
}

double Movie::framesPerSecond() const {
    return decoder_.frameMicros() ? 1e6 / decoder_.frameMicros() : 0;
}

double Movie::durationSeconds() const {
    const double fps = framesPerSecond();
    return fps > 0 ? double(frameCount()) / fps : 0;
}

Error Movie::audio(audio::Sound &out) const {
    out = {};
    if (!hasAudio())
        return Error::fail("AVI file without sound");
    const int bits = decoder_.audioBits(), channels = decoder_.audioChannels();
    if ((bits != 8 && bits != 16) || channels < 1 || channels > 2)
        return Error::fail("AVI sound isn't 8- or 16-bit PCM");
    out.rate = decoder_.audioRate();
    out.channels = channels;
    out.format = 1;
    const std::vector<uint8_t> &p = decoder_.audio();
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
    if (target < decoder_.currentFrame())
        if (Error err = restart())
            return err;
    if (target == decoder_.currentFrame() && !picture_.rgba.empty())
        return Error::success();
    while (decoder_.currentFrame() < target)
        if (!decoder_.decodeNextFrame())
            return Error::fail("frame " + std::to_string(decoder_.currentFrame() + 1) + " can't be read");
    shown_ = size_t(target);
    const Surface &s = decoder_.surface();
    picture_.w = s.w;
    picture_.h = s.h;
    picture_.rgba.resize(size_t(s.w) * size_t(s.h) * 4);
    for (int y = 0; y < s.h; y++)
        std::copy_n(s.row(y), size_t(s.w) * 4, picture_.rgba.data() + size_t(y) * size_t(s.w) * 4);
    return Error::success();
}

} // namespace reluva::clue
