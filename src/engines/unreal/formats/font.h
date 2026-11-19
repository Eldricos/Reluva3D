// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// UE1 fonts (class Font): pages of characters, each page a texture and
// each character a rectangle on it. See docs/formats/unreal/font.md.

#include "common/types.h"
#include "engines/unreal/formats/package_set.h"

#include <cstdint>
#include <vector>

namespace reluva::unreal {

/** A character's rectangle on its page's texture, in texels. */
struct FontCharacter {
    int32_t u = 0;
    int32_t v = 0;
    int32_t width = 0;
    int32_t height = 0;
};

/** A page of characters, all on one texture. */
struct FontPage {
    ObjectRef texture;
    std::vector<FontCharacter> characters;
};

/** A Font object: character `c` is on page c / charactersPerPage. */
struct Font {
    std::vector<FontPage> pages;
    int charactersPerPage = 256;
    /**
     * A character's rectangle, and its page in `page` if given; nullptr for
     * one past the pages or past its page's characters.
     */
    const FontCharacter *character(uint32_t c, const FontPage **page = nullptr) const;
};

/**
 * Reads a Font object, its pages' textures resolved through `packages`.
 * Fails on more than 256 pages, or data that runs past the end.
 */
Error readFont(const PackageSet &packages, const Package &package, int exportIndex, Font &out);

} // namespace reluva::unreal
