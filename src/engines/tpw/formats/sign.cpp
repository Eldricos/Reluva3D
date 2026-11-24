// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Read off Land of Invention's 26 signs, side by side:
// docs/engines/tpw/games/simcoaster/formats/sgn.md.

#include "engines/tpw/formats/sign.h"

#include <cstdlib>

namespace reluva::tpw {

namespace {

constexpr uint32_t kVersion = 101;
constexpr size_t kFontName = 0x11;
constexpr size_t kFontNameSize = 0x40;
constexpr size_t kFontFile = 0x51;
constexpr size_t kFontFileSize = 0x100;
// Each line's record: its place, then a Windows font description (its
// height negative, as a character's em), 0x1B4 apart.
constexpr size_t kFirstLine = 0x155;
constexpr size_t kLineStride = 0x1B4;
constexpr size_t kStretchAt = 0;
constexpr size_t kTopAt = 4;
constexpr size_t kHeightAt = 8;
constexpr size_t kWidthAt = 12;
constexpr size_t kWeightAt = 24;
constexpr size_t kFaceAt = 36;
constexpr size_t kFaceSize = 32;
// The lines' fills after them, each its rows, width and 4, then its
// pixels, alpha, red, green, blue; to the file's end.
constexpr size_t kFills = 0x3A1;
constexpr int kMaxFill = 1024;

int32_t i32(const std::vector<uint8_t> &d, size_t at) {
    return int32_t(uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24);
}

std::string text(const std::vector<uint8_t> &d, size_t at, size_t size) {
    std::string s;
    for (size_t i = at; i < at + size && d[i] != 0; ++i)
        s.push_back(char(d[i]));
    return s;
}

} // namespace

Error SignStyle::parse(const std::vector<uint8_t> &data) {
    *this = {};
    if (data.size() < kFills)
        return Error::fail("a sign (.sgn) cut short");
    if (uint32_t(i32(data, 0)) != kVersion)
        return Error::fail("not a sign (.sgn 101)");
    _fontName = text(data, kFontName, kFontNameSize);
    _fontFile = text(data, kFontFile, kFontFileSize);
    size_t fill = kFills;
    for (size_t k = 0; k < _lines.size(); ++k) {
        const size_t at = kFirstLine + k * kLineStride;
        Line &line = _lines[k];
        line.stretch = i32(data, at + kStretchAt);
        line.top = i32(data, at + kTopAt);
        line.height = std::abs(i32(data, at + kHeightAt));
        line.width = std::abs(i32(data, at + kWidthAt));
        line.weight = i32(data, at + kWeightAt);
        line.face = text(data, at + kFaceAt, kFaceSize);

        if (fill + 12 > data.size())
            return Error::fail("a sign (.sgn) cut short");
        const int rows = i32(data, fill);
        const int width = i32(data, fill + 4);
        if (rows <= 0 || width <= 0 || rows > kMaxFill || width > kMaxFill || i32(data, fill + 8) != 4)
            return Error::fail("a sign's (.sgn) fill isn't one");
        fill += 12;
        const size_t bytes = size_t(rows) * size_t(width) * 4;
        if (fill + bytes > data.size())
            return Error::fail("a sign (.sgn) cut short");
        line.fill.w = width;
        line.fill.h = rows;
        line.fill.rgba.resize(bytes);
        for (size_t px = 0; px < bytes; px += 4) {
            line.fill.rgba[px] = data[fill + px + 1];
            line.fill.rgba[px + 1] = data[fill + px + 2];
            line.fill.rgba[px + 2] = data[fill + px + 3];
            line.fill.rgba[px + 3] = 255;
        }
        fill += bytes;
    }
    return Error::success();
}

} // namespace reluva::tpw
