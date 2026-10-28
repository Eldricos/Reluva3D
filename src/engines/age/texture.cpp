// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Layout worked out from Midtown Madness 2's textures; see
// docs/formats/age/tex.md.

#include "engines/age/texture.h"

#include <algorithm>

namespace reluva::age {

namespace {

constexpr size_t kHeaderSize = 14;
constexpr size_t kPaletteSize = 256 * 4;

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

} // namespace

const char* formatName(uint16_t format) {
    switch (format) {
    case texformat::Palette: return "8-bit palette";
    case texformat::PaletteAlpha: return "8-bit palette with alpha";
    case texformat::Bgr: return "24-bit";
    case texformat::Bgra: return "32-bit with alpha";
    default: return "unknown";
    }
}

Error decodeTex(const std::vector<uint8_t>& data, Tex& out) {
    if (data.size() < kHeaderSize)
        return Error::fail("too small for a .tex header");
    const uint8_t* h = data.data();
    const int w = le16(h), height = le16(h + 2);
    out.format = le16(h + 4);
    out.levels = le16(h + 6);
    out.flags = le32(h + 10);
    if (w == 0 || height == 0 || out.levels == 0)
        return Error::fail("empty texture");

    size_t bytesPerPixel = 0, palette = 0;
    switch (out.format) {
    case texformat::Palette:
    case texformat::PaletteAlpha: bytesPerPixel = 1, palette = kPaletteSize; break;
    case texformat::Bgr: bytesPerPixel = 3; break;
    case texformat::Bgra: bytesPerPixel = 4; break;
    default: return Error::fail("unknown texture format " + std::to_string(out.format));
    }
    // Every level is checked to fit; only the first is decoded.
    size_t need = kHeaderSize + palette;
    for (int i = 0; i < out.levels; ++i)
        need += size_t(std::max(w >> i, 1)) * size_t(std::max(height >> i, 1)) * bytesPerPixel;
    if (data.size() < need)
        return Error::fail("texture data runs past the end of the file");

    image::Image& img = out.image;
    img.w = w;
    img.h = height;
    img.rgba.resize(size_t(w) * size_t(height) * 4);
    const uint8_t* pal = h + kHeaderSize;
    const uint8_t* src = pal + palette;
    uint8_t* dst = img.rgba.data();
    for (size_t i = 0, n = size_t(w) * size_t(height); i < n; ++i, dst += 4) {
        const uint8_t* p = palette ? pal + 4 * src[i] : src + i * bytesPerPixel;
        dst[0] = p[2];
        dst[1] = p[1];
        dst[2] = p[0];
        dst[3] = out.format == texformat::PaletteAlpha || out.format == texformat::Bgra ? p[3] : 255;
    }
    return Error::success();
}

Error loadTexture(const SearchSet& files, std::string_view name, Tex& out) {
    const std::string base = "texture/" + std::string(name);
    std::vector<uint8_t> data;
    if (!files.read(base + ".tex", data) || !files.read(base + "-0001.tex", data)) {
        if (Error err = decodeTex(data, out))
            return Error::fail(base + ".tex: " + err.message);
        return Error::success();
    }
    if (!files.read(base + ".tga", data)) {
        out = Tex{};
        out.levels = 1;
        if (Error err = image::decode(data, out.image))
            return Error::fail(base + ".tga: " + err.message);
        return Error::success();
    }
    return Error::fail("no texture " + std::string(name));
}

} // namespace reluva::age
