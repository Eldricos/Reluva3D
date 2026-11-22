// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "tests/engines/botf/unit/helpers.h"

#include <algorithm>
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

Bytes makeAnimation(int w, int h, const std::vector<uint16_t> &frames, bool alpha) {
    const int across = 2;
    const int sheetW = w * across;
    const int sheetH = h * int((frames.size() + 1) / 2);
    Bytes a;
    // Height first, then width.
    for (const int v : {h, w, int(frames.size()), int(frames.size()), sheetW, sheetH})
        put16(a, uint32_t(v));
    put32(a, uint32_t(18 + sheetW * sheetH * 2));
    for (const uint8_t b : {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0})
        a.push_back(b);
    put16(a, uint32_t(sheetW));
    put16(a, uint32_t(sheetH));
    a.push_back(16);
    a.push_back(alpha ? 1 : 0);
    // Bottom-up, as the game's are.
    for (int y = sheetH - 1; y >= 0; --y)
        for (int x = 0; x < sheetW; ++x) {
            const size_t f = size_t(y / h * across + x / w);
            put16(a, f < frames.size() ? frames[f] : 0);
        }
    return a;
}

Bytes makeFont(int lineHeight, int rowsToDraw) {
    const int rows[5] = {10, 26, 26, 32, 26};
    std::vector<uint16_t> advances;
    int sheetW = 0;
    for (int r = 0, i = 0; r < 5; ++r) {
        int x = 1;
        for (int k = 0; k < rows[r]; ++k, ++i) {
            const int cell = 2 + i % 4 + (r > 0 && k == 0 ? 1 : 0);
            advances.push_back(uint16_t(2 + i % 4 + 1));
            x += cell + 1;
        }
        sheetW = std::max(sheetW, x);
    }
    const int sheetH = 5 * lineHeight + 1;
    std::vector<uint16_t> px(size_t(sheetW) * sheetH, 0); // top row first
    auto at = [&](int x, int y) -> uint16_t & { return px[size_t(y) * sheetW + x]; };
    const uint16_t frame = 0x03f1;
    for (int r = 0, i = 0; r < 5; ++r) {
        for (int x = 0; x < sheetW; ++x)
            at(x, r * lineHeight) = at(x, (r + 1) * lineHeight) = frame;
        int x = 1;
        for (int k = 0; k < rows[r]; ++k, ++i) {
            const int cell = 2 + i % 4 + (r > 0 && k == 0 ? 1 : 0);
            for (int y = r * lineHeight; y <= (r + 1) * lineHeight; ++y)
                at(x - 1, y) = at(x + cell, y) = frame;
            if (r < rowsToDraw)
                for (int y = r * lineHeight + 1; y < (r + 1) * lineHeight; ++y)
                    for (int cx = 0; cx < cell; ++cx)
                        if (!(cx == 0 && y == r * lineHeight + 1))
                            at(x + cx, y) = uint16_t(0x0400 + i);
            x += cell + 1;
        }
    }
    if (rowsToDraw < 5) // a full-height frame-coloured column splits a cell
        for (int y = 4 * lineHeight + 1; y < 5 * lineHeight; ++y)
            at(2, y) = frame;
    Bytes f(260, 0);
    f[0] = uint8_t(lineHeight);
    f[1] = 1;
    f[2] = 4;
    f[3] = 1;
    set32(f, 4, 10 | 120u << 16);
    set32(f, 8, uint32_t(sheetW) | uint32_t(sheetH) << 16);
    set32(f, 12, uint32_t(px.size() * 2));
    f[16] = 1;
    f[17] = 6;
    f[18] = 86;
    for (int i = 0; i < 120; ++i) {
        f[20 + 2 * size_t(i)] = uint8_t(advances[size_t(i)]);
        f[21 + 2 * size_t(i)] = uint8_t(advances[size_t(i)] >> 8);
    }
    Bytes tga = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    put16(tga, uint32_t(sheetW));
    put16(tga, uint32_t(sheetH));
    tga.push_back(16);
    tga.push_back(1); // rows from the bottom up
    for (int y = sheetH - 1; y >= 0; --y)
        for (int x = 0; x < sheetW; ++x)
            put16(tga, at(x, y));
    f.insert(f.end(), tga.begin(), tga.end());
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

Bytes lzssLiterals(const Bytes &raw) {
    Bytes f;
    for (size_t i = 0; i < raw.size(); i += 8) {
        const size_t n = std::min<size_t>(8, raw.size() - i);
        f.push_back(uint8_t((1u << n) - 1));
        f.insert(f.end(), raw.begin() + std::ptrdiff_t(i), raw.begin() + std::ptrdiff_t(i + n));
    }
    return f;
}

Bytes makeLzssArchive(const std::string &name, const std::vector<std::pair<std::string, Bytes>> &files,
                      const std::set<std::string> &stored) {
    Bytes f(24, 0);
    std::copy(name.begin(), name.begin() + std::min<size_t>(name.size(), 15), f.begin());
    std::vector<std::pair<uint32_t, uint32_t>> placed; // offset, packed size
    for (const auto &[file, raw] : files) {
        const size_t at = f.size();
        const Bytes packed = stored.count(file) ? raw : lzssLiterals(raw);
        f.insert(f.end(), packed.begin(), packed.end());
        placed.emplace_back(uint32_t(at), uint32_t(f.size() - at));
    }
    set32(f, 16, uint32_t(f.size()));
    set32(f, 20, uint32_t(files.size()));
    for (size_t i = 0; i < files.size(); ++i) {
        Bytes e(16, 0);
        std::copy(files[i].first.begin(), files[i].first.end(), e.begin());
        put32(e, placed[i].first);
        put32(e, uint32_t(i));
        put32(e, uint32_t(files[i].second.size()));
        put32(e, placed[i].second);
        put32(e, 1);
        put32(e, 0);
        f.insert(f.end(), e.begin(), e.end());
    }
    return f;
}

Bytes makeSave(const std::string &name, const std::string &description, uint32_t turn, uint8_t empire) {
    // gameInfo: a file name, the description at 0x0D, the turn at 0x40, the
    // player's empire at 0x252.
    Bytes info(0x2C0, 0);
    std::copy(description.begin(), description.end(), info.begin() + 0x0D);
    set32(info, 0x40, turn);
    info[0x252] = empire;
    return makeLzssArchive(name, {{"gameInfo", info}});
}

// Laid out as docs/engines/botf/formats/sav.md says.
std::vector<std::pair<std::string, Bytes>> galaxyFiles(int16_t systems, bool withQueue) {
    auto put16At = [](Bytes &b, size_t at, uint16_t v) {
        b[at] = uint8_t(v);
        b[at + 1] = uint8_t(v >> 8);
    };
    Bytes info(48, 0);
    set32(info, 0x08, 1);
    put16At(info, 0x0C, uint16_t(systems));
    put16At(info, 0x0E, 1);
    put16At(info, 0x14, 6);
    Bytes sectors;
    for (uint32_t i = 0; i < 6; ++i) {
        Bytes e(104, 0);
        set32(e, 0, i / 3);
        set32(e, 4, i % 3);
        put16At(e, 8, 0xFFFF);
        put16At(e, 10, i == 4 ? 0 : 0xFFFF);
        put16At(e, 12, i == 1 ? 0 : 0xFFFF);
        put16At(e, 14, i == 1 ? 0x10 : 0);
        set32(e, 40, i < 3 ? 0x10 : 0);
        sectors.insert(sectors.end(), e.begin(), e.end());
    }
    Bytes system(0x328, 0);
    set32(system, 4, 2);
    const std::string name = "Romulus";
    const std::string star = "s-r-";
    std::copy(name.begin(), name.end(), system.begin() + 0x08);
    system[0x31] = 4;
    std::copy(star.begin(), star.end(), system.begin() + 0x32);
    set32(system, 0x40, 355);
    put16At(system, 0x44, 4);
    set32(system, 0x4C, 4);
    put16At(system, 0x6A, 1);
    put16At(system, 0x6E, 3);
    for (uint32_t i = 0; i < 5; ++i)
        set32(system, 0x54 + 4 * i, 10 * (i + 1));
    set32(system, 0x80, 1);
    put16At(system, 0x88, 154);
    put16At(system, 0x90, 30);
    set32(system, 0xBC, 3350);
    set32(system, 0x78, 7);
    set32(system, 0x7C, 2);
    system[0x308] = 1; // one trade route coming in
    Bytes planet(76, 0);
    const std::string pname = "Romulus II\n";
    const std::string pani = "atol.ani";
    std::copy(pname.begin(), pname.end(), planet.begin());
    std::copy(pani.begin(), pani.end(), planet.begin() + 0x28);
    put16At(planet, 0x38, 2);
    planet[0x48] = 4;
    planet[0x49] = 10;
    system.insert(system.end(), planet.begin(), planet.end());
    system.insert(system.end(), 4 + 4, 0); // the planet's word, the trade route's
    if (withQueue) {
        system.insert(system.end(), 108, 0);
        system.back() = 1; // the first item bought
    }
    Bytes stellar(88, 0);
    set32(stellar, 0, 2);
    const std::string sname = "Nebula B2";
    std::copy(sname.begin(), sname.end(), stellar.begin() + 4);
    set32(stellar, 68, 7);
    set32(stellar, 72, 7);
    return {{"galInfo", info}, {"sector.lst", sectors}, {"systInfo", system}, {"stellInfo", stellar}};
}

} // namespace reluva::botf::test
