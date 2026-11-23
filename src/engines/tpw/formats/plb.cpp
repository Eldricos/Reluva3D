// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's one library's, worked out from it:
// docs/engines/tpw/games/simcoaster/formats/plb.md.

#include "engines/tpw/formats/plb.h"

namespace reluva::tpw {

namespace {

constexpr size_t kHeaderSize = 8;
// A record: 70 words, then its name in 40 bytes.
constexpr size_t kRecordSize = 320;
constexpr size_t kWords = 70;
constexpr size_t kNameSize = 40;
constexpr size_t kColoursAt = 54; // in words: the last 16
constexpr size_t kLifeAt = 29;

uint32_t u32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

} // namespace

Error ParticleLibrary::parse(const std::vector<uint8_t> &data) {
    _effects.clear();
    if (data.size() < kHeaderSize)
        return Error::fail("not a particle library (.plb): too short");
    const size_t count = u32(data, 0);
    if (u32(data, 4) != kRecordSize)
        return Error::fail("not a particle library (.plb): records of " + std::to_string(u32(data, 4)) + " bytes");
    if (count > (data.size() - kHeaderSize) / kRecordSize)
        return Error::fail("the particle library's effects run past the end");
    for (size_t i = 0; i < count; ++i) {
        const size_t at = kHeaderSize + kRecordSize * i;
        Effect e;
        for (size_t w = 0; w < kWords; ++w)
            e.words[w] = u32(data, at + 4 * w);
        for (size_t c = 0; c < e.colours.size(); ++c)
            e.colours[c] = e.words[kColoursAt + c];
        e.lifeMs = e.words[kLifeAt];
        for (size_t c = 0; c < kNameSize && data[at + 4 * kWords + c]; ++c)
            e.name += char(data[at + 4 * kWords + c]);
        _effects.push_back(std::move(e));
    }
    return Error::success();
}

} // namespace reluva::tpw
