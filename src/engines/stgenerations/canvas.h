// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"

#include <cstdint>
#include <vector>

namespace reluva::stgenerations {

// A picture the game draws on: 8-bit palette indices, or true colour as
// 32-bit 0xAARRGGBB values (the bridge, space combat and the movies, which
// the original showed on a 16-bit screen). Everything is clipped to it.
class Canvas {
public:
    enum Format { kClut8, kArgb32 };

    int w = 0, h = 0;
    int pitch = 0;

    Canvas() = default;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    Canvas(Canvas&& other) noexcept { *this = std::move(other); }
    Canvas& operator=(Canvas&& other) noexcept;

    // width x height pixels, all 0.
    void create(int width, int height, Format format);
    void free();
    bool empty() const { return pixels_.empty(); }

    Format format() const { return format_; }
    bool isClut8() const { return format_ == kClut8; }
    int bytesPerPixel() const { return format_ == kClut8 ? 1 : 4; }

    void* getPixels() { return pixels_.data(); }
    const void* getPixels() const { return pixels_.data(); }
    void* getBasePtr(int x, int y) { return pixels_.data() + size_t(y) * size_t(pitch) + size_t(x) * bytesPerPixel(); }
    const void* getBasePtr(int x, int y) const {
        return pixels_.data() + size_t(y) * size_t(pitch) + size_t(x) * bytesPerPixel();
    }

    uint32_t getPixel(int x, int y) const;
    void setPixel(int x, int y, uint32_t color);
    void fillRect(Rect r, uint32_t color);
    void frameRect(const Rect& r, uint32_t color);
    void hLine(int x1, int y, int x2, uint32_t color);
    void vLine(int x, int y1, int y2, uint32_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    // Filled, sampled at pixel centres (each edge with its top end only).
    void fillPolygon(const Point* points, int count, uint32_t color);
    // Pixels in this canvas's format, `srcPitch` bytes apart.
    void copyRectToSurface(const void* buffer, int srcPitch, int destX, int destY, int width, int height);
    void copyRectToSurface(const Canvas& src, int destX, int destY, const Rect& subRect);
    // `src` (the same format) but for its pixels of `transColor`.
    void transBlitFrom(const Canvas& src, int destX, int destY, uint32_t transColor);

private:
    Format format_ = kClut8;
    std::vector<uint8_t> pixels_;
};

// A true colour pixel: 0xAARRGGBB.
constexpr uint32_t argb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
}
inline void argbToRgb(uint32_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = uint8_t(c >> 16), g = uint8_t(c >> 8), b = uint8_t(c);
}

} // namespace reluva::stgenerations
