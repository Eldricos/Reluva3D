// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>

namespace reluva {

/** A pixel's position. */
struct Point {
    int x = 0;
    int y = 0;
    /** (0, 0), or the position given. */
    ///@{
    constexpr Point() = default;
    constexpr Point(int px, int py) : x(px), y(py) {}
    ///@}
    /** Equal when x and y are. */
    bool operator==(const Point &) const = default;
};

/** A rectangle of pixels: left and top inside, right and bottom outside. */
struct Rect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    /** All zeros, or the edges given (left, top, right, bottom). */
    ///@{
    constexpr Rect() = default;
    constexpr Rect(int l, int t, int r, int b) : left(l), top(t), right(r), bottom(b) {}
    ///@}
    /** The size in pixels (negative if the edges are crossed). */
    ///@{
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    ///@}
    /** Whether the pixel is inside. */
    ///@{
    bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
    bool contains(Point p) const { return contains(p.x, p.y); }
    ///@}
    /** Whether it holds no pixels. */
    bool isEmpty() const { return left >= right || top >= bottom; }
    /** Whether it shares a pixel with `r`. */
    bool intersects(const Rect &r) const {
        return left < r.right && r.left < right && top < r.bottom && r.top < bottom;
    }
    /** Keeps only what's inside `r` too; all zeros when nothing is. */
    void clip(const Rect &r) {
        left = left > r.left ? left : r.left;
        top = top > r.top ? top : r.top;
        right = right < r.right ? right : r.right;
        bottom = bottom < r.bottom ? bottom : r.bottom;
        if (isEmpty())
            left = top = right = bottom = 0;
    }
    /** Moves it by (dx, dy). */
    void translate(int dx, int dy) {
        left += dx;
        right += dx;
        top += dy;
        bottom += dy;
    }
    /** Equal when every edge is. */
    bool operator==(const Rect &) const = default;
};

/**
 * Drawing on 8-bit (CLUT8) surfaces, for games that draw in palette
 * indices. Everything is clipped to the surface.
 */
namespace draw {

/** Makes `s` a width x height CLUT8 surface, every pixel `color`. */
void create(Surface &s, int width, int height, uint8_t color = 0);
/** The pixel at (x, y) (unchecked). */
///@{
inline uint8_t *at(Surface &s, int x, int y) {
    return s.row(y) + x;
}
inline const uint8_t *at(const Surface &s, int x, int y) {
    return s.row(y) + x;
}
///@}
/** Whether the surface has no pixels. */
inline bool empty(const Surface &s) {
    return s.pixels.empty();
}

/** Every pixel `color`. */
void fill(Surface &s, uint8_t color);
/** A filled rectangle, w x h with its top left at (x, y). */
void fillRect(Surface &s, int x, int y, int w, int h, uint8_t color);
/** A one-pixel outline of the same rectangle. */
void frameRect(Surface &s, int x, int y, int w, int h, uint8_t color);
/**
 * `src` copied with its top left at (x, y); pixels of the `transparent`
 * index (0..255) are left out, none if it's -1.
 */
void blit(Surface &dst, const Surface &src, int x, int y, int transparent = -1);

} // namespace draw

} // namespace reluva
