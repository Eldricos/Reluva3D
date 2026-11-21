// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/formats/animation.h"

#include "common/bytes.h"

#include <cstddef>
#include <utility>

namespace reluva::botf {

namespace {

constexpr size_t kHeaderSize = 16; // then a TGA's own 18-byte header
constexpr size_t kTgaHeaderSize = 18;

} // namespace

Error readAnimation(const std::vector<uint8_t> &data, Animation &out) {
    out = Animation{};
    if (data.size() < kHeaderSize + kTgaHeaderSize)
        return Error::fail("too small for an animation");
    // The frame's height and width (in that order: the stellar objects'
    // are 300 wide), the frames' count (twice), the sheet's width and height.
    const int frameH = readLE16(&data[0]);
    const int frameW = readLE16(&data[2]);
    const int count = readLE16(&data[4]);
    const int sheetW = readLE16(&data[8]);
    const int sheetH = readLE16(&data[10]);
    const uint8_t *tga = &data[kHeaderSize];
    const int tgaW = readLE16(tga + 12);
    const int tgaH = readLE16(tga + 14);
    const uint8_t descriptor = tga[17];
    const size_t pixels = kHeaderSize + kTgaHeaderSize + tga[0];
    if (tga[2] != 2 || tga[16] != 16 || tgaW != sheetW || tgaH != sheetH || frameW <= 0 || frameH <= 0 || count <= 0 ||
        sheetW / frameW * (sheetH / frameH) < count || data.size() < pixels + size_t(sheetW) * size_t(sheetH) * 2)
        return Error::fail("the animation's header doesn't fit its picture");
    // With an alpha bit declared, a pixel's top bit says it's drawn; without,
    // black is what isn't. ns.ani declares one but sets it nowhere: black
    // isn't drawn there either, as on the neutron star's screenshot.
    bool alpha = false;
    if ((descriptor & 0x0F) != 0)
        for (size_t i = 0; i < size_t(sheetW) * size_t(sheetH) && !alpha; ++i)
            alpha = (data[pixels + i * 2 + 1] & 0x80) != 0;
    const bool topDown = (descriptor & 0x20) != 0;
    out.frameWidth = frameW;
    out.frameHeight = frameH;
    const int across = sheetW / frameW;
    for (int f = 0; f < count; ++f) {
        image::Image img;
        img.w = frameW;
        img.h = frameH;
        img.rgba.resize(size_t(frameW) * size_t(frameH) * 4);
        for (int y = 0; y < frameH; ++y) {
            const int sheetY = f / across * frameH + y;
            const int row = topDown ? sheetY : sheetH - 1 - sheetY;
            for (int x = 0; x < frameW; ++x) {
                const int sheetX = f % across * frameW + x;
                const uint16_t v = readLE16(&data[pixels + (size_t(row) * size_t(sheetW) + size_t(sheetX)) * 2]);
                uint8_t *p = img.pixel(x, y);
                // 5 bits a colour, widened as the game's 16-bit screen shows them.
                p[0] = uint8_t((v >> 10 & 31) << 3);
                p[1] = uint8_t((v >> 5 & 31) << 3);
                p[2] = uint8_t((v & 31) << 3);
                p[3] = (alpha ? (v & 0x8000) != 0 : (v & 0x7FFF) != 0) ? 255 : 0;
            }
        }
        out.frames.push_back(std::move(img));
    }
    return Error::success();
}

} // namespace reluva::botf
