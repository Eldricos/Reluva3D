// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/canvas.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace reluva::stgenerations {

Canvas &Canvas::operator=(Canvas &&other) noexcept {
    if (this != &other) {
        w = other.w;
        h = other.h;
        pitch = other.pitch;
        format_ = other.format_;
        pixels_ = std::move(other.pixels_);
        other.pixels_.clear();
        other.w = other.h = other.pitch = 0;
    }
    return *this;
}

void Canvas::create(int width, int height, Format format) {
    format_ = format;
    if (width <= 0 || height <= 0) {
        free();
        format_ = format;
        return;
    }
    w = width;
    h = height;
    pitch = width * bytesPerPixel();
    pixels_.assign(size_t(height) * size_t(pitch), 0);
}

void Canvas::free() {
    pixels_.clear();
    pixels_.shrink_to_fit();
    w = h = pitch = 0;
}

uint32_t Canvas::getPixel(int x, int y) const {
    const uint8_t *p = static_cast<const uint8_t *>(getBasePtr(x, y));
    if (format_ == kClut8)
        return *p;
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void Canvas::setPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= w || y >= h)
        return;
    uint8_t *p = static_cast<uint8_t *>(getBasePtr(x, y));
    if (format_ == kClut8)
        *p = uint8_t(color);
    else
        std::memcpy(p, &color, 4);
}

void Canvas::fillRect(Rect r, uint32_t color) {
    r.clip(Rect(0, 0, w, h));
    if (r.isEmpty())
        return;
    for (int y = r.top; y < r.bottom; y++) {
        if (format_ == kClut8) {
            std::memset(getBasePtr(r.left, y), int(color & 0xFF), size_t(r.width()));
        } else {
            for (int x = r.left; x < r.right; x++)
                setPixel(x, y, color);
        }
    }
}

void Canvas::frameRect(const Rect &r, uint32_t color) {
    hLine(r.left, r.top, r.right - 1, color);
    hLine(r.left, r.bottom - 1, r.right - 1, color);
    vLine(r.left, r.top, r.bottom - 1, color);
    vLine(r.right - 1, r.top, r.bottom - 1, color);
}

void Canvas::hLine(int x1, int y, int x2, uint32_t color) {
    if (x1 > x2)
        std::swap(x1, x2);
    fillRect(Rect(x1, y, x2 + 1, y + 1), color);
}

void Canvas::vLine(int x, int y1, int y2, uint32_t color) {
    if (y1 > y2)
        std::swap(y1, y2);
    fillRect(Rect(x, y1, x + 1, y2 + 1), color);
}

void Canvas::drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    // Bresenham's.
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        setPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Canvas::fillPolygon(const Point *points, int count, uint32_t color) {
    if (count < 3)
        return;
    int minY = points[0].y;
    int maxY = points[0].y;
    for (int i = 1; i < count; i++) {
        minY = std::min(minY, points[i].y);
        maxY = std::max(maxY, points[i].y);
    }
    minY = std::max(minY, 0);
    maxY = std::min(maxY, h - 1);
    std::vector<int> crossings;
    for (int y = minY; y <= maxY; y++) {
        const double sampleY = y + 0.5;
        crossings.clear();
        for (int i = 0; i < count; i++) {
            const Point &a = points[i];
            const Point &b = points[(i + 1) % count];
            if ((a.y <= sampleY) == (b.y <= sampleY))
                continue;
            const double t = (sampleY - a.y) / double(b.y - a.y);
            crossings.push_back(int(a.x + t * (b.x - a.x) + 0.5));
        }
        std::sort(crossings.begin(), crossings.end());
        for (size_t i = 0; i + 1 < crossings.size(); i += 2)
            if (crossings[i + 1] > crossings[i])
                hLine(crossings[i], y, crossings[i + 1] - 1, color);
    }
}

void Canvas::copyRectToSurface(const void *buffer, int srcPitch, int destX, int destY, int width, int height) {
    const uint8_t *src = static_cast<const uint8_t *>(buffer);
    const int bpp = bytesPerPixel();
    // Clipped, the source's corner moved with it.
    if (destX < 0) {
        src -= destX * bpp;
        width += destX;
        destX = 0;
    }
    if (destY < 0) {
        src -= ptrdiff_t(destY) * srcPitch;
        height += destY;
        destY = 0;
    }
    if (destX + width > w)
        width = w - destX;
    if (destY + height > h)
        height = h - destY;
    if (width <= 0 || height <= 0)
        return;
    for (int y = 0; y < height; y++)
        std::memcpy(getBasePtr(destX, destY + y), src + ptrdiff_t(y) * srcPitch, size_t(width) * bpp);
}

void Canvas::copyRectToSurface(const Canvas &src, int destX, int destY, const Rect &subRect) {
    Rect r = subRect;
    r.clip(Rect(0, 0, src.w, src.h));
    if (r.isEmpty())
        return;
    copyRectToSurface(src.getBasePtr(r.left, r.top), src.pitch, destX, destY, r.width(), r.height());
}

void Canvas::transBlitFrom(const Canvas &src, int destX, int destY, uint32_t transColor) {
    for (int y = 0; y < src.h; y++) {
        const int dy = destY + y;
        if (dy < 0 || dy >= h)
            continue;
        for (int x = 0; x < src.w; x++) {
            const uint32_t color = src.getPixel(x, y);
            if (color != transColor)
                setPixel(destX + x, dy, color);
        }
    }
}

} // namespace reluva::stgenerations
