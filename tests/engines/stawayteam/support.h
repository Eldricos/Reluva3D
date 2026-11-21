// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What Star Trek: Away Team's tests make of the game's formats, to read.

#pragma once

#include <cstdint>
#include <vector>

namespace reluva::stawayteam {

inline void put16(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
inline void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, x & 0xffff);
    put16(v, x >> 16);
}

// A frame as the game stores it: the header, then each block that has
// rows, then where each row starts in it. A run-length coded block begins
// with its own size, so its rows start at 4; a raw one's at 0.
inline std::vector<uint8_t> frameFile(int w, int h, int originX, int originY, uint32_t flags,
                                      const std::vector<std::vector<std::vector<uint8_t>>> &blocks, bool raw = false) {
    std::vector<uint8_t> out = {0x2e, 16};
    put16(out, uint16_t(originX));
    put16(out, uint16_t(originY));
    put16(out, uint16_t(w));
    put16(out, uint16_t(h));
    put16(out, 0);
    put32(out, flags);
    std::vector<std::vector<uint8_t>> data;
    std::vector<std::vector<uint32_t>> starts;
    for (size_t b = 0; b < 3; ++b) {
        std::vector<uint8_t> block;
        std::vector<uint32_t> rows;
        if (b < blocks.size() && !blocks[b].empty()) {
            if (!raw)
                put32(block, 0);
            for (const auto &row : blocks[b]) {
                rows.push_back(uint32_t(block.size()));
                block.insert(block.end(), row.begin(), row.end());
            }
            if (!raw) {
                const auto size = uint32_t(block.size());
                for (int i = 0; i < 4; ++i)
                    block[size_t(i)] = uint8_t(size >> (8 * i));
            }
        }
        put32(out, uint32_t(block.size()));
        data.push_back(block);
        starts.push_back(rows);
    }
    for (size_t b = 0; b < 3; ++b) {
        if (data[b].empty())
            continue;
        out.insert(out.end(), data[b].begin(), data[b].end());
        for (uint32_t r : starts[b])
            put32(out, r);
    }
    return out;
}

} // namespace reluva::stawayteam
