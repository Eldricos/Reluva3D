// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Windows bitmaps (BITMAPINFOHEADER and later headers; OS/2's aren't
// read): uncompressed at 1, 4, 8, 16, 24 and 32 bits, BI_BITFIELDS at 16
// and 32, and RLE8. From Microsoft's documentation of the format. It came
// to Reluva with the Star Trek: Generations engine (STG-Engine).

#include "common/bmp.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace reluva::image {

namespace {

constexpr uint32_t kRgb = 0, kRle8 = 1, kBitfields = 3;

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

int maskShift(uint32_t mask) {
    int shift = 0;
    while (mask && !(mask & 1)) {
        mask >>= 1;
        ++shift;
    }
    return shift;
}

int maskBits(uint32_t mask) {
    int bits = 0;
    for (; mask; mask >>= 1)
        bits += int(mask & 1);
    return bits;
}

// A channel from a 16- or 32-bit pixel, scaled to 8 bits.
uint8_t extract(uint32_t pixel, uint32_t mask) {
    if (!mask)
        return 0;
    const uint32_t v = (pixel & mask) >> maskShift(mask);
    const int bits = maskBits(mask);
    if (bits >= 8)
        return uint8_t(v >> (bits - 8));
    return uint8_t(v * 255 / ((1u << bits) - 1));
}

void decodeRle8(const uint8_t* p, const uint8_t* end, Surface& s, bool bottomUp) {
    int x = 0, y = 0;
    auto put = [&](uint8_t color) {
        if (x < s.w && y < s.h)
            s.row(bottomUp ? s.h - 1 - y : y)[x] = color;
        ++x;
    };
    while (p + 2 <= end) {
        const uint8_t count = *p++;
        const uint8_t value = *p++;
        if (count > 0) {
            for (int i = 0; i < count; ++i)
                put(value);
            continue;
        }
        switch (value) {
        case 0: // end of line
            x = 0;
            ++y;
            break;
        case 1: // end of bitmap
            return;
        case 2: // delta
            if (p + 2 > end)
                return;
            x += p[0];
            y += p[1];
            p += 2;
            break;
        default: // a run of single pixels, padded to a 16-bit boundary
            for (int i = 0; i < value && p < end; ++i)
                put(*p++);
            if ((value & 1) && p < end)
                ++p;
            break;
        }
    }
    // Some encoders leave out the end-of-bitmap marker.
}

// `info` is the BITMAPINFOHEADER; `pixels` where the pixels start (or
// nullptr: straight after the palette).
Error decodeInfo(const uint8_t* data, size_t size, size_t info, const uint8_t* pixels, Bitmap& out) {
    out = Bitmap{};
    if (info + 40 > size)
        return Error::fail("BMP: too short for its header");
    const uint8_t* h = data + info;
    const uint32_t headerSize = le32(h);
    if (headerSize < 40)
        return Error::fail("BMP: an OS/2 bitmap (header of " + std::to_string(headerSize) + " bytes)");
    const int32_t width = int32_t(le32(h + 4));
    int32_t height = int32_t(le32(h + 8));
    const int bits = le16(h + 14);
    const uint32_t compression = le32(h + 16);
    const uint32_t colorsUsed = le32(h + 32);
    const bool bottomUp = height > 0;
    height = std::abs(height);
    if (width <= 0 || width > 8192 || height == 0 || height > 8192)
        return Error::fail("BMP: a size of " + std::to_string(width) + "x" + std::to_string(height));

    uint32_t rMask = 0x7C00, gMask = 0x03E0, bMask = 0x001F;
    if (bits == 32)
        rMask = 0xFF0000, gMask = 0xFF00, bMask = 0xFF;
    size_t paletteAt = info + headerSize;
    if (compression == kBitfields) {
        if (info + 52 > size)
            return Error::fail("BMP: too short for its bit fields");
        rMask = le32(h + 40);
        gMask = le32(h + 44);
        bMask = le32(h + 48);
        if (headerSize == 40)
            paletteAt += 12;
    } else if (compression != kRgb && !(compression == kRle8 && bits == 8)) {
        return Error::fail("BMP: compression " + std::to_string(compression) + " isn't read");
    }

    if (bits <= 8) {
        if (bits != 8 && bits != 4 && bits != 1)
            return Error::fail("BMP: " + std::to_string(bits) + " bits per pixel isn't read");
        int count = colorsUsed ? int(colorsUsed) : 1 << bits;
        if (count > 256)
            count = 256;
        for (int i = 0; i < count; ++i) {
            const size_t at = paletteAt + size_t(i) * 4;
            if (at + 4 > size)
                break; // cut short: the rest black
            out.palette.setColor(i, data[at + 2], data[at + 1], data[at]);
        }
        out.paletteColors = count;
        paletteAt += size_t(count) * 4;
        out.surface.create(width, height, PixelFormat::CLUT8);
    } else if (bits == 16 || bits == 24 || bits == 32) {
        out.surface.create(width, height, PixelFormat::RGBA32);
    } else {
        return Error::fail("BMP: " + std::to_string(bits) + " bits per pixel isn't read");
    }
    if (!pixels)
        pixels = data + std::min(paletteAt, size);
    const uint8_t* end = data + size;
    if (pixels > end)
        return Error::fail("BMP: its pixels are past its end");

    if (compression == kRle8) {
        decodeRle8(pixels, end, out.surface, bottomUp);
        return Error::success();
    }

    const size_t pitch = (size_t(width) * size_t(bits) + 31) / 32 * 4;
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = pixels + size_t(y) * pitch;
        if (row + pitch > end)
            return Error::fail("BMP: its pixels end early");
        uint8_t* dst = out.surface.row(bottomUp ? height - 1 - y : y);
        for (int x = 0; x < width; ++x) {
            switch (bits) {
            case 1: dst[x] = (row[x >> 3] >> (7 - (x & 7))) & 1; break;
            case 4: dst[x] = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 0xF; break;
            case 8: dst[x] = row[x]; break;
            case 16: {
                const uint32_t p = le16(row + x * 2);
                uint8_t* d = dst + x * 4;
                d[0] = extract(p, rMask), d[1] = extract(p, gMask), d[2] = extract(p, bMask), d[3] = 255;
                break;
            }
            case 24: {
                uint8_t* d = dst + x * 4;
                d[0] = row[x * 3 + 2], d[1] = row[x * 3 + 1], d[2] = row[x * 3], d[3] = 255;
                break;
            }
            default: { // 32
                const uint32_t p = le32(row + x * 4);
                uint8_t* d = dst + x * 4;
                d[0] = extract(p, rMask), d[1] = extract(p, gMask), d[2] = extract(p, bMask), d[3] = 255;
                break;
            }
            }
        }
    }
    return Error::success();
}

} // namespace

Error decodeBmp(const uint8_t* data, size_t size, Bitmap& out) {
    if (size < 14 || data[0] != 'B' || data[1] != 'M') {
        out = Bitmap{};
        return Error::fail("not a BMP file");
    }
    const uint32_t offset = le32(data + 10);
    if (offset > size) {
        out = Bitmap{};
        return Error::fail("BMP: its pixels are past its end");
    }
    return decodeInfo(data, size, 14, data + offset, out);
}

Error decodeDib(const uint8_t* data, size_t size, Bitmap& out) { return decodeInfo(data, size, 0, nullptr, out); }

} // namespace reluva::image
