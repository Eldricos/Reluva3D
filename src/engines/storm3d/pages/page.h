// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace reluva::image {
struct Image;
} // namespace reluva::image

namespace reluva::storm3d {

/** The data viewer's screen, in pixels. */
///@{
constexpr int kViewerWidth = 800;
constexpr int kViewerHeight = 600;
///@}

/**
 * One of the data viewer's pages: it draws into the viewer's RGBA32 screen
 * and says where it is in the window's title.
 */
class Page {
public:
    virtual ~Page() = default;
    /** The page's name, for the title: "Images". */
    virtual const char *name() const = 0;
    /** How many items it has; the viewer skips a page with none. */
    virtual size_t count() const = 0;
    /** Draws the page as it's shown. */
    virtual void activate() = 0;
    /** Called as another page is shown: stops what's playing. */
    virtual void deactivate() {}
    /** A key pressed on the page; it draws again if it needs to. */
    virtual void handleKey(uint32_t key) = 0;
    /**
     * Called each frame while the page is shown, for what moves.
     * @param millis the system's clock
     */
    virtual void update(uint64_t millis) { (void)millis; }
};

/** Fills a rectangle of an RGBA32 surface, clipped to it. */
void fillRect(Surface &s, int x, int y, int w, int h, Color c);

/** A line of text at the screen's foot, on a dark band. */
void drawStatus(Surface &s, std::string_view text);

/**
 * An image drawn as large as fits, up to 4 times its size, centred in the
 * screen's top `areaHeight` rows (over what's there where it doesn't
 * reach); over a checkerboard where it's see-through when `alpha` is set,
 * else opaque.
 */
void drawFitted(Surface &screen, const image::Image &img, bool alpha, int areaHeight);

/** `index` moved by `step` within [0, count), stopping at the ends. */
size_t stepIndex(size_t index, int step, size_t count);

} // namespace reluva::storm3d
