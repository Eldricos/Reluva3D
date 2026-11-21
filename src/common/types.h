// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Inlined whatever the compiler would choose: for the few small functions
// run for nearly every interpreted instruction or drawn pixel, where a
// call costs more than the work (Visual Studio declines many such).
#if defined(_MSC_VER)
#define RELUVA_FORCE_INLINE __forceinline
#else
#define RELUVA_FORCE_INLINE [[gnu::always_inline]] inline
#endif

namespace reluva {

/**
 * The result of an operation that can fail, with a reason a person can
 * read. Tested as a bool, it is true when there is an error:
 *
 *     if (Error e = load(path))
 *         return e;
 */
struct Error {
    bool ok = true;
    std::string message;

    /** A result that worked, and one that failed for `msg`. */
    ///@{
    static Error success() { return {}; }
    static Error fail(std::string msg) { return {false, std::move(msg)}; }
    ///@}
    /** True when there is an error, so `if (err)` reads as "if it failed". */
    explicit operator bool() const { return !ok; }
};

/** A palette entry or a pixel's colour, without alpha. */
struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

/** How a Surface's pixels are stored. */
enum class PixelFormat {
    kCLUT8,  // 8-bit palette indices, colours set with System::setPalette
    kRGBA32, // 4 bytes per pixel in memory order R, G, B, A
};

/** The bytes one pixel of `f` takes in a Surface's rows. */
inline int bytesPerPixel(PixelFormat f) {
    return f == PixelFormat::kCLUT8 ? 1 : 4;
}

/** A pixel buffer that owns its pixels: a game's framebuffer, among others. */
struct Surface {
    int w = 0;
    int h = 0;
    int pitch = 0; // bytes from one row to the next
    PixelFormat format = PixelFormat::kCLUT8;
    std::vector<uint8_t> pixels;

    /** Sizes it and clears every pixel to 0. */
    void create(int width, int height, PixelFormat fmt) {
        w = width;
        h = height;
        format = fmt;
        pitch = width * bytesPerPixel(fmt);
        pixels.assign(static_cast<size_t>(pitch) * height, 0);
    }

    /** The first byte of row `y` (0 at the top); not checked against h. */
    ///@{
    uint8_t *row(int y) { return pixels.data() + static_cast<size_t>(y) * pitch; }
    const uint8_t *row(int y) const { return pixels.data() + static_cast<size_t>(y) * pitch; }
    ///@}
};

} // namespace reluva
