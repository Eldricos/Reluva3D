// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/overlay.h"

#include <cstring>

#include "engines/stgenerations/debug.h"

namespace reluva::stgenerations {

bool Overlay::load(SeekableReadStream &stream, int key) {
    _surface.free();
    _mask.clear();

    char magic[8];
    if (stream.read(magic, 8) != 8 || memcmp(magic, "OVERLAY\0", 8) != 0) {
        log::warning("Overlay: missing OVERLAY header");
        return false;
    }
    uint32_t width = stream.readUint32LE();
    uint32_t rows = stream.readUint32LE();
    if (width == 0 || width > 4096 || rows == 0 || rows > 65536) {
        log::warning("Overlay: implausible size %ux%u", width, rows);
        return false;
    }

    std::vector<uint32_t> offsets(rows);
    for (uint32_t &offset : offsets)
        offset = stream.readUint32LE();
    int64_t base = stream.pos();
    int64_t dataSize = stream.size() - base;

    _surface.create((int16_t)width, (int16_t)rows, Canvas::kClut8);
    _mask.assign((size_t)width * rows, 0);

    for (uint32_t row = 0; row < rows; row++) {
        if (offsets[row] > dataSize || !stream.seek(base + offsets[row])) {
            log::warning("Overlay: bad offset for row %u", row);
            return false;
        }
        uint8_t *pixels = (uint8_t *)_surface.getBasePtr(0, row);
        uint8_t *mask = &_mask[(size_t)row * width];
        uint32_t x = 0;
        while (x < width) {
            int16_t n = stream.readSint16LE();
            if (stream.eos()) {
                log::warning("Overlay: row %u is truncated", row);
                return false;
            }
            if (n < 0) {
                x += -n;
                continue;
            }
            if (n == 0 || x + n > width) {
                log::warning("Overlay: row %u has a run of %d at x=%u (width %u)", row, n, x, width);
                return false;
            }
            stream.read(pixels + x, n);
            for (int i = 0; i < n; i++)
                mask[x + i] = pixels[x + i] != key || key < 0;
            x += n;
            if (n & 1)
                stream.readByte(); // pad to even
        }
        if (x != width) {
            log::warning("Overlay: row %u is %u pixels wide, expected %u", row, x, width);
            return false;
        }
    }
    return true;
}

void Overlay::drawFrame(Canvas &dst, int frame, int frameHeight, int x, int y) const {
    // Each frame's rows are stored bottom up, like a DIB's; the frames
    // themselves run top down.
    int top = frame * frameHeight;
    for (int fy = 0; fy < frameHeight; fy++) {
        int row = top + frameHeight - 1 - fy;
        if (row < 0 || row >= _surface.h)
            continue;
        const uint8_t *src = (const uint8_t *)_surface.getBasePtr(0, row);
        const uint8_t *mask = &_mask[(size_t)row * _surface.w];
        for (int fx = 0; fx < _surface.w; fx++) {
            if (mask[fx])
                dst.setPixel(x + fx, y + fy, src[fx]);
        }
    }
}

} // namespace reluva::stgenerations
