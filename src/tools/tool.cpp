// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tools/tool.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <utility>

namespace reluva::tool {

std::vector<Registered>& registered() {
    static std::vector<Registered> all;
    return all;
}

Commands::Commands(const std::string& engine, std::vector<Command> list) {
    for (Command& c : list)
        registered().push_back({engine, std::move(c)});
}

bool readFile(const char* path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

uint16_t rd16(const Bytes& d, size_t o) { return o + 2 <= d.size() ? uint16_t(d[o] | d[o + 1] << 8) : 0; }
uint32_t rd32(const Bytes& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 |
                                   uint32_t(d[o + 3]) << 24
                             : 0;
}

void hexdump(const Bytes& d, size_t off, size_t len) {
    size_t end = std::min(d.size(), off + len);
    for (size_t row = off; row < end; row += 16) {
        std::printf("%08zx: ", row);
        for (size_t i = 0; i < 16; ++i) {
            if (row + i < end)
                std::printf("%02x", d[row + i]);
            else
                std::printf("  ");
            if (i % 2)
                std::printf(" ");
        }
        std::printf(" ");
        for (size_t i = 0; i < 16 && row + i < end; ++i) {
            uint8_t c = d[row + i];
            std::putchar(c >= 32 && c < 127 ? c : '.');
        }
        std::printf("\n");
    }
}

long long parseNumber(const char* s) { return std::strtoll(s, nullptr, 0); }

std::string extensionOf(const std::string& name) {
    size_t dot = name.rfind('.');
    return dot == std::string::npos || name.find('\\', dot) != std::string::npos ? "" : name.substr(dot);
}

} // namespace reluva::tool
