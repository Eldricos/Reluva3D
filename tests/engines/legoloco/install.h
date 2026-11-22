// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A small LEGO Loco install of the tests' own, in the game's formats:
// Exe/Loco.exe with a string table naming three resources, and art-res
// with a resource file (a two-frame picture and its definition, a sound,
// an area board), a layout on disk and a backdrop. Nothing of the game.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace reluva::legoloco::fixture {

using Bytes = std::vector<uint8_t>;

inline void put16(Bytes &b, size_t at, uint32_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}

inline void put32(Bytes &b, size_t at, uint32_t v) {
    put16(b, at, v & 0xffff);
    put16(b, at + 2, v >> 16);
}

inline void writeFile(const std::filesystem::path &path, const Bytes &b) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
}

inline Bytes text(const std::string &s) {
    return Bytes(s.begin(), s.end());
}

/**
 * An executable with one block of string table, block `block` (ids
 * (block - 1) * 16 on), holding `strings` by their place in it.
 */
inline Bytes stringTableExe(uint32_t block, const std::vector<std::pair<int, std::string>> &strings) {
    Bytes f(0x600, 0);
    f[0] = 'M';
    f[1] = 'Z';
    put32(f, 0x3c, 0x80);
    put32(f, 0x80, 0x00004550);
    put16(f, 0x84, 0x14c);
    put16(f, 0x86, 1);
    put16(f, 0x94, 224);
    put16(f, 0x98, 0x10b);
    put32(f, 0x98 + 32, 0x1000);
    put32(f, 0x98 + 36, 0x200);
    put32(f, 0x98 + 56, 0x2000);
    put32(f, 0x98 + 60, 0x200);
    put32(f, 0x98 + 92, 16);
    put32(f, 0x98 + 96 + 2 * 8, 0x1000);
    put32(f, 0x98 + 96 + 2 * 8 + 4, 0x400);
    const size_t sec = 0x98 + 224;
    std::memcpy(&f[sec], ".rsrc", 5);
    put32(f, sec + 8, 0x400);
    put32(f, sec + 12, 0x1000);
    put32(f, sec + 16, 0x400);
    put32(f, sec + 20, 0x200);
    // type 6 -> block -> language 1033 -> the data
    const size_t r = 0x200;
    put16(f, r + 14, 1);
    put32(f, r + 16, 6);
    put32(f, r + 20, 0x80000000 | 0x18);
    put16(f, r + 0x18 + 14, 1);
    put32(f, r + 0x18 + 16, block);
    put32(f, r + 0x18 + 20, 0x80000000 | 0x30);
    put16(f, r + 0x30 + 14, 1);
    put32(f, r + 0x30 + 16, 1033);
    put32(f, r + 0x30 + 20, 0x48);
    put32(f, r + 0x48, 0x1060);
    put32(f, r + 0x48 + 4, 0x300);
    size_t at = r + 0x60;
    for (int k = 0; k < 16; ++k) {
        std::string s;
        for (const auto &[place, str] : strings)
            if (place == k)
                s = str;
        put16(f, at, uint32_t(s.size()));
        at += 2;
        for (char c : s) {
            put16(f, at, uint8_t(c));
            at += 2;
        }
    }
    return f;
}

/** An 8-bit BMP whose pixel at (x, y) is `pixel(x, y)`, colour i being (i, 255 - i, 128). */
template<typename F>
Bytes bmp8(int w, int h, F pixel) {
    const int stride = (w + 3) & ~3;
    Bytes b(54 + 1024 + size_t(stride) * size_t(h), 0);
    b[0] = 'B';
    b[1] = 'M';
    put32(b, 2, uint32_t(b.size()));
    put32(b, 10, 54 + 1024);
    put32(b, 14, 40);
    put32(b, 18, uint32_t(w));
    put32(b, 22, uint32_t(h));
    put16(b, 26, 1);
    put16(b, 28, 8);
    put32(b, 46, 256);
    for (int i = 0; i < 256; ++i) {
        b[54 + size_t(i) * 4] = 128;
        b[55 + size_t(i) * 4] = uint8_t(255 - i);
        b[56 + size_t(i) * 4] = uint8_t(i);
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            b[54 + 1024 + size_t(h - 1 - y) * size_t(stride) + size_t(x)] = uint8_t(pixel(x, y));
    return b;
}

/** A mono 16-bit PCM WAV of `frames` samples. */
inline Bytes wav(int frames) {
    Bytes b(44 + size_t(frames) * 2, 0);
    std::memcpy(&b[0], "RIFF", 4);
    put32(b, 4, uint32_t(b.size() - 8));
    std::memcpy(&b[8], "WAVEfmt ", 8);
    put32(b, 16, 16);
    put16(b, 20, 1);
    put16(b, 22, 1);
    put32(b, 24, 22050);
    put32(b, 28, 44100);
    put16(b, 32, 2);
    put16(b, 34, 16);
    std::memcpy(&b[36], "data", 4);
    put32(b, 40, uint32_t(frames * 2));
    for (int i = 0; i < frames; ++i)
        put16(b, 44 + size_t(i) * 2, uint16_t(i * 300));
    return b;
}

/** A board of 64 by 48 squares with one piece of resource `resource` at (column, row). */
inline Bytes board(uint16_t resource, uint16_t column, uint16_t row, const char *backdrop) {
    Bytes b(0x114 + 64 * 48 + 0x80, 0);
    put16(b, 0, 8);
    put16(b, 2, 64);
    put16(b, 4, 48);
    put32(b, 8, 1);
    std::memcpy(&b[0xe], backdrop, std::strlen(backdrop));
    b[0x114 + size_t(row) * 64 + column] = 5;
    const size_t p = 0x114 + 64 * 48;
    put16(b, p, resource);
    put16(b, p + 2, column);
    put16(b, p + 4, row);
    std::memcpy(&b[p + 0x10], "Building", 8);
    return b;
}

/** resource.RFH and resource.RFD under `dir`, the entries stored as they are. */
inline void resourceFile(const std::filesystem::path &dir, const std::vector<std::pair<std::string, Bytes>> &entries) {
    Bytes rfh;
    Bytes rfd;
    for (const auto &[name, data] : entries) {
        Bytes head(4 + name.size() + 1 + 8, 0);
        put32(head, 0, uint32_t(name.size() + 1));
        std::memcpy(&head[4], name.data(), name.size());
        put32(head, 4 + name.size() + 1, uint32_t(data.size()));
        rfh.insert(rfh.end(), head.begin(), head.end());
        rfd.insert(rfd.end(), data.begin(), data.end());
    }
    writeFile(dir / "resource.RFH", rfh);
    writeFile(dir / "resource.RFD", rfd);
}

/** The definition of the test install's track piece: one square, two frames. */
constexpr const char *kPostDat = "physical_occupancy\r\n1 1 1\r\n1\r\n"
                                 "bitmap_occupancy\r\n1 2\r\n0\r\n1\r\n"
                                 "RMBSeq 3164\r\n-9\r\n"
                                 "button offset 0 0 0\r\n"
                                 "total_number_of_frames 2\r\n"
                                 "number_of_frame_sets 1\r\n"
                                 "cursor_frame_set 0 0\r\n"
                                 "blink 0 1 2 0 0 0 0 0 0 0\r\n-9\r\n";

/**
 * Makes the install under `root`: resource 3164 is `track\post`, a
 * two-frame 32x32 picture with its definition; 3165 is `sounds\bell`; the
 * area board holds one post, and art-res\SAVEGAME\test.sav another.
 */
inline void makeInstall(const std::filesystem::path &root) {
    writeFile(root / "Exe" / "Loco.exe", stringTableExe(198, {{11, "Go!"}, {12, "track\\post"}, {13, "sounds\\bell"}}));
    const std::filesystem::path art = root / "art-res";
    const Bytes post = bmp8(32, 32, [](int x, int y) { return x < 16 ? (y < 16 ? 0 : 20) : (y < 16 ? 0 : 200); });
    resourceFile(art, {{"track\\post.bmp", post},
                       {"track\\post.dat", text(kPostDat)},
                       {"sounds\\bell.wav", wav(2205)},
                       {"layouts\\area1.sav", board(3164, 10, 12, "")}});
    writeFile(art / "SAVEGAME" / "test.sav", board(3164, 3, 4, "GREEN"));
    writeFile(art / "backdrop" / "GREEN.bmp", bmp8(8, 8, [](int x, int y) { return (x + y) % 2 ? 60 : 70; }));
}

} // namespace reluva::legoloco::fixture
