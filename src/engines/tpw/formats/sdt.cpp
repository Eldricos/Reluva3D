// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's 28 sound banks', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/sdt.md.

#include "engines/tpw/formats/sdt.h"

#include "audio/mp3.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr uint32_t kHeader = 0x28;
constexpr uint8_t kMpeg = 0x20;
constexpr uint8_t kStereo = 0x01;

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

} // namespace

Error SoundBank::parse(std::vector<uint8_t> data) {
    _entries.clear();
    _data = std::move(data);
    if (_data.size() < 4)
        return Error::fail("not a sound bank");
    const size_t count = le32(_data, 0);
    if (count > (_data.size() - 4) / 4)
        return Error::fail("the sound bank's table runs past its end");
    _entries.resize(count);
    for (size_t i = 0; i < count; ++i) {
        Entry &e = _entries[i];
        const size_t at = le32(_data, 4 + 4 * i);
        // An entry's header: its size, its data's, its name, its format.
        if (at + kHeader > _data.size() || le32(_data, at) != kHeader)
            continue;
        const uint32_t size = le32(_data, at + 4);
        if (size > _data.size() - at - kHeader)
            continue;
        const char *name = reinterpret_cast<const char *>(&_data[at + 8]);
        e.name.assign(name, std::find(name, name + 16, '\0'));
        e.offset = uint32_t(at + kHeader);
        e.size = size;
        e.rate = int(_data[at + 0x18] | _data[at + 0x19] << 8);
        const uint8_t kind = _data[at + 0x1b];
        e.mpeg = kind & kMpeg;
        e.channels = kind & kStereo ? 2 : 1;
        e.pcmBytes = le32(_data, at + 0x20);
        e.readable = true;
    }
    return Error::success();
}

Error SoundBank::decode(size_t index, audio::Sound &out) const {
    out = {};
    if (index >= _entries.size() || !_entries[index].readable)
        return Error::fail("no sound " + std::to_string(index));
    const Entry &e = _entries[index];
    const auto begin = _data.begin() + long(e.offset);
    if (e.mpeg)
        return audio::decodeMp3(std::vector<uint8_t>(begin, begin + long(e.size)), out);
    out.rate = e.rate;
    out.channels = e.channels;
    out.format = 1;
    out.samples.resize(e.size / 2);
    for (size_t i = 0; i < out.samples.size(); ++i)
        out.samples[i] = int16_t(
            e.offset + 2 * i + 1 < _data.size() ? _data[e.offset + 2 * i] | _data[e.offset + 2 * i + 1] << 8 : 0);
    return Error::success();
}

} // namespace reluva::tpw
