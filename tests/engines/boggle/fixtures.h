// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/support/pe_resources_fixture.h"
#include <filesystem>
#include <fstream>

namespace reluva::boggle::fixtures {
inline std::string palette() {
    std::string text = "JASC-PAL\r\n0100\r\n256\r\n";
    for (int i = 0; i < 256; ++i)
        text += std::to_string(i) + " " + std::to_string(255 - i) + " " + std::to_string((3 * i) % 256) + "\r\n";
    return text;
}
inline std::string dice(size_t count = 16) {
    std::string text;
    // A deliberately artificial repeated die, not a retail dice distribution.
    for (size_t i = 0; i < count; ++i) text += "Ab,C,D,E,F,G\r\n";
    return text;
}
inline std::string mesh() {
    return "xof 0302txt 0064\nHeader { 1; 0; 1; }\nMesh Triangle {\n"
           "3; 0;0;0;, 2;0;0;, 0;3;0;;\n1; 3;0,1,2;;\n"
           "MeshMaterialList { 1; 1; 0; Material { 0.2;0.4;0.6;1;; 8; 1;1;1;; 0;0;0;; } }\n"
           "MeshNormals {3; 0;0;1;, 0;0;1;, 0;0;1;; 1; 3;0,1,2;; }\n"
           "MeshTextureCoords {3; 0;0;, 1;0;, 0;1;; }\n}";
}
inline std::vector<uint8_t> language() {
    std::array<std::u16string, 16> strings{}; strings[3] = u"Synthetic menu";
    return test::fixtures::resourcePe(test::fixtures::stringBlock(strings));
}
inline std::vector<uint8_t> bitmap() {
    using namespace test::fixtures;
    std::vector<uint8_t> b(58, 0); b[0] = 'B'; b[1] = 'M';
    put32(b, 2, 58); put32(b, 10, 54); put32(b, 14, 40);
    put32(b, 18, 1); put32(b, 22, 1); put16(b, 26, 1); put16(b, 28, 24); put32(b, 34, 4);
    b[54] = 19; b[55] = 11; b[56] = 7;
    return b;
}
inline std::vector<uint8_t> wave() {
    using namespace test::fixtures;
    std::vector<uint8_t> b(48, 0);
    auto tag = [&](size_t at, const char* s) { for (size_t i = 0; i < 4; ++i) b[at + i] = uint8_t(s[i]); };
    tag(0, "RIFF"); put32(b, 4, 40); tag(8, "WAVE"); tag(12, "fmt "); put32(b, 16, 16);
    put16(b, 20, 1); put16(b, 22, 1); put32(b, 24, 8000); put32(b, 28, 16000);
    put16(b, 32, 2); put16(b, 34, 16); tag(36, "data"); put32(b, 40, 4);
    put16(b, 44, 1000); put16(b, 46, uint16_t(-1000));
    return b;
}
inline void write(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}
inline void write(const std::filesystem::path& path, const std::string& text) {
    write(path, std::vector<uint8_t>(text.begin(), text.end()));
}
inline void install(const std::filesystem::path& path) {
    write(path / "PALETTES/demo.PAL", palette());
    write(path / "Dicts/demo.dst", dice());
    write(path / "Scripts/demo.x", mesh());
    write(path / "Language/demo.dll", language());
    write(path / "Bitmaps/demo.bmp", bitmap());
    write(path / "Waves/demo.wav", wave());
}
// A generated flat texture, not a retail letter atlas.
inline std::vector<uint8_t> atlas() {
    auto image = bitmap();
    image.resize(54 + 512 * 512 * 3, 240);
    test::fixtures::put32(image, 2, uint32_t(image.size()));
    test::fixtures::put32(image, 18, 512); test::fixtures::put32(image, 22, 512);
    test::fixtures::put32(image, 34, 512 * 512 * 3);
    return image;
}
inline void classicInstall(const std::filesystem::path& path) {
    std::vector<uint8_t> dict(12);
    const char words[] = "AAA\0AAAA\0";
    test::fixtures::put32(dict, 4, 1); test::fixtures::put32(dict, 8, uint32_t(sizeof(words)));
    dict.insert(dict.end(), words, words + sizeof(words));
    write(path / "Dicts/English.dic", dict);
    std::string diceText; for (size_t i = 0; i < 16; ++i) diceText += "A,A,A,A,A,A\n";
    write(path / "Dicts/EngOrg.dst", diceText);
    write(path / "Palettes/ClsO1Pal.pal", palette());
    write(path / "Textures/Letters.bmp", atlas());
    write(path / "Textures/LettersQu.bmp", atlas());
}
inline void masterInstall(const std::filesystem::path& path) {
    classicInstall(path);
    std::string text; for (size_t i = 0; i < 25; ++i) text += "A,A,A,A,A,A\n";
    write(path / "Dicts/EngMas.dst", text);
    write(path / "Palettes/ClsM1Pal.pal", palette());
}
} // namespace reluva::boggle::fixtures
