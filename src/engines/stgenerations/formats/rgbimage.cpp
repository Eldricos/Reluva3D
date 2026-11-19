// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/rgbimage.h"

#include <algorithm>

#include "engines/stgenerations/formats/bitmap.h"
#include "engines/stgenerations/formats/overlay.h"

namespace reluva::stgenerations {

namespace {

uint32_t toRgb(const Palette &palette, uint8_t index) {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    palette.getColor(index, r, g, b);
    return 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

} // namespace

bool RgbImage::load(const BitmapDecoder &decoder, int frames, int key) {
    const Canvas &s = decoder.getSurface();
    if (!s.isClut8() || s.w <= 0 || s.h <= 0)
        return false;
    const Palette &palette = decoder.getPalette();
    _w = s.w;
    _h = s.h;
    _frames = std::max(1, frames);
    _frameHeight = std::max(1, _h / _frames);
    _fromBottom = true;
    _pixels.resize((size_t)_w * _h);
    for (int y = 0; y < _h; y++) {
        const uint8_t *src = (const uint8_t *)s.getBasePtr(0, y);
        for (int x = 0; x < _w; x++)
            _pixels[(size_t)y * _w + x] = src[x] == key ? 0 : toRgb(palette, src[x]);
    }
    return true;
}

void RgbImage::load(const Overlay &overlay, const Palette &palette, int frameHeight) {
    const Canvas &s = overlay.getSurface();
    _w = s.w;
    _h = s.h;
    _frameHeight = std::max(1, std::min(frameHeight, _h));
    _frames = std::max(1, (_h + _frameHeight - 1) / _frameHeight);
    _fromBottom = false;
    _pixels.assign((size_t)_w * _frames * _frameHeight, 0);
    // Each frame's rows are stored bottom up.
    for (int f = 0; f < _frames; f++) {
        int top = f * _frameHeight;
        int height = std::min(_frameHeight, _h - top);
        for (int y = 0; y < height; y++) {
            int srcY = top + height - 1 - y;
            const uint8_t *src = (const uint8_t *)s.getBasePtr(0, srcY);
            uint32_t *out = &_pixels[((size_t)top + y) * _w];
            for (int x = 0; x < _w; x++)
                out[x] = overlay.isOpaque(x, srcY) ? toRgb(palette, src[x]) : 0;
        }
    }
    _h = _frames * _frameHeight;
}

const uint32_t *RgbImage::row(int frame, int y) const {
    frame = std::max(0, std::min(frame, _frames - 1));
    if (_fromBottom)
        frame = _frames - 1 - frame;
    return &_pixels[((size_t)frame * _frameHeight + y) * _w];
}

uint32_t RgbImage::getPixel(int frame, int x, int y) const {
    if (!isLoaded() || x < 0 || y < 0 || x >= _w || y >= _frameHeight)
        return 0;
    return row(frame, y)[x];
}

void RgbImage::draw(Canvas &dst, int frame, int x, int y, const Rect *clip) const {
    drawScaled(dst, frame, x, y, _w, _frameHeight, clip);
}

void RgbImage::drawScaled(Canvas &dst, int frame, int x, int y, int w, int h, const Rect *clip) const {
    if (!isLoaded() || w <= 0 || h <= 0)
        return;
    Rect area(0, 0, dst.w, dst.h);
    if (clip)
        area.clip(*clip);
    int x0 = std::max<int>(x, area.left);
    int x1 = std::min<int>(x + w, area.right);
    int y0 = std::max<int>(y, area.top);
    int y1 = std::min<int>(y + h, area.bottom);
    for (int dy = y0; dy < y1; dy++) {
        const uint32_t *src = row(frame, (dy - y) * _frameHeight / h);
        uint32_t *out = (uint32_t *)dst.getBasePtr(0, dy);
        for (int dx = x0; dx < x1; dx++) {
            uint32_t c = src[(dx - x) * _w / w];
            if (c >> 24)
                out[dx] = c;
        }
    }
}

} // namespace reluva::stgenerations
