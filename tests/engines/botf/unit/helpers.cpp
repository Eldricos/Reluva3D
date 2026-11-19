// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/botf/unit/helpers.h"

#include <fstream>

namespace reluva::botf::test {

void put16(Bytes &b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put32(Bytes &b, uint32_t v) {
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

void set32(Bytes &b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

void writeFile(const std::filesystem::path &p, const Bytes &b) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
}

void putName(Bytes &b, const std::string &name) {
    std::string n = name;
    n.resize(13, '\0');
    b.insert(b.end(), n.begin(), n.end());
}

void putWidget(Bytes &b, uint32_t type, uint32_t id, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
               const std::string &bg) {
    put32(b, type);
    put32(b, id);
    put32(b, x);
    put32(b, y);
    put32(b, w);
    put32(b, h);
    putName(b, bg);
}

void putWindowCounts(Bytes &b, const std::vector<std::pair<uint32_t, uint32_t>> &byType) {
    uint32_t total = 0;
    for (auto [t, n] : byType)
        total += n;
    put32(b, total);
    for (uint32_t t = 1; t <= 15; ++t) {
        uint32_t n = 0;
        for (auto [tt, nn] : byType)
            if (tt == t)
                n = nn;
        put32(b, n);
    }
}

void putFont(Bytes &b, uint8_t r, uint8_t g, uint8_t bl, const std::string &file) {
    b.push_back(r);
    b.push_back(bl); // stored red, blue, green
    b.push_back(g);
    for (int i = 0; i < 7; ++i)
        put32(b, 0);
    put32(b, 1);
    putName(b, file);
}

Bytes makeLexicon(const std::vector<std::string> &words) {
    // The strings, then the table: a u32, a u16 count, offset and length each.
    Bytes f(4, 0);
    std::vector<uint32_t> offsets;
    for (const std::string &w : words) {
        offsets.push_back(uint32_t(f.size()));
        f.insert(f.end(), w.begin(), w.end());
        f.push_back(0);
    }
    const uint32_t table = uint32_t(f.size());
    f[0] = uint8_t(table);
    f[1] = uint8_t(table >> 8);
    f[2] = uint8_t(table >> 16);
    put32(f, 0);
    f.push_back(uint8_t(words.size()));
    f.push_back(uint8_t(words.size() >> 8));
    for (size_t i = 0; i < words.size(); ++i) {
        put32(f, offsets[i]);
        put32(f, uint32_t(words[i].size() + 1));
    }
    return f;
}

std::vector<std::string> MemoryArchive::list() const {
    std::vector<std::string> names;
    for (const auto &[n, d] : files)
        names.push_back(n);
    return names;
}

Error MemoryArchive::read(std::string_view name, std::vector<uint8_t> &out) const {
    auto it = files.find(std::string(name));
    if (it == files.end())
        return Error::fail("missing");
    out = it->second;
    return Error::success();
}

} // namespace reluva::botf::test
