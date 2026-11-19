// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/palette.h"

namespace reluva {

void Palette::loadVga(const uint8_t *vga) {
    for (int i = 0; i < 768; i++) {
        const uint8_t v = vga[i] & 63;
        _data[size_t(i)] = uint8_t((v << 2) | (v >> 4));
    }
}

void Palette::setColor(int index, uint8_t r, uint8_t g, uint8_t b) {
    _data[size_t(index) * 3] = r;
    _data[size_t(index) * 3 + 1] = g;
    _data[size_t(index) * 3 + 2] = b;
}

int Palette::findBestColor(uint8_t r, uint8_t g, uint8_t b) const {
    int best = 0;
    int bestDist = 1 << 30;
    for (int i = 0; i < 256; i++) {
        const int dr = _data[size_t(i) * 3] - r;
        const int dg = _data[size_t(i) * 3 + 1] - g;
        const int db = _data[size_t(i) * 3 + 2] - b;
        const int dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

std::array<Color, 256> Palette::colors() const {
    return toColors(_data.data());
}

std::array<Color, 256> toColors(const uint8_t *rgb, int count) {
    std::array<Color, 256> out{};
    for (int i = 0; i < count && i < 256; i++)
        out[size_t(i)] = {rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]};
    return out;
}

} // namespace reluva
