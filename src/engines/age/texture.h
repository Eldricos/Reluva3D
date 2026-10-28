// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/image.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// The pixel formats .tex files use. See docs/formats/age/tex.md.
namespace texformat {
constexpr uint16_t Palette = 1;       // 8-bit indices, 256 BGRA colours, opaque
constexpr uint16_t PaletteAlpha = 14; // the same, the colours' alpha used
constexpr uint16_t Rgb = 17;          // 24-bit: R, G, B
constexpr uint16_t Rgba = 18;         // 32-bit: R, G, B, A
} // namespace texformat

// A .tex file's header and its first (largest) level, decoded to RGBA.
// A texture's flags, as far as they're known (tex.md).
namespace texflag {
constexpr uint32_t Clamp = 0x1; // its coordinates kept within it, not repeated
}

struct Tex {
    uint16_t format = 0;
    uint16_t levels = 0;  // mipmap levels stored, each half the last
    uint32_t flags = 0;   // see texflag; the other bits' meanings aren't known
    image::Image image;
};

Error decodeTex(const std::vector<uint8_t>& data, Tex& out);

// A shader's texture by name, as models name them ("vpbugyellow_sd"):
// texture/<name>.tex, else the first frame of an animated texture
// (texture/<name>-0001.tex, played as tune/<name>.movie says), else
// texture/<name>.tga. `format` is 0 for a .tga.
Error loadTexture(const SearchSet& files, std::string_view name, Tex& out);

// The name of a format, for the viewer ("8-bit palette"...).
const char* formatName(uint16_t format);

} // namespace reluva::age
