// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/formats/postcard.h"

#include <cstdio>

namespace reluva::legoloco {

namespace {

constexpr uint16_t kVersion = 0x66; // the game refuses a card without it (0x442bbf)
constexpr size_t kStickers = 0x92;  // 128 of 6 bytes, to the end
constexpr int kMaxStickers = 128;

uint32_t read32(const std::vector<uint8_t> &d, size_t at) {
    return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24;
}

// A name or message: up to its terminator, within its field.
std::string text(const std::vector<uint8_t> &d, size_t at, size_t size) {
    std::string s;
    for (size_t i = at; i < at + size && d[i]; ++i)
        s += char(d[i]);
    return s;
}

} // namespace

Error parsePostcard(const std::vector<uint8_t> &data, Postcard &out) {
    out = Postcard();
    if (data.size() != kPostcardSize)
        return Error::fail("is " + std::to_string(data.size()) + " bytes, not " + std::to_string(kPostcardSize));
    if ((data[0] | data[1] << 8) != kVersion)
        return Error::fail("isn't marked as a postcard (0x66)");
    out.field04 = read32(data, 0x04);
    out.number = read32(data, 0x08);
    out.to = text(data, 0x0c, 21);
    out.from = text(data, 0x21, 21);
    out.field36 = uint16_t(data[0x36] | data[0x37] << 8);
    out.field38 = read32(data, 0x38);
    for (int i = 0; i < 3; ++i)
        out.field3c[i] = data[0x3c + size_t(i)];
    out.message = text(data, 0x3f, 0x8f - 0x3f);
    out.stamp = data[0x8f];
    out.background = data[0x90];
    out.backgroundSet = data[0x91];
    // A slot is used if it names a picture; the game keeps the used ones
    // first, but looks at every slot.
    for (int i = 0; i < kMaxStickers; ++i) {
        const size_t at = kStickers + 6 * size_t(i);
        if (!data[at + 1])
            continue;
        PostcardSticker s;
        s.set = data[at];
        s.number = data[at + 1];
        // The centre is kept halved, to fit a byte (0x442ce1).
        s.x = data[at + 2] * 2;
        s.width = data[at + 3];
        s.y = data[at + 4] * 2;
        s.height = data[at + 5];
        out.stickers.push_back(s);
    }
    return Error::success();
}

uint8_t clipArtSet(int category, int digit) {
    return uint8_t(category << 3 | (digit & 7));
}

// The game's names by category (0x445700): 0-15 a hex digit, 16-25 and
// 26-29 letters, 30 R (backgrounds) and 31 S0 (stamps).
std::string clipArtName(uint8_t set, uint8_t number) {
    const int category = set >> 3;
    const int digit = set & 7;
    char name[16];
    if (category < 16)
        std::snprintf(name, sizeof name, "%x%d%03d.bmp", category, digit, number);
    else if (category < 26)
        std::snprintf(name, sizeof name, "%c%d%03d.bmp", char(category + 0x58), digit, number);
    else if (category < 30)
        std::snprintf(name, sizeof name, "%c%d%03d.bmp", char(category + 0x5a), digit, number);
    else if (category == 30)
        std::snprintf(name, sizeof name, "R%d%03d.bmp", digit, number);
    else
        std::snprintf(name, sizeof name, "S0%03d.bmp", number);
    return name;
}

} // namespace reluva::legoloco
