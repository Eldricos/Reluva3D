// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is worked out from the game's 113 sprite files, every one
// read to its end: docs/engines/tpw/games/simcoaster/formats/sprites.md.

#include "engines/tpw/formats/sprites.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

namespace {

constexpr size_t kSheetHeader = 8;
constexpr size_t kPalette = 256 * 4;
constexpr size_t kFrameHeader = 20;
constexpr size_t kSetSize = 350;
constexpr const char *kSetMagic = "ESP_FILE2.00";
constexpr size_t kSetName = 12;
constexpr size_t kSetNameSize = 244;
constexpr size_t kSetAnimations = 270;

uint32_t le32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

uint16_t le16(const std::vector<uint8_t> &d, size_t at) {
    return uint16_t(d[at] | d[at + 1] << 8);
}

} // namespace

Error SpriteSheet::parse(const std::vector<uint8_t> &data) {
    _frames.clear();
    if (data.size() < kSheetHeader + kPalette)
        return Error::fail("not a sprite sheet");
    const uint32_t count = le32(data, 4);
    // Blue, green, red, then how opaque.
    for (size_t i = 0; i < _palette.size(); ++i) {
        const size_t at = kSheetHeader + 4 * i;
        _palette[i] = {data[at + 2], data[at + 1], data[at], data[at + 3]};
    }
    size_t at = kSheetHeader + kPalette;
    for (uint32_t f = 0; f < count; ++f) {
        if (at + kFrameHeader > data.size())
            return Error::fail("frame " + std::to_string(f) + " runs past the end");
        const uint32_t size = le32(data, at);
        Frame frame;
        frame.width = le16(data, at + 4);
        frame.height = le16(data, at + 6);
        frame.scaleX = le16(data, at + 8);
        frame.scaleY = le16(data, at + 10);
        frame.left = int32_t(le32(data, at + 12));
        frame.top = int32_t(le32(data, at + 16));
        at += kFrameHeader;
        if (size > data.size() - at)
            return Error::fail("frame " + std::to_string(f) + " runs past the end");
        const size_t end = at + size;
        frame.pixels.reserve(size_t(frame.width) * size_t(frame.height));
        // Each row: its length in bytes, then runs: a byte under 0x80 says
        // so many numbers follow, one above repeats the next 256 − it times.
        for (int y = 0; y < frame.height; ++y) {
            if (at >= end)
                return Error::fail("frame " + std::to_string(f) + "'s rows run past it");
            const size_t rowEnd = at + 1 + data[at];
            ++at;
            if (rowEnd > end)
                return Error::fail("frame " + std::to_string(f) + "'s rows run past it");
            const size_t rowStart = frame.pixels.size();
            while (at < rowEnd) {
                const uint8_t c = data[at++];
                if (c < 0x80) {
                    if (at + c > rowEnd)
                        return Error::fail("frame " + std::to_string(f) + "'s run runs past its row");
                    frame.pixels.insert(frame.pixels.end(), data.begin() + long(at), data.begin() + long(at + c));
                    at += c;
                } else {
                    if (at >= rowEnd)
                        return Error::fail("frame " + std::to_string(f) + "'s run runs past its row");
                    frame.pixels.insert(frame.pixels.end(), size_t(256 - c), data[at++]);
                }
            }
            if (frame.pixels.size() - rowStart != size_t(frame.width))
                return Error::fail("frame " + std::to_string(f) + "'s row " + std::to_string(y) + " isn't its width");
        }
        at = end;
        _frames.push_back(std::move(frame));
    }
    return Error::success();
}

Error SpriteSet::parse(const std::vector<uint8_t> &data) {
    _sheet.clear();
    _animations = {};
    if (data.size() < kSetSize || std::memcmp(data.data(), kSetMagic, std::strlen(kSetMagic)) != 0)
        return Error::fail("not a sprite's animations (ESP_FILE2.00)");
    const char *name = reinterpret_cast<const char *>(&data[kSetName]);
    _sheet.assign(name, std::find(name, name + kSetNameSize, '\0'));
    for (size_t i = 0; i < kSlots; ++i) {
        const size_t at = kSetAnimations + 4 * i;
        _animations[i] = {le16(data, at), data[at + 2], data[at + 3]};
    }
    return Error::success();
}

} // namespace reluva::tpw
