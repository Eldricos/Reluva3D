// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "image/image.h"
#include "engines/botf/formats/font.h"
#include "engines/botf/formats/menu_file.h"

#include <map>
#include <string>

// What the screens draw with, loaded from an archive (stbof.res) when first
// asked for and kept: pictures (with the colour key applied), fonts and
// menus. A missing or unreadable file gives null, and why() says what went
// wrong.

namespace reluva::botf {

/** The pictures, fonts and menus of an archive, each loaded once, when first asked for. */
class Assets {
public:
    explicit Assets(const Archive &archive) : _archive(archive) {}
    const Archive &archive() const { return _archive; }

    const image::Image *picture(const std::string &name);
    const Font *font(const std::string &name);
    const MenuFile *menu(const std::string &name);
    /** Why the file couldn't be had ("missing", "can't decode" ...), or empty. */
    std::string why(const std::string &name) const;

private:
    const Archive &_archive;
    std::map<std::string, image::Image> _pictures;
    std::map<std::string, Font> _fonts;
    std::map<std::string, MenuFile> _menus;
    std::map<std::string, std::string> _failed;
};

/**
 * The interface pictures mark what isn't drawn with magenta (red and blue
 * full, no green), the colour key MPR's blits skip: makes those pixels
 * transparent, and returns how many there were. The key is set by the
 * executable, not stored in the files; magenta is what the pictures show.
 */
size_t applyColorKey(image::Image &img);

/** The part of the image inside the clip rectangle, blended over the surface. */
void blitClipped(Surface &dst, const image::Image &img, int x, int y, int cx, int cy, int cw, int ch);

} // namespace reluva::botf
