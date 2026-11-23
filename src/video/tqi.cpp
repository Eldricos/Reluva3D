// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// TQI is MPEG-1's intra coding (ISO/IEC 11172-2) with EA's own framing:
// docs/common/video/tqi.md says how it was worked out and checked.

#include "video/tqi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace reluva::tqi {

namespace {

constexpr size_t kHeader = 8;
constexpr int kMacroblock = 16;

// A code's place in a table looked up by the next 16 bits.
struct Code {
    uint8_t length = 0; // 0: no code starts so
    int8_t run = 0;     // kEnd and kEscape for those two codes
    int16_t level = 0;
};
constexpr int8_t kEnd = -1;
constexpr int8_t kEscape = -2;

// MPEG-1's AC codes (ISO/IEC 11172-2, table B.14), each followed by a sign
// bit: run of zeros, then level.
struct AcCode {
    std::string_view bits;
    int8_t run;
    int16_t level;
};
constexpr AcCode kAcCodes[] = {
    {"11", 0, 1},
    {"011", 1, 1},
    {"0100", 0, 2},
    {"0101", 2, 1},
    {"00101", 0, 3},
    {"00110", 4, 1},
    {"00111", 3, 1},
    {"000100", 7, 1},
    {"000101", 6, 1},
    {"000110", 1, 2},
    {"000111", 5, 1},
    {"0000100", 2, 2},
    {"0000101", 9, 1},
    {"0000110", 0, 4},
    {"0000111", 8, 1},
    {"00100000", 13, 1},
    {"00100001", 0, 6},
    {"00100010", 12, 1},
    {"00100011", 11, 1},
    {"00100100", 3, 2},
    {"00100101", 1, 3},
    {"00100110", 0, 5},
    {"00100111", 10, 1},
    {"0000001000", 16, 1},
    {"0000001001", 5, 2},
    {"0000001010", 0, 7},
    {"0000001011", 2, 3},
    {"0000001100", 1, 4},
    {"0000001101", 15, 1},
    {"0000001110", 14, 1},
    {"0000001111", 4, 2},
    {"000000010000", 0, 11},
    {"000000010001", 8, 2},
    {"000000010010", 4, 3},
    {"000000010011", 0, 10},
    {"000000010100", 2, 4},
    {"000000010101", 7, 2},
    {"000000010110", 21, 1},
    {"000000010111", 20, 1},
    {"000000011000", 0, 9},
    {"000000011001", 19, 1},
    {"000000011010", 18, 1},
    {"000000011011", 1, 5},
    {"000000011100", 3, 3},
    {"000000011101", 0, 8},
    {"000000011110", 6, 2},
    {"000000011111", 17, 1},
    {"0000000010000", 10, 2},
    {"0000000010001", 9, 2},
    {"0000000010010", 5, 3},
    {"0000000010011", 3, 4},
    {"0000000010100", 2, 5},
    {"0000000010101", 1, 7},
    {"0000000010110", 1, 6},
    {"0000000010111", 0, 15},
    {"0000000011000", 0, 14},
    {"0000000011001", 0, 13},
    {"0000000011010", 0, 12},
    {"0000000011011", 26, 1},
    {"0000000011100", 25, 1},
    {"0000000011101", 24, 1},
    {"0000000011110", 23, 1},
    {"0000000011111", 22, 1},
    {"00000000010000", 0, 31},
    {"00000000010001", 0, 30},
    {"00000000010010", 0, 29},
    {"00000000010011", 0, 28},
    {"00000000010100", 0, 27},
    {"00000000010101", 0, 26},
    {"00000000010110", 0, 25},
    {"00000000010111", 0, 24},
    {"00000000011000", 0, 23},
    {"00000000011001", 0, 22},
    {"00000000011010", 0, 21},
    {"00000000011011", 0, 20},
    {"00000000011100", 0, 19},
    {"00000000011101", 0, 18},
    {"00000000011110", 0, 17},
    {"00000000011111", 0, 16},
    {"000000000010000", 0, 40},
    {"000000000010001", 0, 39},
    {"000000000010010", 0, 38},
    {"000000000010011", 0, 37},
    {"000000000010100", 0, 36},
    {"000000000010101", 0, 35},
    {"000000000010110", 0, 34},
    {"000000000010111", 0, 33},
    {"000000000011000", 0, 32},
    {"000000000011001", 1, 14},
    {"000000000011010", 1, 13},
    {"000000000011011", 1, 12},
    {"000000000011100", 1, 11},
    {"000000000011101", 1, 10},
    {"000000000011110", 1, 9},
    {"000000000011111", 1, 8},
    {"0000000000010000", 1, 18},
    {"0000000000010001", 1, 17},
    {"0000000000010010", 1, 16},
    {"0000000000010011", 1, 15},
    {"0000000000010100", 6, 3},
    {"0000000000010101", 16, 2},
    {"0000000000010110", 15, 2},
    {"0000000000010111", 14, 2},
    {"0000000000011000", 13, 2},
    {"0000000000011001", 12, 2},
    {"0000000000011010", 11, 2},
    {"0000000000011011", 31, 1},
    {"0000000000011100", 30, 1},
    {"0000000000011101", 29, 1},
    {"0000000000011110", 28, 1},
    {"0000000000011111", 27, 1},
};

// MPEG-1's DC size codes (table B.12, luminance; B.13, chrominance).
constexpr std::string_view kLumaDcSizes[] = {"100", "00", "01", "101", "110", "1110", "11110", "111110", "1111110"};
constexpr std::string_view kChromaDcSizes[] = {"00",    "01",     "10",      "110",     "1110",
                                               "11110", "111110", "1111110", "11111110"};

// MPEG-1's default intra quantiser matrix, in raster order.
constexpr std::array<uint8_t, 64> kIntraMatrix = {8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37,
                                                  19, 22, 26, 27, 29, 34, 34, 38, 22, 22, 26, 27, 29, 34, 37, 40,
                                                  22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32, 35, 40, 48, 58,
                                                  26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};

// MPEG-1's zigzag scan: the raster place of each coefficient in turn.
constexpr std::array<uint8_t, 64> kZigzag = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

using Lookup = std::array<Code, 1 << 16>;

void addCode(Lookup &table, std::string_view bits, int8_t run, int16_t level) {
    uint32_t value = 0;
    for (char c : bits)
        value = value << 1 | uint32_t(c == '1');
    const uint32_t first = value << (16 - bits.size());
    const uint32_t count = 1u << (16 - bits.size());
    for (uint32_t i = 0; i < count; ++i)
        table[first + i] = {uint8_t(bits.size()), run, level};
}

const Lookup &acTable() {
    static const Lookup kTable = [] {
        Lookup t{};
        for (const AcCode &c : kAcCodes)
            addCode(t, c.bits, c.run, c.level);
        addCode(t, "10", kEnd, 0);
        addCode(t, "000001", kEscape, 0);
        return t;
    }();
    return kTable;
}

template<size_t N>
Lookup dcTable(const std::string_view (&sizes)[N]) {
    Lookup t{};
    for (size_t i = 0; i < N; ++i)
        addCode(t, sizes[i], 0, int16_t(i));
    return t;
}

const Lookup &lumaDcTable() {
    static const Lookup kTable = dcTable(kLumaDcSizes);
    return kTable;
}
const Lookup &chromaDcTable() {
    static const Lookup kTable = dcTable(kChromaDcSizes);
    return kTable;
}

// The frame's bits: 32-bit little-endian words, each read from its top
// bit. Reading past the end gives zeros and marks it.
class Bits {
public:
    Bits(const uint8_t *data, size_t size) : _data(data), _size(size) {}
    uint32_t peek16() {
        fill();
        return uint32_t(_cache >> (_count - 16)) & 0xffff;
    }
    void skip(int n) {
        fill();
        _count -= n;
    }
    uint32_t get(int n) {
        if (n == 0)
            return 0;
        fill();
        _count -= n;
        return uint32_t(_cache >> _count) & ((1u << n) - 1);
    }
    bool overrun() const { return _overrun; }

private:
    void fill() {
        while (_count <= 32) {
            uint32_t word = 0;
            if (_at + 4 <= _size)
                word = uint32_t(_data[_at]) | uint32_t(_data[_at + 1]) << 8 | uint32_t(_data[_at + 2]) << 16 |
                       uint32_t(_data[_at + 3]) << 24;
            else
                _overrun = _overrun || _at >= _size + 8;
            _at += 4;
            _cache = _cache << 32 | word;
            _count += 32;
        }
    }

    const uint8_t *_data;
    size_t _size;
    size_t _at = 0;
    uint64_t _cache = 0;
    int _count = 0; // bits of _cache not yet read, the lowest
    bool _overrun = false;
};

// The 8x8 inverse DCT, as floating point: the frame's pictures needn't
// match EA's own integer one to the last bit.
void inverseDct(const float *in, float *out) {
    static const std::array<float, 64> kBasis = [] {
        std::array<float, 64> b{};
        for (int x = 0; x < 8; ++x)
            for (int u = 0; u < 8; ++u)
                b[size_t(x * 8 + u)] =
                    float((u == 0 ? std::sqrt(0.5) : 1.0) * std::cos((2 * x + 1) * u * 3.14159265358979 / 16) / 2);
        return b;
    }();
    float rows[64];
    for (int v = 0; v < 8; ++v)
        for (int x = 0; x < 8; ++x) {
            float s = 0;
            for (int u = 0; u < 8; ++u)
                s += kBasis[size_t(x * 8 + u)] * in[v * 8 + u];
            rows[v * 8 + x] = s;
        }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            float s = 0;
            for (int v = 0; v < 8; ++v)
                s += kBasis[size_t(y * 8 + v)] * rows[v * 8 + x];
            out[y * 8 + x] = s;
        }
}

uint8_t clampByte(float v) {
    return uint8_t(std::clamp(std::lround(v), 0L, 255L));
}

} // namespace

Error Decoder::decode(const uint8_t *data, size_t size) {
    if (size < kHeader)
        return Error::fail("a TQI frame cut short");
    const int width = data[0] | data[1] << 8;
    const int height = data[2] | data[3] << 8;
    if (width <= 0 || height <= 0 || width % kMacroblock || height % kMacroblock)
        return Error::fail("a TQI frame of " + std::to_string(width) + "x" + std::to_string(height));
    if (width != _width || height != _height) {
        _width = width;
        _height = height;
        _picture.create(width, height, PixelFormat::kRGBA32);
        _y.assign(size_t(width) * size_t(height), 0);
        _cb.assign(size_t(width / 2) * size_t(height / 2), 128);
        _cr.assign(_cb.size(), 128);
    }
    // MPEG-1's quantiser scale from the frame's quantiser (0-107).
    const float scale = (107.5f - float(data[4])) * 0.625f;
    float matrix[64];
    for (int i = 0; i < 64; ++i)
        matrix[i] = float(kIntraMatrix[size_t(i)]) * scale / 8;

    Bits bits(data + kHeader, size - kHeader);
    const Lookup &ac = acTable();
    int predictors[3] = {0, 0, 0}; // Y, Cb, Cr: from 0 for the frame, not MPEG-1's 1024
    float coefficients[64];
    float pixels[64];
    for (int my = 0; my < height / kMacroblock; ++my)
        for (int mx = 0; mx < width / kMacroblock; ++mx)
            for (int block = 0; block < 6; ++block) {
                const int plane = block < 4 ? 0 : block - 3;
                const Code dc = (plane == 0 ? lumaDcTable() : chromaDcTable())[bits.peek16()];
                if (dc.length == 0)
                    return Error::fail("a TQI frame's DC code isn't known");
                bits.skip(dc.length);
                const int length = dc.level;
                int diff = int(bits.get(length));
                if (length > 0 && diff < 1 << (length - 1))
                    diff -= (1 << length) - 1;
                predictors[plane] += diff;
                std::fill(coefficients, coefficients + 64, 0.0f);
                coefficients[0] = float(predictors[plane] * 8);
                for (int i = 0;;) {
                    const Code c = ac[bits.peek16()];
                    if (c.length == 0)
                        return Error::fail("a TQI frame's AC code isn't known");
                    bits.skip(c.length);
                    if (c.run == kEnd)
                        break;
                    int run = c.run;
                    int level = c.level;
                    if (c.run == kEscape) {
                        run = int(bits.get(6));
                        level = int(int8_t(bits.get(8)));
                        if (level == 0)
                            level = int(bits.get(8));
                        else if (level == -128)
                            level = int(bits.get(8)) - 256;
                    } else if (bits.get(1)) {
                        level = -level;
                    }
                    i += run + 1;
                    if (i > 63)
                        return Error::fail("a TQI frame's block runs past its end");
                    const int at = kZigzag[size_t(i)];
                    coefficients[at] = float(level) * matrix[at];
                }
                if (bits.overrun())
                    return Error::fail("a TQI frame cut short");
                inverseDct(coefficients, pixels);
                std::vector<uint8_t> &dst = plane == 0 ? _y : plane == 1 ? _cb : _cr;
                const int stride = plane == 0 ? width : width / 2;
                const int x0 = plane == 0 ? mx * 16 + (block % 2) * 8 : mx * 8;
                const int y0 = plane == 0 ? my * 16 + (block / 2) * 8 : my * 8;
                for (int y = 0; y < 8; ++y)
                    for (int x = 0; x < 8; ++x)
                        dst[size_t(y0 + y) * size_t(stride) + size_t(x0 + x)] = clampByte(pixels[y * 8 + x]);
            }

    // MPEG-1's colours: ITU-R BT.601, the levels 16-235 for Y.
    for (int y = 0; y < height; ++y) {
        uint8_t *row = _picture.row(y);
        for (int x = 0; x < width; ++x) {
            const float l = 1.164f * (float(_y[size_t(y) * size_t(width) + size_t(x)]) - 16);
            const size_t c = size_t(y / 2) * size_t(width / 2) + size_t(x / 2);
            const float u = float(_cb[c]) - 128;
            const float v = float(_cr[c]) - 128;
            row[x * 4 + 0] = clampByte(l + 1.596f * v);
            row[x * 4 + 1] = clampByte(l - 0.392f * u - 0.813f * v);
            row[x * 4 + 2] = clampByte(l + 2.017f * u);
            row[x * 4 + 3] = 255;
        }
    }
    return Error::success();
}

} // namespace reluva::tqi
