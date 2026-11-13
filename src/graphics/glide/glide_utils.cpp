// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/glide/glide_utils.h"

#include "graphics/glide/glide_defs.h"
#include "graphics/glide/textures.h"

#include <cstdio>
#include <cstring>

namespace reluva::glide {

namespace {

// TexUS's colour format names (the guide's table) and Glide's formats.
// argb8888 is in the table "for archiving purposes only" ("The Voodoo
// Graphics hardware can not use this color format"): not read.
struct FormatName {
    const char* name;
    int format;
};
constexpr FormatName kFormats[] = {
    {"i8", kTexIntensity8},     {"a8", kTexAlpha8},          {"ai44", kTexAlphaIntensity44}, {"yiq", kTexYiq422},
    {"rgb332", kTexRgb332},     {"rgb565", kTexRgb565},      {"argb8332", kTexArgb8332},     {"argb1555", kTexArgb1555},
    {"ayiq8422", kTexAyiq8422}, {"argb4444", kTexArgb4444}, {"ai88", kTexAlphaIntensity88},
};

int log2Of(int v) {
    int l = 0;
    while ((1 << l) < v)
        ++l;
    return (1 << l) == v ? l : -1;
}

bool fail(std::string* error, const char* why) {
    if (error)
        *error = why;
    return false;
}

} // namespace

bool parse3dfHeader(const uint8_t* file, size_t size, File3df& out, std::string* error) {
    size_t at = 0;
    auto line = [&](std::string& s) {
        s.clear();
        while (at < size && file[at] != '\n')
            s += char(file[at++]);
        if (at >= size)
            return false;
        ++at;
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
        return true;
    };
    std::string s;
    int major = 0, minor = 0;
    if (!line(s) || std::sscanf(s.c_str(), "3df v%d.%d", &major, &minor) != 2)
        return fail(error, "no \"3df v<major>.<minor>\" magic cookie");
    do {
        if (!line(s))
            return fail(error, "the header ends early");
    } while (!s.empty() && s[0] == '#');
    char name[32] = {};
    if (std::sscanf(s.c_str(), "color format: %31s", name) != 1)
        return fail(error, "no colour format line");
    out.format = -1;
    for (const FormatName& f : kFormats)
        if (std::strcmp(f.name, name) == 0)
            out.format = f.format;
    if (out.format < 0)
        return fail(error, "a colour format Glide can't use");
    if (out.format == kTexYiq422 || out.format == kTexAyiq8422)
        return fail(error, "an NCC format: the guide doesn't say how the file holds its table");
    // "lod range: %d %d", the minimum and maximum: Reluva reads them as
    // the smallest and largest levels' sizes in texels (1-256).
    int lo = 0, hi = 0;
    if (!line(s) || std::sscanf(s.c_str(), "lod range: %d %d", &lo, &hi) != 2)
        return fail(error, "no LOD range line");
    if (lo > hi)
        std::swap(lo, hi);
    out.smallLodLog2 = log2Of(lo);
    out.largeLodLog2 = log2Of(hi);
    if (out.smallLodLog2 < 0 || out.largeLodLog2 < 0 || out.largeLodLog2 > 8)
        return fail(error, "a LOD range that isn't powers of two up to 256");
    // "aspect ratio: %d %d": width to height.
    int aw = 0, ah = 0;
    if (!line(s) || std::sscanf(s.c_str(), "aspect ratio: %d %d", &aw, &ah) != 2 || aw <= 0 || ah <= 0)
        return fail(error, "no aspect ratio line");
    const int lw = log2Of(aw), lh = log2Of(ah);
    if (lw < 0 || lh < 0 || lw - lh > 3 || lh - lw > 3)
        return fail(error, "an aspect ratio Glide doesn't have");
    out.aspectLog2 = lw - lh;
    levelSize(out.largeLodLog2, out.aspectLog2, out.width, out.height);
    out.dataBytes = 0;
    for (int l = out.largeLodLog2; l >= out.smallLodLog2; --l) {
        int w = 0, h = 0;
        levelSize(l, out.aspectLog2, w, h);
        out.dataBytes += uint32_t(w * h * texelBytes(out.format));
    }
    out.dataOffset = at;
    return true;
}

bool load3dfData(const uint8_t* file, size_t size, const File3df& info, uint8_t* out) {
    if (info.dataOffset > size || size - info.dataOffset < info.dataBytes)
        return false;
    const uint8_t* src = file + info.dataOffset;
    if (texelBytes(info.format) == 1) {
        std::memcpy(out, src, info.dataBytes);
        return true;
    }
    for (uint32_t i = 0; i + 1 < info.dataBytes; i += 2) {
        out[i] = src[i + 1];
        out[i + 1] = src[i];
    }
    return true;
}

uint32_t encodeRle16(uint8_t* dst, const uint16_t* src, uint32_t width, uint32_t height) {
    uint32_t bytes = 0;
    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* row = src + size_t(y) * width;
        for (uint32_t x = 0; x < width;) {
            uint32_t n = 1;
            while (x + n < width && row[x + n] == row[x] && n < 0x7fff)
                ++n;
            if (dst) {
                const uint16_t words[2] = {uint16_t(n), row[x]};
                std::memcpy(dst + bytes, words, 4);
            }
            bytes += 4;
            x += n;
        }
    }
    return bytes;
}

} // namespace reluva::glide
