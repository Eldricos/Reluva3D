// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Builds EA's picture banks (.fsh, "SHPI") for tests, laid out as
// docs/common/image/fsh.md says.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace reluva::test {

/**
 * Appends a picture's or palette's 16-byte block header: its type, the
 * offset to the next block (0 for the last), its size, centre and place.
 */
inline void putShapeHeader(std::vector<uint8_t> &b, uint8_t type, uint32_t next, int w, int h, int cx, int cy, int x,
                           int y) {
    b.push_back(type);
    for (int i = 0; i < 3; ++i)
        b.push_back(uint8_t(next >> (8 * i)));
    for (int v : {w, h, cx, cy, x, y}) {
        b.push_back(uint8_t(unsigned(v)));
        b.push_back(uint8_t(unsigned(v) >> 8));
    }
}

/**
 * An SHPI bank of the shapes, each a 4-character name and its blocks.
 * @param id The directory's id: "GIMX" as Need for Speed III's.
 */
inline std::vector<uint8_t> makeShapeBank(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &shapes,
                                          const std::string &id = "GIMX") {
    std::vector<uint8_t> b = {'S', 'H', 'P', 'I', 0, 0, 0, 0};
    auto put32 = [&b](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            b.push_back(uint8_t(v >> (8 * i)));
    };
    auto put4 = [&b](const std::string &s) {
        for (size_t i = 0; i < 4; ++i)
            b.push_back(i < s.size() ? uint8_t(s[i]) : 0);
    };
    put32(uint32_t(shapes.size()));
    put4(id);
    uint32_t offset = 16 + 8 * uint32_t(shapes.size());
    for (const auto &s : shapes) {
        put4(s.first);
        put32(offset);
        offset += uint32_t(s.second.size());
    }
    for (const auto &s : shapes)
        b.insert(b.end(), s.second.begin(), s.second.end());
    for (int i = 0; i < 4; ++i)
        b[4 + size_t(i)] = uint8_t(b.size() >> (8 * i));
    return b;
}

} // namespace reluva::test
