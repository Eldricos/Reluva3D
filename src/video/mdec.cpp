// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/mdec.h"

#include "video/mpeg1.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace reluva::mdec {

namespace {

constexpr uint16_t kMagic = 0x3800;
constexpr uint16_t kVersion = 2;
constexpr size_t kHeaderSize = 8;

uint16_t le16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | p[1] << 8);
}

// cos((2x + 1) u pi / 16), with u = 0 scaled by 1/sqrt(2), halved: the
// inverse DCT's basis, so that out = sum over u of in[u] * basis[x][u].
const std::array<std::array<float, 8>, 8> &basis() {
    static const std::array<std::array<float, 8>, 8> kBasis = [] {
        std::array<std::array<float, 8>, 8> b{};
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                b[size_t(x)][size_t(u)] = (u == 0 ? 0.70710678f : 1.0f) * 0.5f *
                                          static_cast<float>(std::cos((2 * x + 1) * u * 3.14159265358979 / 16));
        return b;
    }();
    return kBasis;
}

} // namespace

uint32_t Decoder::peek(int n) const {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        const size_t at = _bitPos + static_cast<size_t>(i);
        // Past the end reads 0, which no code is all of.
        const uint32_t bit = at < _bitCount ? (le16(_bits + (at / 16) * 2) >> (15 - at % 16)) & 1 : 0;
        v = v << 1 | bit;
    }
    return v;
}

uint32_t Decoder::read(int n) {
    const uint32_t v = peek(n);
    _bitPos += static_cast<size_t>(n);
    return v;
}

int Decoder::readSigned(int n) {
    const uint32_t v = read(n);
    return v >> (n - 1) ? static_cast<int>(v) - (1 << n) : static_cast<int>(v);
}

bool Decoder::decodeBlock(float coefficients[64]) {
    std::fill(coefficients, coefficients + 64, 0.0f);
    // The DC term (10 bits) times the matrix's first entry, which MDEC
    // has as 2 where MPEG-1 has 8.
    coefficients[0] = static_cast<float>(readSigned(10) * 2);
    const auto &table = mpeg1::acTable();
    const auto &scan = mpeg1::zigzag();
    int i = 0;
    while (true) {
        if (_bitPos >= _bitCount)
            return false;
        const mpeg1::AcEntry e = table[peek(16)];
        if (e.length == 0)
            return false;
        _bitPos += e.length;
        if (e.run == 0xff)
            return true;
        int run = e.run;
        int level = e.level;
        if (level == 0) { // the escape: a 6-bit run, then a 10-bit signed level
            run = static_cast<int>(read(6));
            level = readSigned(10);
        } else if (read(1)) {
            level = -level;
        }
        i += run + 1;
        if (i > 63)
            return false;
        const int j = scan[size_t(i)];
        // Arithmetic shift, as the level may be negative.
        coefficients[j] = static_cast<float>((level * _scale * mpeg1::kIntraMatrix[j]) >> 3);
    }
}

void Decoder::putBlock(const float coefficients[64], int plane, int x0, int y0) {
    const auto &b = basis();
    float rows[64];
    // Columns, then rows: out[y][x] = sum over v, u of c[v][u] b[y][v] b[x][u].
    for (int v = 0; v < 8; ++v)
        for (int x = 0; x < 8; ++x) {
            float sum = 0;
            for (int u = 0; u < 8; ++u)
                sum += coefficients[v * 8 + u] * b[size_t(x)][size_t(u)];
            rows[v * 8 + x] = sum;
        }
    const int stride = plane ? _width / 2 : _width;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            float sum = 0;
            for (int v = 0; v < 8; ++v)
                sum += rows[v * 8 + x] * b[size_t(y)][size_t(v)];
            // Luma comes out about 0 for grey: +128 makes it 0-255.
            const int value = static_cast<int>(std::lround(sum)) + (plane ? 0 : 128);
            _planes[plane][size_t((y0 + y) * stride + x0 + x)] =
                static_cast<int16_t>(plane ? std::clamp(value, -128, 127) : std::clamp(value, 0, 255));
        }
}

Error Decoder::decode(const uint8_t *frame, size_t size, int width, int height) {
    if (size < kHeaderSize || le16(frame + 2) != kMagic)
        return Error::fail("Not an MDEC frame: no 0x3800 in its header");
    if (le16(frame + 6) != kVersion)
        return Error::fail("MDEC frames of version " + std::to_string(le16(frame + 6)) + " aren't read");
    if (width <= 0 || height <= 0 || width % 16 || height % 16)
        return Error::fail("An MDEC picture's size must be a multiple of 16 each way");
    _scale = le16(frame + 4);
    if (_width != width || _height != height) {
        _width = width;
        _height = height;
        _planes[0].assign(size_t(width) * size_t(height), 0);
        _planes[1].assign(size_t(width / 2) * size_t(height / 2), 0);
        _planes[2].assign(size_t(width / 2) * size_t(height / 2), 0);
    }
    _bits = frame + kHeaderSize;
    _bitCount = (size - kHeaderSize) / 2 * 16;
    _bitPos = 0;
    float c[64];
    // The macroblocks in columns, top to bottom, then left to right.
    for (int mx = 0; mx < width / 16; ++mx)
        for (int my = 0; my < height / 16; ++my) {
            // Cr, Cb, then the luma blocks top left, top right, bottom left
            // and bottom right.
            constexpr int kPlane[6] = {2, 1, 0, 0, 0, 0};
            constexpr int kX[6] = {0, 0, 0, 8, 0, 8};
            constexpr int kY[6] = {0, 0, 0, 0, 8, 8};
            for (int k = 0; k < 6; ++k) {
                if (!decodeBlock(c))
                    return Error::fail("MDEC frame damaged at bit " + std::to_string(_bitPos));
                const int scale = kPlane[k] ? 8 : 16;
                putBlock(c, kPlane[k], mx * scale + kX[k], my * scale + kY[k]);
            }
        }
    return Error::success();
}

void Decoder::toSurface(Surface &out) const {
    out.create(_width, _height, PixelFormat::kRGBA32);
    for (int y = 0; y < _height; ++y) {
        uint8_t *row = out.row(y);
        for (int x = 0; x < _width; ++x) {
            const float l = _planes[0][size_t(y * _width + x)];
            const size_t c = size_t((y / 2) * (_width / 2) + x / 2);
            const float cb = _planes[1][c];
            const float cr = _planes[2][c];
            // ITU-R BT.601's YCbCr to RGB, full range.
            const float rgb[3] = {l + 1.402f * cr, l - 0.344136f * cb - 0.714136f * cr, l + 1.772f * cb};
            for (int i = 0; i < 3; ++i)
                row[x * 4 + i] = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(rgb[i])), 0, 255));
            row[x * 4 + 3] = 255;
        }
    }
}

} // namespace reluva::mdec
