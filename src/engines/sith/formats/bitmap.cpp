// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/bitmap.h"

#include "common/bytes.h"

#include <cstring>
#include <string>

namespace reluva::sith {

namespace {

constexpr uint32_t kVersion = 0x46;

// A channel of `bits` bits widened to 8, its top bits repeated below.
uint8_t widen(uint32_t value, uint32_t bits) {
    if (bits == 0)
        return 0;
    if (bits >= 8)
        return uint8_t(value >> (bits - 8));
    uint32_t out = value << (8 - bits);
    for (uint32_t shift = bits; shift < 8; shift += bits)
        out |= value << (8 - bits) >> shift;
    return uint8_t(out);
}

} // namespace

Error parseBitmap(const uint8_t *data, size_t size, Bitmap &out, size_t *used) {
    out = Bitmap{};
    if (size < kBitmapHeaderSize || std::memcmp(data, "BM  ", 4) != 0)
        return Error::fail("not a .bm picture: no BM header");
    if (readLE32(data + 4) != kVersion)
        return Error::fail(".bm version " + std::to_string(readLE32(data + 4)) + ", not 70");
    out.flags = readLE32(data + 12);
    const uint32_t count = readLE32(data + 16);
    out.x = int32_t(readLE32(data + 20));
    out.y = int32_t(readLE32(data + 24));
    out.transparentColor = readLE32(data + 28);
    out.field8 = readLE32(data + 32);
    out.bitsPerPixel = int(readLE32(data + 36));
    for (int i = 0; i < 3; ++i) {
        out.channelBits[i] = readLE32(data + 40 + 4 * i);
        out.channelShifts[i] = readLE32(data + 52 + 4 * i);
    }
    if (out.bitsPerPixel != 8 && out.bitsPerPixel != 16)
        return Error::fail(".bm with " + std::to_string(out.bitsPerPixel) + " bits a pixel");
    for (int i = 0; out.bitsPerPixel == 16 && i < 3; ++i)
        if (out.channelBits[i] > 16 || out.channelShifts[i] + out.channelBits[i] > 16)
            return Error::fail(".bm's 16-bit channels don't fit in 16 bits");
    const size_t bytesPerPixel = size_t(out.bitsPerPixel) / 8;

    size_t pos = kBitmapHeaderSize;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 8 > size)
            return Error::fail(".bm cut short in cel " + std::to_string(i));
        Bitmap::Cel cel;
        cel.w = int(readLE32(data + pos));
        cel.h = int(readLE32(data + pos + 4));
        pos += 8;
        const uint64_t bytes = uint64_t(uint32_t(cel.w)) * uint32_t(cel.h) * bytesPerPixel;
        if (cel.w < 0 || cel.h < 0 || bytes > size - pos)
            return Error::fail(".bm cel " + std::to_string(i) + " runs past the end");
        cel.pixels.assign(data + pos, data + pos + bytes);
        pos += size_t(bytes);
        out.cels.push_back(std::move(cel));
    }
    if (out.flags & kBitmapOwnPalette) {
        if (pos + 768 > size)
            return Error::fail(".bm cut short in its palette");
        Palette pal;
        for (Color &c : pal) {
            c = {data[pos], data[pos + 1], data[pos + 2]};
            pos += 3;
        }
        out.palette = pal;
    }
    if (used)
        *used = pos;
    return Error::success();
}

image::Image bitmapToImage(const Bitmap &bm, const Bitmap::Cel &cel, const Palette &palette, bool transparent) {
    const bool keyed = transparent && (bm.flags & kBitmapTransparent);
    image::Image img;
    img.w = cel.w;
    img.h = cel.h;
    img.rgba.resize(size_t(cel.w) * cel.h * 4);
    const Palette &pal = bm.palette ? *bm.palette : palette;
    for (int y = 0; y < cel.h; ++y)
        for (int x = 0; x < cel.w; ++x) {
            uint8_t *p = img.pixel(x, y);
            const size_t i = size_t(y) * cel.w + x;
            uint32_t value = 0;
            if (bm.bitsPerPixel == 8) {
                value = cel.pixels[i];
                p[0] = pal[value].r;
                p[1] = pal[value].g;
                p[2] = pal[value].b;
            } else {
                value = readLE16(&cel.pixels[i * 2]);
                for (int c = 0; c < 3; ++c) {
                    const uint32_t bits = bm.channelBits[c];
                    p[c] = widen((value >> bm.channelShifts[c]) & ((1u << bits) - 1), bits);
                }
            }
            p[3] = keyed && value == bm.transparentColor ? 0 : 255;
        }
    return img;
}

} // namespace reluva::sith
