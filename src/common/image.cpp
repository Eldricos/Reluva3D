// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/image.h"

#include "common/zip.h"

#include <algorithm>
#include <climits>
#include <cstddef>
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

std::vector<uint8_t> encodeTga(const Image& img, bool alpha) {
    std::vector<uint8_t> out(18, 0);
    out[2] = 2; // uncompressed true-colour
    out[12] = uint8_t(img.w & 0xff);
    out[13] = uint8_t(img.w >> 8);
    out[14] = uint8_t(img.h & 0xff);
    out[15] = uint8_t(img.h >> 8);
    out[16] = alpha ? 32 : 24; // bits per pixel
    out[17] = alpha ? 8 : 0;   // descriptor: alpha bits; rows from the bottom
    out.reserve(out.size() + static_cast<size_t>(img.w) * img.h * (alpha ? 4 : 3));
    for (int y = img.h - 1; y >= 0; --y)
        for (int x = 0; x < img.w; ++x) {
            const uint8_t* p = img.pixel(x, y);
            out.push_back(p[2]);
            out.push_back(p[1]);
            out.push_back(p[0]);
            if (alpha)
                out.push_back(p[3]);
        }
    return out;
}

std::vector<uint8_t> encodePng(const Image& img) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    auto be32 = [](std::vector<uint8_t>& v, uint32_t x) {
        for (int i = 3; i >= 0; --i)
            v.push_back(uint8_t(x >> (8 * i)));
    };
    auto chunk = [&](const char* tag, const std::vector<uint8_t>& data) {
        be32(out, uint32_t(data.size()));
        std::vector<uint8_t> body(tag, tag + 4);
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        be32(out, crc32(body.data(), body.size()));
    };
    std::vector<uint8_t> header;
    be32(header, uint32_t(img.w));
    be32(header, uint32_t(img.h));
    header.insert(header.end(), {8, 6, 0, 0, 0}); // 8 bits, RGBA, deflate, no filter choice, not interlaced
    chunk("IHDR", header);

    // The rows, each after its filter byte (0: none), in zlib's wrapping
    // with stored deflate blocks of up to 65535 bytes.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(img.h) * (size_t(img.w) * 4 + 1));
    for (int y = 0; y < img.h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), img.pixel(0, y), img.pixel(0, y) + size_t(img.w) * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(raw.size() - pos, 65535);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + std::ptrdiff_t(pos), raw.begin() + std::ptrdiff_t(pos + n));
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0; // Adler-32
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}

void blit(Surface& dst, const Image& img, int x, int y, int opacity) {
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
            const int a = opacity == 255 ? s[3] : s[3] * opacity / 255;
            if (a == 0)
                continue;
            uint8_t* d = row + dx * 4;
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
