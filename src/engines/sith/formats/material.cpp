// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/material.h"

#include "common/bytes.h"

#include <cstring>
#include <string>

namespace reluva::sith {

namespace {

constexpr uint32_t kVersion = 0x32;
constexpr size_t kHeaderSize = 76;
constexpr size_t kColorCelSize = 24;
constexpr size_t kTextureCelSize = 40;
constexpr size_t kTextureHeaderSize = 24;
constexpr uint32_t kTextureCel = 8;

} // namespace

Error parseMaterial(const std::vector<uint8_t> &data, Material &out) {
    out = Material{};
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "MAT ", 4) != 0)
        return Error::fail("not a material: no MAT header");
    if (readLE32(&data[4]) != kVersion)
        return Error::fail("material version " + std::to_string(readLE32(&data[4])) + ", not 50");
    out.type = readLE32(&data[8]);
    const uint32_t celCount = readLE32(&data[12]);
    const uint32_t textureCount = readLE32(&data[16]);
    if (readLE32(&data[24]) != 8)
        return Error::fail("material with " + std::to_string(readLE32(&data[24])) + " bits a pixel");

    size_t pos = kHeaderSize;
    for (uint32_t i = 0; i < celCount; ++i) {
        if (pos + kColorCelSize > data.size())
            return Error::fail("material cut short in cel " + std::to_string(i));
        Material::Cel cel;
        cel.kind = readLE32(&data[pos]);
        cel.color = uint8_t(readLE32(&data[pos + 4]));
        if (cel.kind == kTextureCel) {
            if (pos + kTextureCelSize > data.size())
                return Error::fail("material cut short in cel " + std::to_string(i));
            const uint32_t texture = readLE32(&data[pos + 36]);
            if (texture >= textureCount)
                return Error::fail("material cel " + std::to_string(i) + " names texture " + std::to_string(texture) +
                                   " of " + std::to_string(textureCount));
            cel.texture = int(texture);
            pos += kTextureCelSize;
        } else
            pos += kColorCelSize;
        out.cels.push_back(cel);
    }
    for (uint32_t t = 0; t < textureCount; ++t) {
        if (pos + kTextureHeaderSize > data.size())
            return Error::fail("material cut short in texture " + std::to_string(t));
        Material::Texture tex;
        tex.w = int(readLE32(&data[pos]));
        tex.h = int(readLE32(&data[pos + 4]));
        tex.transparent = (readLE32(&data[pos + 8]) & 1) != 0;
        tex.transparentColor = uint8_t(readLE32(&data[pos + 16]));
        const uint32_t mipCount = readLE32(&data[pos + 20]);
        pos += kTextureHeaderSize;
        if (tex.w <= 0 || tex.h <= 0 || mipCount == 0 || mipCount > 16)
            return Error::fail("material texture " + std::to_string(t) + " has a bad size");
        for (uint32_t m = 0; m < mipCount; ++m) {
            const size_t bytes = size_t(tex.w >> m) * size_t(tex.h >> m);
            if (bytes > data.size() - pos)
                return Error::fail("material texture " + std::to_string(t) + " runs past the end");
            tex.mips.emplace_back(data.begin() + std::ptrdiff_t(pos), data.begin() + std::ptrdiff_t(pos + bytes));
            pos += bytes;
        }
        out.textures.push_back(std::move(tex));
    }
    return Error::success();
}

image::Image materialToImage(const Material::Texture &tex, int level, const Palette &palette, bool transparent) {
    image::Image img;
    img.w = tex.w >> level;
    img.h = tex.h >> level;
    img.rgba.resize(size_t(img.w) * img.h * 4);
    const std::vector<uint8_t> &pixels = tex.mips[size_t(level)];
    for (size_t i = 0; i < pixels.size(); ++i) {
        const Color c = palette[pixels[i]];
        uint8_t *p = &img.rgba[i * 4];
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = transparent && tex.transparent && pixels[i] == tex.transparentColor ? 0 : 255;
    }
    return img;
}

} // namespace reluva::sith
