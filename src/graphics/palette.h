// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>

namespace reluva {

// 256 colours, 8 bits per channel, as games with 8-bit screens keep them
// (and as System::setPalette takes them, with colors()).
class Palette {
public:
    Palette() { _data.fill(0); }

    // 768 bytes of 6-bit VGA DAC values (0..63), as DOS games store them.
    void loadVga(const uint8_t *vga);
    void setColor(int index, uint8_t r, uint8_t g, uint8_t b);
    void getColor(int index, uint8_t &r, uint8_t &g, uint8_t &b) const {
        r = _data[size_t(index) * 3], g = _data[size_t(index) * 3 + 1], b = _data[size_t(index) * 3 + 2];
    }
    // R, G, B for each entry in turn.
    const uint8_t *data() const { return _data.data(); }
    uint8_t *data() { return _data.data(); }
    // The entry nearest to (r, g, b), weighted as the eye sees: green most.
    int findBestColor(uint8_t r, uint8_t g, uint8_t b) const;
    std::array<Color, 256> colors() const;

private:
    std::array<uint8_t, 768> _data;
};

// R, G, B bytes (`count` entries) as Colors, for System::setPalette.
std::array<Color, 256> toColors(const uint8_t *rgb, int count = 256);

} // namespace reluva
