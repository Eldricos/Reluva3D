// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/ground_fractal.h"

#include <memory>

namespace reluva::stawayteam {

namespace {

constexpr int kSide = kGroundCell + 1;

// A value moved by a random amount from -amp to amp - 1; past 255 is 255,
// under 0 is 1, 0 meaning a place not filled yet (0x4ac290).
uint8_t displace(int mid, int amp, R250 &random) {
    const int v = int(random.next() & uint32_t(2 * amp - 1)) - amp + mid;
    if (v > 255)
        return 255;
    return v < 0 ? 1 : uint8_t(v);
}

// An edge: its ends' values halved down to every pixel, the first steps
// moved most, by an eighth of its length at first (0x4abec0 along a row,
// 0x4ac310 down a column).
void edge(uint8_t *p, int length, int stride, R250 &random) {
    int amp = length >> 3;
    for (int step = length; step > 1; step /= 2, amp >>= 1) {
        const int half = step / 2;
        for (int i = 0; i < length; i += step) {
            const int mid = (p[i * stride] + p[(i + step) * stride]) >> 1;
            p[(i + half) * stride] = amp ? displace(mid, amp, random) : uint8_t(mid);
        }
    }
}

// The inside: each square's middle the average of its corners, and the
// middles of its sides not made yet those of their ends, moved by an
// eighth of the side at first; at a move of 0, the rest without (0x4abfd0).
void inside(std::vector<uint8_t> &v, R250 &random) {
    const auto at = [&](int x, int y) -> uint8_t & { return v[size_t(y) * kSide + size_t(x)]; };
    int amp = kGroundCell >> 3;
    for (int step = kGroundCell; step > 1; step /= 2, amp >>= 1) {
        const int half = step / 2;
        const auto side = [&](uint8_t &place, int a, int b) {
            if (place == 0)
                place = amp ? displace((a + b) >> 1, amp, random) : uint8_t((a + b) >> 1);
        };
        for (int y = 0; y < kGroundCell; y += step)
            for (int x = 0; x < kGroundCell; x += step) {
                const int a = at(x, y);
                const int b = at(x + step, y);
                const int c = at(x + step, y + step);
                const int d = at(x, y + step);
                at(x + half, y + half) = uint8_t((a + b + c + d + 2) >> 2);
                side(at(x + step, y + half), b, c);
                side(at(x, y + half), a, d);
                side(at(x + half, y), a, b);
                side(at(x + half, y + step), c, d);
            }
    }
}

} // namespace

R250::R250(uint32_t seed) {
    // Filled last first; then one number in each 7 from the fourth given
    // a bit of its own, so the 32 are independent (0x54c0af).
    uint32_t state = seed;
    for (int i = 249; i >= 0; --i) {
        const uint64_t product = uint64_t(state) * 0x41c64e6du + 0x3039u;
        state = uint32_t(product);
        _table[size_t(i)] = uint32_t(product >> 16);
    }
    for (int b = 0; b < 32; ++b) {
        uint32_t &t = _table[size_t(3 + 7 * b)];
        t = (t & (0xffffffffu >> b)) | (0x80000000u >> b);
    }
}

uint32_t R250::next() {
    const uint32_t r = _table[size_t(_j)] ^ _table[size_t(_k)];
    _table[size_t(_j)] = r;
    _j = (_j + 1) % 250;
    _k = (_k + 1) % 250;
    return r;
}

const R250 &groundGenerator(int n) {
    static const auto kGenerators = [] {
        std::vector<std::unique_ptr<R250>> all;
        for (uint32_t i = 1; i <= 256; ++i)
            all.push_back(std::make_unique<R250>(i * 0xe6cefu));
        return all;
    }();
    return *kGenerators[size_t(n & 255)];
}

std::vector<uint8_t> groundFractal(const GroundMap &ground, int cx, int cy) {
    std::vector<uint8_t> v(size_t(kSide) * kSide, 0);
    const auto corner = [&](const std::vector<uint8_t> &values, int x, int y) -> int {
        if (x < 0 || y < 0 || x >= ground.width || y >= ground.height)
            return 0;
        const size_t i = size_t(y) * size_t(ground.width) + size_t(x);
        return i < values.size() ? values[i] : 0;
    };
    const auto value = [&](int x, int y) { return uint8_t(corner(ground.values, x, y)); };
    const auto seed = [&](int x, int y) { return corner(ground.seeds, x, y); };
    v[0] = value(cx, cy);
    v[kGroundCell] = value(cx + 1, cy);
    v[size_t(kGroundCell) * kSide] = value(cx, cy + 1);
    v[size_t(kGroundCell) * kSide + kGroundCell] = value(cx + 1, cy + 1);
    // A row's edge by its left corner's seed and 0x55 more, a column's by
    // its top corner's; the inside by the cell's own and 0x56 less.
    R250 random = groundGenerator(seed(cx, cy) + 0x55);
    edge(v.data(), kGroundCell, 1, random);
    random = groundGenerator(seed(cx, cy));
    edge(v.data(), kGroundCell, kSide, random);
    random = groundGenerator(seed(cx + 1, cy));
    edge(v.data() + kGroundCell, kGroundCell, kSide, random);
    random = groundGenerator(seed(cx, cy + 1) + 0x55);
    edge(v.data() + size_t(kGroundCell) * kSide, kGroundCell, 1, random);
    random = groundGenerator(seed(cx, cy) - 0x56);
    inside(v, random);
    return v;
}

} // namespace reluva::stawayteam
