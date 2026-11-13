// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/graphics.h"

#include "common/bytes.h"
#include "common/dcl.h"
#include "common/log.h"

#include <algorithm>
#include <cstring>

namespace reluva::stharbinger {

namespace {

const uint8_t kRleMagic[8] = {0x00, 0x00, 0x01, 0x01, 0x0A, 0xFF, 0x00, 0x00};
const uint32_t kRleFrameHeaderSize = 25;

} // namespace

bool loadCMP(SeekableReadStream &stream, Surface &surface, Palette *palette, bool *hasPalette) {
    if (hasPalette)
        *hasPalette = false;
    std::vector<uint8_t> packed = stream.readAll();
    if (packed.size() < 6)
        return false;
    std::vector<uint8_t> pixels;
    if (!decompressDCL(packed.data() + 4, uint32_t(packed.size() - 4), pixels))
        return false;

    static const struct {
        int w, h;
    } kSizes[] = {{640, 480}, {640, 400}, {320, 200}, {640, 350}};
    int w = 0;
    int h = 0;
    bool tail = false;
    for (const auto &s : kSizes) {
        size_t n = size_t(s.w) * s.h;
        if (pixels.size() == n || pixels.size() == n + 768) {
            w = s.w;
            h = s.h;
            tail = pixels.size() == n + 768;
            break;
        }
    }
    if (!w && pixels.size() % 640 == 0) {
        w = 640;
        h = int(pixels.size() / 640);
    }
    if (!w) {
        log::warning("CMP: unexpected size %u", unsigned(pixels.size()));
        return false;
    }
    draw::create(surface, w, h);
    std::memcpy(surface.pixels.data(), pixels.data(), size_t(w) * h);
    if (tail && palette) {
        palette->loadVga(pixels.data() + size_t(w) * h);
        if (hasPalette)
            *hasPalette = true;
    }
    return true;
}

bool loadPAL(SeekableReadStream &stream, Palette &palette) {
    uint8_t data[768];
    if (stream.read(data, sizeof(data)) != sizeof(data))
        return false;
    palette.loadVga(data);
    return true;
}

bool RleAnimation::load(SeekableReadStream &stream) {
    std::vector<uint8_t> data = stream.readAll();
    return load(data.data(), uint32_t(data.size()));
}

bool RleAnimation::load(const uint8_t *data, uint32_t size) {
    return parse(data, size, true);
}

bool RleAnimation::loadWithHeader(SeekableReadStream &stream, uint8_t header[8]) {
    std::vector<uint8_t> data = stream.readAll();
    if (data.size() < 10)
        return false;
    std::memcpy(header, data.data(), 8);
    return parse(data.data(), uint32_t(data.size()), false);
}

bool RleAnimation::parse(const uint8_t *data, uint32_t size, bool checkHeader) {
    _frames.clear();
    if (size < 10 || (checkHeader && std::memcmp(data, kRleMagic, sizeof(kRleMagic)) != 0))
        return false;
    uint32_t count = readLE16(data + 8);
    uint32_t pos = 10;
    while (_frames.size() < count && pos + kRleFrameHeaderSize <= size) {
        const uint8_t *hdr = data + pos;
        uint32_t length = readLE32(hdr + 21);
        if (pos + kRleFrameHeaderSize + length > size) {
            log::warning("RLE: frame %u runs past the end of the file", unsigned(_frames.size()));
            break;
        }
        Frame f;
        f.key = hdr[0];
        f.w = readLE16(hdr + 1);
        f.h = readLE16(hdr + 3);
        f.x = int16_t(readLE16(hdr + 5));
        f.y = int16_t(readLE16(hdr + 7));
        for (int m = 0; m < 3; m++) {
            f.muzzleX[m] = int16_t(readLE16(hdr + 9 + m * 2));
            f.muzzleY[m] = int16_t(readLE16(hdr + 15 + m * 2));
        }
        f.data.assign(hdr + kRleFrameHeaderSize, hdr + kRleFrameHeaderSize + length);
        _frames.push_back(std::move(f));
        pos += kRleFrameHeaderSize + length;
    }
    return !_frames.empty();
}

void RleAnimation::drawFrame(int i, Surface &dst, int dx, int dy) const {
    if (i < 0 || i >= frameCount())
        return;
    drawFrameAt(i, dst, _frames[i].x + dx, _frames[i].y + dy);
}

void RleAnimation::drawFrameAt(int i, Surface &dst, int ox, int oy, const Rect *clip) const {
    if (i < 0 || i >= frameCount())
        return;
    const Frame &f = _frames[i];
    Rect bounds(0, 0, dst.w, dst.h);
    if (clip)
        bounds = Rect(std::max(0, clip->left), std::max(0, clip->top), std::min(dst.w, clip->right),
                      std::min(dst.h, clip->bottom));
    const uint8_t *p = f.data.data();
    const uint8_t *end = p + f.data.size();
    int row = -1;
    int x = 0;

    // Writes `n` pixels of the current row, clipped to the frame and the surface.
    auto put = [&](const uint8_t *src, int n, bool run) {
        int sy = oy + row;
        if (row < f.h && sy >= bounds.top && sy < bounds.bottom) {
            for (int k = 0; k < n; k++) {
                int fx = x + k;
                int sx = ox + fx;
                if (fx < f.w && sx >= bounds.left && sx < bounds.right)
                    *draw::at(dst, sx, sy) = run ? *src : src[k];
            }
        }
        x += n;
    };

    // The top two bits of each code select the operation, the low six are a count:
    // 00 copy n bytes, 01 start a new row and skip n, 10 repeat the next byte n times,
    // 11 skip n pixels.
    while (p < end) {
        uint8_t code = *p++;
        int n = code & 0x3F;
        int op = code >> 6;
        if (op == 1) {
            row++;
            x = n;
            continue;
        }
        if (row < 0) {
            row = 0;
            x = 0;
        }
        if (op == 3) {
            x += n;
        } else if (op == 2) {
            if (p >= end)
                break;
            put(p++, n, true);
        } else {
            n = std::min<int>(n, int(end - p));
            put(p, n, false);
            p += n;
        }
    }
}

void drawPhaserBeam(Surface &dst, int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int len = std::max(dx, dy);
    int every = std::max(1, len / 12);
    int width = 12;
    int err = dx - dy;
    int x = x0;
    int y = y0;
    for (int i = 0; i <= len; i++) {
        int half = width >> 1;
        for (int k = -half; k <= half; k++)
            if (x >= 0 && x < dst.w && y + k >= 0 && y + k < dst.h)
                *draw::at(dst, x, y + k) = color;
        if (i % every == every - 1 && width > 1)
            width--;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

} // namespace reluva::stharbinger
