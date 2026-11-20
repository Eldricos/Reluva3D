// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/draw.h"

#include <cstdint>
#include <vector>

namespace reluva::stgenerations {

/**
 * A picture the game draws on: 8-bit palette indices, or true colour as
 * 32-bit 0xAARRGGBB values (the bridge, space combat and the movies, which
 * the original showed on a 16-bit screen). Drawing is clipped to it;
 * getPixel() and getBasePtr() aren't, and need a point inside it.
 */
class Canvas {
public:
    /** How its pixels are stored. */
    enum Format { kClut8, kArgb32 };

    int w = 0;
    int h = 0;
    int pitch = 0;

    Canvas() = default;
    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;
    /** Takes the other's pixels, leaving it empty. */
    ///@{
    Canvas(Canvas &&other) noexcept { *this = std::move(other); }
    Canvas &operator=(Canvas &&other) noexcept;
    ///@}

    /** Makes it width x height pixels, all 0; empty if either is 0 or less. */
    void create(int width, int height, Format format);
    /** Empties it, freeing its pixels. */
    void free();
    /** Whether it has no pixels. */
    bool empty() const { return _pixels.empty(); }

    /** How its pixels are stored: the format, and 1 or 4 bytes a pixel. */
    ///@{
    Format format() const { return _format; }
    bool isClut8() const { return _format == kClut8; }
    int bytesPerPixel() const { return _format == kClut8 ? 1 : 4; }
    ///@}

    /** The pixels, rows `pitch` bytes apart, and the address of the one at (x, y). */
    ///@{
    void *getPixels() { return _pixels.data(); }
    const void *getPixels() const { return _pixels.data(); }
    void *getBasePtr(int x, int y) { return _pixels.data() + size_t(y) * size_t(pitch) + size_t(x) * bytesPerPixel(); }
    const void *getBasePtr(int x, int y) const {
        return _pixels.data() + size_t(y) * size_t(pitch) + size_t(x) * bytesPerPixel();
    }
    ///@}

    /** A pixel: an index, or 0xAARRGGBB; (x, y) must be inside. */
    uint32_t getPixel(int x, int y) const;
    /**
     * Drawing in one colour, an index or 0xAARRGGBB as the format is. Lines
     * include both ends; rectangles are filled or outlined.
     */
    ///@{
    void setPixel(int x, int y, uint32_t color);
    void fillRect(Rect r, uint32_t color);
    void frameRect(const Rect &r, uint32_t color);
    void hLine(int x1, int y, int x2, uint32_t color);
    void vLine(int x, int y1, int y2, uint32_t color);
    void drawLine(int x0, int y0, int x1, int y1, uint32_t color);
    ///@}
    /** A filled polygon, sampled at pixel centres (each edge with its top end only). */
    void fillPolygon(const Point *points, int count, uint32_t color);
    /** Copies pixels in this canvas's format, `srcPitch` bytes apart, to (destX, destY). */
    void copyRectToSurface(const void *buffer, int srcPitch, int destX, int destY, int width, int height);
    /** Copies part of `src`, in the same format, to (destX, destY). */
    void copyRectToSurface(const Canvas &src, int destX, int destY, const Rect &subRect);
    /** Copies `src` (the same format) to (destX, destY), but for its pixels of `transColor`. */
    void transBlitFrom(const Canvas &src, int destX, int destY, uint32_t transColor);

private:
    Format _format = kClut8;
    std::vector<uint8_t> _pixels;
};

/** A true colour pixel: 0xAARRGGBB. */
constexpr uint32_t argb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 0xFF) {
    return uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
}
/** A true colour pixel's red, green and blue. */
inline void argbToRgb(uint32_t c, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = uint8_t(c >> 16), g = uint8_t(c >> 8), b = uint8_t(c);
}

} // namespace reluva::stgenerations
