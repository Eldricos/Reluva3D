// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Small stand-ins for Jedi Knight's install and CDs, for Sith's tests: the
// files' names and places as the game has them, none of its data.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace reluva::sith::testing {

namespace stdfs = std::filesystem;

/** Writes `bytes` to `path`, making its folders. */
inline void writeBytes(const stdfs::path &path, const std::vector<uint8_t> &bytes) {
    stdfs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

/** An empty file at `path`. */
inline void touch(const stdfs::path &path) {
    writeBytes(path, {});
}

/** A .gob archive holding `files` (name, contents), in that order. */
inline std::vector<uint8_t> makeGob(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files) {
    constexpr size_t kHeader = 12;
    constexpr size_t kEntry = 136;
    auto set32 = [](std::vector<uint8_t> &out, size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            out[at + i] = uint8_t(v >> (8 * i));
    };
    std::vector<uint8_t> out(kHeader + 4 + files.size() * kEntry);
    std::copy_n("GOB ", 4, out.begin());
    set32(out, 4, 0x14);
    set32(out, 8, kHeader);
    set32(out, kHeader, uint32_t(files.size()));
    for (size_t i = 0; i < files.size(); ++i) {
        const size_t entry = kHeader + 4 + i * kEntry;
        set32(out, entry, uint32_t(out.size()));
        set32(out, entry + 4, uint32_t(files[i].second.size()));
        std::copy(files[i].first.begin(), files[i].first.end(), out.begin() + std::ptrdiff_t(entry + 8));
        out.insert(out.end(), files[i].second.begin(), files[i].second.end());
    }
    return out;
}

/** Appends `v`, little-endian. */
inline void put32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(uint8_t(v >> (8 * i)));
}

/** A file starting with `tag` and then `words`, made `size` bytes long with zeros. */
inline std::vector<uint8_t> makeHeader(const char *tag, const std::vector<uint32_t> &words, size_t size) {
    std::vector<uint8_t> out(tag, tag + 4);
    for (uint32_t w : words)
        put32(out, w);
    out.resize(size);
    return out;
}

/** A colormap with a blend table: a palette of ramps, light levels darkening it. */
inline std::vector<uint8_t> makeColormapFile() {
    std::vector<uint8_t> out = makeHeader("CMP ", {30, 1, 0x3f800000, 0x3f800000, 0x3f800000}, 64);
    for (int i = 0; i < 256; ++i)
        out.insert(out.end(), {uint8_t(i), uint8_t(i * 3), uint8_t(255 - i)});
    for (int level = 0; level < 64; ++level)
        for (int i = 0; i < 256; ++i)
            out.push_back(uint8_t(i * level / 63));
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            out.push_back(uint8_t((a + b) / 2));
    return out;
}

/** An 8-bit picture without a palette: two 16x8 cels of stripes. */
inline std::vector<uint8_t> makePictureFile() {
    std::vector<uint8_t> out = makeHeader("BM  ", {0x46, 0, 1, 2, 0, 0, 0, 0, 8}, 128);
    for (int cel = 0; cel < 2; ++cel) {
        put32(out, 16);
        put32(out, 8);
        for (int p = 0; p < 16 * 8; ++p)
            out.push_back(uint8_t((p % 16) * 16 + cel * 8));
    }
    return out;
}

/** A material of a colour cel and a 16x16 texture with two mip levels. */
inline std::vector<uint8_t> makeMaterialFile() {
    std::vector<uint8_t> out = makeHeader("MAT ", {0x32, 2, 2, 1, 0, 8}, 76);
    put32(out, 0);
    put32(out, 200);
    for (int i = 0; i < 4; ++i)
        put32(out, 0x3f800000);
    put32(out, 8);
    put32(out, 0);
    for (int i = 0; i < 4; ++i)
        put32(out, 0x3f800000);
    out.resize(out.size() + 12);
    put32(out, 0);
    for (uint32_t v : {16u, 16u, 0u, 0u, 0u, 2u})
        put32(out, v);
    for (int p = 0; p < 16 * 16 + 8 * 8; ++p)
        out.push_back(uint8_t(p * 7));
    return out;
}

/** A font of the characters ' ' to 'Z', each 4 pixels wide, 6 high. */
inline std::vector<uint8_t> makeFontFile() {
    std::vector<uint8_t> out = makeHeader("SFNT", {10, 0, 8, 32, 1}, 40);
    out.insert(out.end(), {' ', 0, 'Z', 0});
    const int count = 'Z' - ' ' + 1;
    for (int c = 0; c < count; ++c) {
        put32(out, uint32_t(c * 4));
        put32(out, 4);
    }
    std::vector<uint8_t> strip = makeHeader("BM  ", {0x46, 0, 1, 1, 0, 0, 0, 0, 8}, 128);
    put32(strip, uint32_t(count * 4));
    put32(strip, 6);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < count * 4; ++x)
            strip.push_back(uint8_t((x + y) % 4 == 0 ? 0 : 230 + x % 20));
    out.insert(out.end(), strip.begin(), strip.end());
    return out;
}

/** A string table of three strings. */
inline std::vector<uint8_t> makeStringsFile() {
    const std::string text = "MSGS 5\r\n# a comment\r\n\"GUI_OK\" 0 \"OK\"\r\n\"GUI_CANCEL\" 0 \"Cancel\"\r\n"
                             "\"ERR\" 0 \"First\\nSecond\"\r\nEND\r\n";
    return {text.begin(), text.end()};
}

/**
 * A model with each of the quirks the game's have: a mesh name with a space,
 * a material without a space after its colon, corners with and without a
 * space after their commas, an untextured mesh whose faces name texture
 * vertex 0, and a second, smaller geometry set.
 */
inline std::vector<uint8_t> makeModelFile() {
    const std::string text = "# MODEL 'test.3do'\r\n"
                             "SECTION: HEADER\r\n3DO 2.1\r\n"
                             "SECTION: MODELRESOURCE\r\nMATERIALS 2\r\n0:\ttop.mat\r\n1:SIDE.MAT\r\n"
                             "SECTION: GEOMETRYDEF\r\nRADIUS 0.5\r\nINSERT OFFSET 0 0 0.25\r\nGEOSETS 2\r\n"
                             "GEOSET 0\r\nMESHES 2\r\n"
                             "MESH 0\r\nNAME salad bar\r\nRADIUS 0.5\r\nGEOMETRYMODE 4\r\nLIGHTINGMODE 3\r\n"
                             "TEXTUREMODE 0\r\nVERTICES 4\r\n"
                             "0: -0.5 -0.5 0 0\r\n1: 0.5 -0.5 0 0\r\n2: 0.5 0.5 0 0\r\n3: -0.5 0.5 0 0.5\r\n"
                             "TEXTURE VERTICES 4\r\n0: 0 0\r\n1: 63 0\r\n2: 63 63\r\n3: 0 63\r\n"
                             "VERTEX NORMALS\r\n0: 0 0 1\r\n1: 0 0 1\r\n2: 0 0 1\r\n3: 0 0 1\r\n"
                             "FACES 2\r\n"
                             "#  num:  material:   type:  geo:  light:   tex:  extralight:  verts:\r\n"
                             "0: 0 0x0000 4 3 0 0.0000 4 0,0 1,1 2,2 3,3\r\n"
                             "1: 1 0x0001 4 3 0 0.1000 3 0, 0   1, 2   2, 1\r\n"
                             "FACE NORMALS\r\n0: 0 0 1\r\n1: 0 0 1\r\n"
                             "MESH 1\r\nNAME solid\r\nRADIUS 0.1\r\nGEOMETRYMODE 3\r\nLIGHTINGMODE 3\r\n"
                             "TEXTUREMODE 0\r\nVERTICES 3\r\n0: 0 0 0 0\r\n1: 0.1 0 0 0\r\n2: 0 0.1 0 0\r\n"
                             "TEXTURE VERTICES 0\r\nVERTEX NORMALS\r\n0: 0 0 1\r\n1: 0 0 1\r\n2: 0 0 1\r\n"
                             "FACES 1\r\n0: 0 0x0000 3 3 0 0.0000 3 0, 0 1, 0 2, 0\r\nFACE NORMALS\r\n0: 0 0 1\r\n"
                             "GEOSET 1\r\nMESHES 1\r\nMESH 0\r\nNAME low\r\nRADIUS 0.5\r\nGEOMETRYMODE 3\r\n"
                             "LIGHTINGMODE 3\r\nTEXTUREMODE 0\r\nVERTICES 3\r\n0: 0 0 0 0\r\n1: 1 0 0 0\r\n"
                             "2: 0 1 0 0\r\nTEXTURE VERTICES 0\r\nVERTEX NORMALS\r\n0: 0 0 1\r\n1: 0 0 1\r\n"
                             "2: 0 0 1\r\nFACES 1\r\n0: 0 0x0000 3 3 0 0.0000 3 0, 0 1, 0 2, 0\r\n"
                             "FACE NORMALS\r\n0: 0 0 1\r\n"
                             "SECTION: HIERARCHYDEF\r\nHIERARCHY NODES 2\r\n"
                             "0: 0x0000 0x00001 0 -1 1 -1 1 0 0 2 0 0 0 0 0 0 root\r\n"
                             "1: 0x0000 0x00002 1 0 -1 -1 0 1 0 0 0 90 0 0.1 0 0 arm node\r\n";
    return {text.begin(), text.end()};
}

/** An install's files, the names in the case the installer gives them. */
inline void makeInstall(const stdfs::path &dir) {
    touch(dir / "JK.EXE");
    touch(dir / "Resource/Res2.gob");
    touch(dir / "Resource/Res1hi.gob");
    touch(dir / "Episode/JK1.GOB");
}

/** CD `number` (1 or 2), with its marker file and a cut scene. */
inline void makeCd(const stdfs::path &dir, int number) {
    writeBytes(dir / "GAMEDATA/RESOURCE/JK_.CD", number == 1 ? std::vector<uint8_t>{0xc4, 0x32, 0x92, 0x69}
                                                             : std::vector<uint8_t>{0x84, 0x33, 0x92, 0x69});
    touch(dir / "GAMEDATA/RESOURCE/VIDEO" / (number == 1 ? "01-02A.SMK" : "23A.SMK"));
}

} // namespace reluva::sith::testing
