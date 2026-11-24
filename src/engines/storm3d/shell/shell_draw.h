// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "graphics/truetype.h"
#include "image/image.h"

#include <array>
#include <string>
#include <string_view>

namespace reluva::storm3d {

/** One of the install's pictures (a .bmp of the front end's), decoded. */
Error loadPicture(const GameFiles &files, const std::string &name, image::Image &out);
/**
 * Copies RGBA pixels onto an RGBA screen with their top left at (x, y),
 * clipped to it; `keyed`: pixels of the front end's colour key, magenta
 * (255, 0, 255), left out, as the game masks its labels.
 */
void drawPixels(Surface &screen, const uint8_t *rgba, int w, int h, int x, int y, bool keyed);
/** drawPixels of a picture. */
inline void drawPicture(Surface &screen, const image::Image &picture, int x, int y, bool keyed) {
    drawPixels(screen, picture.rgba.data(), picture.w, picture.h, x, y, keyed);
}

/** A front end button drawn from three pictures: at rest, under the pointer, pressed. */
struct ShellButton {
    std::array<image::Image, 3> looks;
    int x = 0;
    int y = 0;
    /** Reads `<folder>/<name>1.bmp` to 3. */
    Error load(const GameFiles &files, const std::string &folder, const std::string &name, int atX, int atY);
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + looks[0].w && py < y + looks[0].h; }
    /** Draws the look for `lit` (under the pointer) and `pressed`. */
    void draw(Surface &screen, bool lit, bool pressed, bool keyed) const;
};

/**
 * The front end's text font, Arial Bold, as the game asks GDI for it
 * (`CreateFontIndirectA`): the machine's own Arial if it has one, else
 * Liberation Sans Bold, which keeps Arial's measures; Reluva's own small
 * font if there's neither.
 */
class ShellFont {
public:
    /** Looks for the font on the machine. */
    void load();
    /** How wide text is at `pixels` (an em). */
    int width(std::string_view utf8, float pixels);
    /** Draws text with the top of its line at (x, y). */
    void draw(Surface &screen, int x, int y, std::string_view utf8, float pixels, Color colour);
    /** Draws text centred in a box, as GDI's DT_CENTER | DT_VCENTER put it. */
    void drawCentred(Surface &screen, int x, int y, int w, int h, std::string_view utf8, float pixels, Color colour);

private:
    ttf::Font _font;
    bool _tried = false;
};

} // namespace reluva::storm3d
