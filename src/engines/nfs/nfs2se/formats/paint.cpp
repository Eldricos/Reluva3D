// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/nfs2se/formats/paint.h"

#include "common/pe.h"
#include "engines/nfs/formats/commented_text.h"

#include <algorithm>

namespace reluva::nfs::nfs2se {

Error readPaintTable(const std::vector<uint8_t> &exe, PaintTable &out) {
    out = PaintTable{};
    PeImage pe;
    if (Error err = parsePe(exe, pe))
        return err;
    // The bytes at a program address, or none past the file.
    auto at = [&](uint32_t address, size_t size) -> const uint8_t * {
        if (address < pe.imageBase)
            return nullptr;
        const int64_t offset = pe.fileOffset(uint32_t(address - pe.imageBase));
        return offset >= 0 && size_t(offset) + size <= exe.size() ? exe.data() + offset : nullptr;
    };
    // Only where the code reads the two tables: the colour lookup
    // (0x4451c0, the table's address at 0x4451c6) and the front end's
    // stepping back through a car's colours (0x4106dd).
    const uint8_t lookup[] = {0x56, 0xc1, 0xe0, 0x04, 0x8b, 0xb0, 0x34, 0x36, 0x4c, 0x00};
    const uint8_t cars[] = {0x8a, 0xa6, 0x20, 0x1d, 0x4c, 0x00};
    const uint8_t *code = at(0x4451c0, sizeof lookup);
    const uint8_t *step = at(0x4106dd, sizeof cars);
    if (!code || !step || !std::equal(std::begin(lookup), std::end(lookup), code) ||
        !std::equal(std::begin(cars), std::end(cars), step))
        return Error::fail("Not the retail nfs2sen.exe: its paint tables aren't where they're read");
    constexpr size_t kMost = 128;
    for (size_t i = 0; i < kMost; ++i) {
        const uint8_t *e = at(0x4c3634 + uint32_t(16 * i), 16);
        if (!e)
            return Error::fail("nfs2sen.exe's paint colours run past the file");
        auto word = [&](int k) {
            return int(int32_t(e[4 * k] | e[4 * k + 1] << 8 | e[4 * k + 2] << 16 | uint32_t(e[4 * k + 3]) << 24));
        };
        const PaintColour c{word(0), word(1), word(2), word(3)};
        if (!c.hue && !c.saturation && !c.value && !c.threshold)
            break;
        out.colours.push_back(c);
    }
    const uint8_t *runs = at(0x4c1d20, 2 * out.cars.size());
    if (!runs)
        return Error::fail("nfs2sen.exe's cars' colours run past the file");
    for (size_t i = 0; i < out.cars.size(); ++i) {
        out.cars[i] = {runs[2 * i], runs[2 * i + 1]};
        if (size_t(runs[2 * i]) + runs[2 * i + 1] > out.colours.size())
            return Error::fail("nfs2sen.exe's car " + std::to_string(i) + " has colours past its table");
    }
    return Error::success();
}

Error parsePaintColours(std::string_view text, std::vector<PaintColour> &out) {
    out.clear();
    const std::vector<int> n = commentedNumbers(text);
    if (n.empty() || n.size() % 4)
        return Error::fail("Not a paint colour file: " + std::to_string(n.size()) + " numbers, not four a colour");
    for (size_t i = 0; i < n.size(); i += 4)
        out.push_back({n[i], n[i + 1], n[i + 2], n[i + 3]});
    return Error::success();
}

std::array<int, 3> paintRgb(const PaintColour &c) {
    if (c.value <= 0)
        return {0, 0, 0};
    const int v = std::min(c.value, 256);
    if (c.saturation <= 0)
        return {v, v, v};
    // Six sectors of the turn, as hue-saturation-value goes, in 256ths.
    const int s = std::min(c.saturation, 256);
    const int at = (c.hue & 0xff) * 6;
    const int sector = at >> 8;
    const int f = at & 0xff;
    const int p = v * (256 - s) / 256;
    const int q = v * (65536 - s * f) / 65536;
    const int t = v * (65536 - s * (256 - f)) / 65536;
    switch (sector) {
    case 0:
        return {v, t, p};
    case 1:
        return {q, v, p};
    case 2:
        return {p, v, t};
    case 3:
        return {p, q, v};
    case 4:
        return {t, p, v};
    default:
        return {v, p, q};
    }
}

bool isPaint(uint8_t r, uint8_t g, uint8_t b) {
    return r < 0x56 && g >= 0x80 && b < 0x56;
}

std::array<uint8_t, 3> repaint(uint8_t r, uint8_t g, uint8_t b, const PaintColour &colour) {
    if (!isPaint(r, g, b))
        return {r, g, b};
    const std::array<int, 3> paint = paintRgb(colour);
    const int shade = g - 0x80; // 0-127
    const int threshold = std::clamp(colour.threshold, 1, 127);
    std::array<int, 3> out{};
    for (size_t i = 0; i < 3; ++i) {
        if (shade <= threshold) {
            out[i] = paint[i] * shade / threshold;
        } else {
            const int light = std::min(paint[i] * 4, 255);
            out[i] = paint[i] + (light - paint[i]) * (shade - threshold) / (127 - threshold);
        }
    }
    if (r > 0 && b > 0) {
        out[0] += r * 3 / 2;
        out[1] += shade / 2;
        out[2] += b * 3 / 2;
    }
    return {uint8_t(std::min(out[0], 255)), uint8_t(std::min(out[1], 255)), uint8_t(std::min(out[2], 255))};
}

} // namespace reluva::nfs::nfs2se
