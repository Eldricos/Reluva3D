// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/cinepak.h"

#include <algorithm>
#include <cstring>

namespace reluva::cinepak {

namespace {

uint32_t be16(const uint8_t *p) {
    return uint32_t(p[0]) << 8 | p[1];
}
uint32_t be24(const uint8_t *p) {
    return uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
}
uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | be24(p + 1);
}

uint8_t clip(int v) {
    return uint8_t(v < 0 ? 0 : v > 255 ? 255 : v);
}

} // namespace

Decoder::Decoder(int width, int height) : _width(width), _height(height) {
    _picture.create(std::max(0, width), std::max(0, height), PixelFormat::kRGBA32);
    for (size_t i = 3; i < _picture.pixels.size(); i += 4)
        _picture.pixels[i] = 255;
    _strips.resize(1);
    std::memset(static_cast<void *>(_strips.data()), 0, sizeof(Strip));
}

void Decoder::decodeCodebook(Cell *codebook, int chunk, const uint8_t *data, const uint8_t *end) {
    // Greyscale codebooks (0x04) have no colour bytes; partial ones (0x01)
    // say which entries change with a bit each, in 32-bit words.
    const int n = (chunk & 0x04) ? 4 : 6;
    uint32_t flags = 0;
    uint32_t mask = 0;
    for (int i = 0; i < 256; ++i) {
        if (chunk & 0x01) {
            mask >>= 1;
            if (!mask) {
                if (data + 4 > end)
                    return;
                flags = be32(data);
                data += 4;
                mask = 0x80000000;
            }
            if (!(flags & mask))
                continue;
        }
        if (data + n > end)
            return;
        Cell &cell = codebook[i];
        for (int j = 0; j < 4; ++j)
            cell.y[j] = *data++;
        if (n == 6) {
            cell.u = int8_t(*data++);
            cell.v = int8_t(*data++);
        } else {
            cell.u = cell.v = 0;
        }
    }
}

bool Decoder::decodeVectors(const Strip &strip, int chunk, int top, int bottom, const uint8_t *data,
                            const uint8_t *end) {
    // 0x30: every block coded, a bit each for V1 or V4. 0x31: a bit for
    // skipped or coded first. 0x32: every block V1.
    uint32_t flags = 0;
    uint32_t mask = 0;
    auto nextBit = [&](bool &bit) {
        mask >>= 1;
        if (!mask) {
            if (data + 4 > end)
                return false;
            flags = be32(data);
            data += 4;
            mask = 0x80000000;
        }
        bit = (flags & mask) != 0;
        return true;
    };
    // Cinepak's YUV: R = Y + 2V, G = Y - U/2 - V, B = Y + 2U.
    auto put = [&](int x, int y, const Cell &cell, int i) {
        if (x >= _width || y >= bottom)
            return;
        const int luma = cell.y[i];
        uint8_t *p = _picture.row(y) + size_t(x) * 4;
        p[0] = clip(luma + 2 * cell.v);
        p[1] = clip(luma - cell.u / 2 - cell.v);
        p[2] = clip(luma + 2 * cell.u);
        p[3] = 255;
    };

    for (int y = top; y < bottom; y += 4) {
        for (int x = 0; x < _width; x += 4) {
            bool bit = false;
            if (chunk & 0x01) {
                if (!nextBit(bit))
                    return false;
                if (!bit)
                    continue;
            }
            bool v4 = false;
            if (!(chunk & 0x02)) {
                if (!nextBit(bit))
                    return false;
                v4 = bit;
            }
            if (!v4) {
                if (data + 1 > end)
                    return false;
                const Cell &cell = strip.v1[*data++];
                for (int i = 0; i < 4; ++i) {
                    const int cx = x + (i & 1) * 2;
                    const int cy = y + (i >> 1) * 2;
                    put(cx, cy, cell, i);
                    put(cx + 1, cy, cell, i);
                    put(cx, cy + 1, cell, i);
                    put(cx + 1, cy + 1, cell, i);
                }
            } else {
                if (data + 4 > end)
                    return false;
                for (int q = 0; q < 4; ++q) {
                    const Cell &cell = strip.v4[*data++];
                    const int cx = x + (q & 1) * 2;
                    const int cy = y + (q >> 1) * 2;
                    for (int i = 0; i < 4; ++i)
                        put(cx + (i & 1), cy + (i >> 1), cell, i);
                }
            }
        }
    }
    return true;
}

bool Decoder::decode(const uint8_t *data, uint32_t size) {
    if (size == 0)
        return true; // a dropped frame: the last one stays up
    if (size < 10)
        return false;
    const uint8_t *end = data + size;
    const uint8_t frameFlags = data[0];
    const int strips = int(be16(data + 8));
    data += 10;

    if (int(_strips.size()) < strips) {
        const size_t old = _strips.size();
        _strips.resize(size_t(strips));
        std::memset(static_cast<void *>(&_strips[old]), 0, (size_t(strips) - old) * sizeof(Strip));
    }

    int y0 = 0;
    for (int i = 0; i < strips; ++i) {
        if (data + 12 > end)
            return false;
        uint32_t stripSize = be24(data + 1);
        int y1 = int(be16(data + 4));
        int y2 = int(be16(data + 8));
        // A strip whose top is zero is placed under the last one.
        if (y1 == 0) {
            y1 = y0;
            y2 += y0;
        }
        if (stripSize < 12 || data + stripSize > end)
            stripSize = uint32_t(end - data);
        const uint8_t *stripEnd = data + stripSize;
        data += 12;

        Strip &strip = _strips[size_t(i)];
        if (i > 0 && !(frameFlags & 0x01))
            strip = _strips[size_t(i) - 1];

        y2 = std::min(y2, _height);
        while (data + 4 <= stripEnd) {
            const int chunk = data[0];
            uint32_t chunkSize = be24(data + 1);
            if (chunkSize < 4 || data + chunkSize > stripEnd)
                chunkSize = uint32_t(stripEnd - data);
            const uint8_t *body = data + 4;
            const uint8_t *bodyEnd = data + chunkSize;
            switch (chunk) {
            case 0x20:
            case 0x21:
            case 0x24:
            case 0x25:
                decodeCodebook(strip.v4, chunk, body, bodyEnd);
                break;
            case 0x22:
            case 0x23:
            case 0x26:
            case 0x27:
                decodeCodebook(strip.v1, chunk, body, bodyEnd);
                break;
            case 0x30:
            case 0x31:
            case 0x32:
                decodeVectors(strip, chunk, y1, y2, body, bodyEnd);
                break;
            default:
                break;
            }
            data += chunkSize;
        }
        data = stripEnd;
        y0 = y2;
    }
    return true;
}

} // namespace reluva::cinepak
