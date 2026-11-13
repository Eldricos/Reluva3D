// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/audio.h"

#include <cmath>
#include <utility>

namespace reluva::stharbinger {

namespace {

// Shannon entropy (bits) of every `step`th byte starting at `start`.
double planeEntropy(const std::vector<uint8_t> &data, size_t start, size_t step) {
    uint32_t counts[256] = {};
    size_t n = 0;
    for (size_t i = start; i < data.size(); i += step, n++)
        counts[data[i]]++;
    if (!n)
        return 0.0;
    double e = 0.0;
    for (uint32_t c : counts) {
        if (c) {
            double p = double(c) / n;
            e -= p * std::log2(p);
        }
    }
    return e;
}

} // namespace

RawFormat detectRawFormat(const std::vector<uint8_t> &data) {
    double lo = planeEntropy(data, 0, 2);
    double hi = planeEntropy(data, 1, 2);
    // A low byte that never changes is 8-bit audio stored as 16-bit (WHISPER1/2.RAW).
    if (lo < 0.1 || (hi >= 0.1 && lo - hi > 1.0))
        return kRawS16LE;
    if (hi < 0.1 || hi - lo > 1.0)
        return kRawS16BE;
    size_t nearMid = 0, nearEdge = 0;
    for (size_t i = 0; i < data.size() && i < 65536; i++) {
        uint8_t b = data[i];
        if (b >= 0x60 && b < 0xA0)
            nearMid++;
        else if (b < 0x20 || b >= 0xE0)
            nearEdge++;
    }
    return nearMid >= nearEdge ? kRawU8 : kRawS8;
}

std::unique_ptr<audio::Stream> makeRawStream(std::vector<uint8_t> data, int loops) {
    audio::RawStream::Format format;
    switch (detectRawFormat(data)) {
    case kRawU8:
        format = audio::RawStream::Format::U8;
        break;
    case kRawS8:
        format = audio::RawStream::Format::S8;
        break;
    case kRawS16BE:
        for (size_t i = 0; i + 1 < data.size(); i += 2)
            std::swap(data[i], data[i + 1]);
        format = audio::RawStream::Format::S16LE;
        break;
    default:
        format = audio::RawStream::Format::S16LE;
        break;
    }
    return std::make_unique<audio::RawStream>(std::move(data), format, kSampleRate, false, loops);
}

} // namespace reluva::stharbinger
