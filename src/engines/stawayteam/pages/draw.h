// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/system.h"
#include "image/image.h"

#include <string>

namespace reluva::stawayteam {

/** The two rows of status at the bottom of every page. */
constexpr int kStatusHeight = 26;

/** Fills a rectangle of an RGBA32 surface, clipped to it. */
void fill(Surface &s, int x, int y, int w, int h, Color c);
/** The page's background, all but the status line. */
void clearPage(Surface &s);
/** The status line: two rows of text at the bottom. */
void drawStatus(Surface &s, const std::string &first, const std::string &second);
/**
 * What a picture is drawn on, behind it only, so its edges show: 0 the
 * page's own colour, 1 a checkerboard, 2 magenta.
 */
void drawBackdrop(Surface &s, int x, int y, int w, int h, int backdrop);
/** The picture `zoom` times larger, each pixel a square. */
image::Image enlarged(const image::Image &img, int zoom);
/** The folder a file of DATA.dat is in ("" at the top). */
std::string folderOf(const std::string &name);

} // namespace reluva::stawayteam
