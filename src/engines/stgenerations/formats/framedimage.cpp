// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/framedimage.h"

#include <algorithm>

#include "engines/stgenerations/formats/bitmap.h"

namespace reluva::stgenerations {

void FramedImage::set(const BitmapDecoder &decoder, const Palette &palette, int height) {
    const Canvas &s = decoder.getSurface();
    surface.create(s.w, s.h, Canvas::kClut8);
    surface.copyRectToSurface(s, 0, 0, Rect(0, 0, s.w, s.h));
    firstPixel = *(const uint8_t *)s.getBasePtr(0, s.h - 1);
    const Palette &own = decoder.getPalette();
    for (int i = 0; i < 256; i++) {
        uint8_t r, g, b;
        own.getColor(i, r, g, b);
        remap[i] = palette.findBestColor(r, g, b);
    }
    frameHeight = height < 0 ? s.h / -height : height;
}

int FramedImage::getFrameCount() const {
    return frameHeight > 0 ? std::max(1, (int)surface.h / frameHeight) : 1;
}

void FramedImage::draw(Canvas &dst, int frame, int x, int y, int key, const Rect *clip) const {
    if (key == kFirstPixel)
        key = firstPixel;
    int height = getHeight();
    // Frames count up from the bottom of the strip: 0x40390E takes frame n
    // at (count - n - 1) frames into rows already turned top down.
    frame = getFrameCount() - 1 - std::max(0, std::min(frame, getFrameCount() - 1));
    Rect area(0, 0, dst.w, dst.h);
    if (clip)
        area.clip(*clip);
    for (int row = 0; row < height; row++) {
        int sy = y + row;
        if (sy < area.top || sy >= area.bottom)
            continue;
        const uint8_t *src = (const uint8_t *)surface.getBasePtr(0, frame * height + row);
        uint8_t *out = (uint8_t *)dst.getBasePtr(0, sy);
        for (int col = 0; col < surface.w; col++) {
            int sx = x + col;
            if (sx < area.left || sx >= area.right || src[col] == key)
                continue;
            out[sx] = remap[src[col]];
        }
    }
}

} // namespace reluva::stgenerations
