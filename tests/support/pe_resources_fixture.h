// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::test::fixtures {
inline void put16(std::vector<uint8_t>& d, size_t at, uint16_t n) {
    d[at] = uint8_t(n); d[at + 1] = uint8_t(n >> 8);
}
inline void put32(std::vector<uint8_t>& d, size_t at, uint32_t n) {
    put16(d, at, uint16_t(n)); put16(d, at + 2, uint16_t(n >> 16));
}
inline std::vector<uint8_t> stringBlock(const std::array<std::u16string, 16>& strings) {
    std::vector<uint8_t> out;
    auto add = [&](uint16_t n) { out.push_back(uint8_t(n)); out.push_back(uint8_t(n >> 8)); };
    for (const auto& s : strings) { add(uint16_t(s.size())); for (char16_t c : s) add(uint16_t(c)); }
    return out;
}
// Original minimal PE image containing one resource. It is data assembled
// in code, not a compiled or embedded retail executable.
inline std::vector<uint8_t> resourcePe(const std::vector<uint8_t>& payload,
                                      uint32_t type = 6, uint32_t name = 1, bool plus = false) {
    const size_t rawSize = (96 + payload.size() + 511) & ~size_t(511);
    std::vector<uint8_t> out(512 + rawSize, 0);
    put16(out, 0, 0x5a4d); put32(out, 0x3c, 0x80);
    put32(out, 0x80, 0x4550); put16(out, 0x84, plus ? 0x8664 : 0x14c);
    put16(out, 0x86, 1); put16(out, 0x94, plus ? 240 : 224);
    const size_t opt = 0x98, dirs = opt + (plus ? 112 : 96), section = opt + (plus ? 240 : 224);
    put16(out, opt, plus ? 0x20b : 0x10b);
    put32(out, dirs - 4, 3); put32(out, dirs + 16, 0x3000); put32(out, dirs + 20, uint32_t(96 + payload.size()));
    for (size_t i = 0; i < 5; ++i) out[section + i] = uint8_t(".rsrc"[i]);
    put32(out, section + 8, uint32_t(rawSize)); put32(out, section + 12, 0x3000);
    put32(out, section + 16, uint32_t(rawSize)); put32(out, section + 20, 512);
    const size_t r = 512;
    put16(out, r + 14, 1); put32(out, r + 16, type); put32(out, r + 20, 0x80000018);
    put16(out, r + 24 + 14, 1); put32(out, r + 40, name); put32(out, r + 44, 0x80000030);
    put16(out, r + 48 + 14, 1); put32(out, r + 64, 1033); put32(out, r + 68, 72);
    put32(out, r + 72, 0x3060); put32(out, r + 76, uint32_t(payload.size())); put32(out, r + 80, 1200);
    for (size_t i = 0; i < payload.size(); ++i) out[r + 96 + i] = payload[i];
    return out;
}
} // namespace reluva::test::fixtures
