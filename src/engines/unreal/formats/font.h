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

struct FontCharacter {
    /** on its page's texture, in texels */
    int32_t u = 0;
    int32_t v = 0;
    int32_t width = 0;
    int32_t height = 0;
};

struct FontPage {
    ObjectRef texture;
    std::vector<FontCharacter> characters;
};

struct Font {
    std::vector<FontPage> pages;
    int charactersPerPage = 256;
    /** A character's page and rectangle; none for one past the pages. */
    const FontCharacter *character(uint32_t c, const FontPage **page = nullptr) const;
};

Error readFont(const PackageSet &packages, const Package &package, int exportIndex, Font &out);

} // namespace reluva::unreal
