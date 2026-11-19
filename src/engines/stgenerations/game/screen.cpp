// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/screen.h"

#include "engines/stgenerations/platform.h"

#include <algorithm>

namespace reluva::stgenerations {

Screen::Screen(Platform *system) : _system(system) {
}

void Screen::init() {
    _system->initSize(kWidth, kHeight);
    _surface.create(kWidth, kHeight, Canvas::kClut8);
}

void Screen::clear(uint8_t color) {
    _surface.fillRect(Rect(0, 0, kWidth, kHeight), color);
}

void Screen::applyPalette() {
    _system->setPalette(_palette.data(), 0, 256);
}

void Screen::drawCentered(const Canvas &image) {
    int x = (kWidth - image.w) / 2;
    int y = (kHeight - image.h) / 2;

    if (image.isClut8()) {
        _surface.copyRectToSurface(image, x, y, Rect(0, 0, image.w, image.h));
        return;
    }

    // Truecolour: map each pixel onto the current palette.
    for (int sy = 0; sy < image.h; sy++) {
        for (int sx = 0; sx < image.w; sx++) {
            uint8_t r, g, b;
            argbToRgb(image.getPixel(sx, sy), r, g, b);
            _surface.setPixel(x + sx, y + sy, _palette.findBestColor(r, g, b));
        }
    }
}

void Screen::drawImage(const Canvas &image, int areaHeight) {
    if (image.w <= 0 || image.h <= 0)
        return;
    int scale = std::min(kWidth / image.w, areaHeight / image.h);
    scale = std::max(1, std::min(scale, 3));
    if (scale == 1 || !image.isClut8()) {
        // Centre unscaled images in the same area.
        int x = (kWidth - image.w) / 2;
        int y = std::max(0, (areaHeight - image.h) / 2);
        if (image.isClut8())
            _surface.copyRectToSurface(image, x, y, Rect(0, 0, image.w, image.h));
        else
            drawCentered(image);
        return;
    }

    int x0 = (kWidth - image.w * scale) / 2;
    int y0 = (areaHeight - image.h * scale) / 2;
    for (int y = 0; y < image.h * scale; y++) {
        const uint8_t *src = (const uint8_t *)image.getBasePtr(0, y / scale);
        for (int x = 0; x < image.w * scale; x++)
            _surface.setPixel(x0 + x, y0 + y, src[x / scale]);
    }
}

void Screen::update() {
    _system->copyRectToScreen(_surface.getPixels(), _surface.pitch, 0, 0, kWidth, kHeight);
    _system->updateScreen();
}

} // namespace reluva::stgenerations
