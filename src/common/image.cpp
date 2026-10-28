// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/image.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <string>

// The stb_image implementation lives here. zip.cpp uses its inflate, which
// comes with PNG support.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

namespace reluva::image {

Error decode(const std::vector<uint8_t>& data, Image& out) {
    out = Image{};
    if (data.empty() || data.size() > static_cast<size_t>(INT_MAX))
        return Error::fail("Not an image");
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
    if (!pixels)
        return Error::fail(std::string("Cannot decode image: ") + stbi_failure_reason());
    out.w = w;
    out.h = h;
    out.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return Error::success();
}

std::vector<uint8_t> encodeTga(const Image& img) {
    std::vector<uint8_t> out(18, 0);
    out[2] = 2; // uncompressed true-colour
    out[12] = uint8_t(img.w & 0xff);
    out[13] = uint8_t(img.w >> 8);
    out[14] = uint8_t(img.h & 0xff);
    out[15] = uint8_t(img.h >> 8);
    out[16] = 24; // bits per pixel; descriptor 0: rows from the bottom
    out.reserve(out.size() + static_cast<size_t>(img.w) * img.h * 3);
    for (int y = img.h - 1; y >= 0; --y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* p = img.pixel(x, y);
            out.push_back(p[2]);
            out.push_back(p[1]);
            out.push_back(p[0]);
        }
    return out;
}

void blit(Surface& dst, const Image& img, int x, int y) {
    if (dst.format != PixelFormat::RGBA32)
        return;
    for (int sy = 0; sy < img.h; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dst.h)
            continue;
        uint8_t* row = dst.row(dy);
        for (int sx = 0; sx < img.w; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dst.w)
                continue;
            const uint8_t* s = img.pixel(sx, sy);
            uint8_t* d = row + dx * 4;
            const int a = s[3];
            for (int c = 0; c < 3; ++c)
                d[c] = static_cast<uint8_t>((s[c] * a + d[c] * (255 - a)) / 255);
            d[3] = 255;
        }
    }
}

void blitOver(Surface& dst, const Image& img, int x, int y) {
    if (dst.format != PixelFormat::RGBA32)
        return;
    for (int sy = std::max(0, -y); sy < img.h && y + sy < dst.h; sy++) {
        uint8_t* row = dst.row(y + sy);
        for (int sx = std::max(0, -x); sx < img.w && x + sx < dst.w; sx++) {
            const uint8_t* s = img.pixel(sx, sy);
            uint8_t* d = row + (x + sx) * 4;
            const int a = s[3], da = d[3] * (255 - a) / 255, out = a + da;
            if (out == 0)
                continue;
            for (int c = 0; c < 3; c++)
                d[c] = uint8_t((s[c] * a + d[c] * da) / out);
            d[3] = uint8_t(out);
        }
    }
}

} // namespace reluva::image
