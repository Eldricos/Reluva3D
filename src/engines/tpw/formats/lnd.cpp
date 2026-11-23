// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The layout is SimCoaster's four worlds' grounds', worked out from them:
// docs/engines/tpw/games/simcoaster/formats/lnd.md.

#include "engines/tpw/formats/lnd.h"

#include <string>

namespace reluva::tpw {

namespace {

constexpr uint8_t kVersion = 3;
constexpr size_t kHeaderSize = 0x17;
constexpr int kLevels = 3;
// In each level, in all four worlds' files.
constexpr int kTextures = 6;
constexpr int kMasks = 7;
constexpr int kMaxSide = 4096;

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : _d(data) {}

    bool has(size_t size) const { return size <= _d.size() - _at; }
    uint8_t u8() { return _d[_at++]; }
    uint32_t u32() {
        const uint32_t v =
            uint32_t(_d[_at]) | uint32_t(_d[_at + 1]) << 8 | uint32_t(_d[_at + 2]) << 16 | uint32_t(_d[_at + 3]) << 24;
        _at += 4;
        return v;
    }
    const uint8_t *take(size_t size) {
        const uint8_t *p = _d.data() + _at;
        _at += size;
        return p;
    }

private:
    const std::vector<uint8_t> &_d;
    size_t _at = 0;
};

// A picture's width, height and bytes a pixel, then its pixels.
Error readPicture(Reader &r, uint32_t bytesPerPixel, int &width, int &height, const uint8_t *&pixels) {
    if (!r.has(12))
        return Error::fail("the ground is cut short");
    width = int(r.u32());
    height = int(r.u32());
    const uint32_t bpp = r.u32();
    if (bpp != bytesPerPixel || width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide)
        return Error::fail("a picture of the ground isn't of its kind");
    const size_t size = size_t(width) * size_t(height) * bpp;
    if (!r.has(size))
        return Error::fail("the ground is cut short");
    pixels = r.take(size);
    return Error::success();
}

} // namespace

Error Land::parse(const std::vector<uint8_t> &data) {
    *this = {};
    if (data.size() < kHeaderSize || data[0] != kVersion)
        return Error::fail("not a park's ground (.lnd) of version 3");
    Reader r(data);
    r.u8();
    for (uint8_t &b : _settings)
        b = r.u8();
    int width = 0;
    int depth = 0;
    const uint8_t *cells = nullptr;
    if (Error err = readPicture(r, 4, width, depth, cells))
        return err;
    _width = width;
    _depth = depth;
    _cells.assign(cells, cells + size_t(width) * size_t(depth) * 4);
    for (int l = 0; l < kLevels; ++l) {
        if (!r.has(5))
            return Error::fail("the ground is cut short");
        Level level;
        level.scale = r.u32();
        if (r.u8() != l)
            return Error::fail("the ground's sizes are out of order");
        for (int t = 0; t < kTextures; ++t) {
            int w = 0;
            int h = 0;
            const uint8_t *p = nullptr;
            if (Error err = readPicture(r, 4, w, h, p))
                return err;
            // A, R, G, B; A is 0 in all of them, so read as opaque.
            image::Image img;
            img.w = w;
            img.h = h;
            img.rgba.resize(size_t(w) * size_t(h) * 4);
            for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
                img.rgba[i * 4] = p[i * 4 + 1];
                img.rgba[i * 4 + 1] = p[i * 4 + 2];
                img.rgba[i * 4 + 2] = p[i * 4 + 3];
                img.rgba[i * 4 + 3] = 255;
            }
            level.textures.push_back(std::move(img));
        }
        for (int m = 0; m < kMasks; ++m) {
            Mask mask;
            const uint8_t *p = nullptr;
            if (Error err = readPicture(r, 1, mask.width, mask.height, p))
                return err;
            mask.pixels.assign(p, p + size_t(mask.width) * size_t(mask.height));
            level.masks.push_back(std::move(mask));
        }
        _levels.push_back(std::move(level));
    }
    return Error::success();
}

} // namespace reluva::tpw
