// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "tests/support/avi_fixture.h"
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
// A conventional invented six-sided cube, with separate corners per face.
inline std::string cubeMesh(double extent=1) {
    constexpr unsigned faces[6][4]={{1,3,7,5},{0,4,6,2},{2,6,7,3},{0,1,5,4},{4,5,7,6},{0,2,3,1}};
    constexpr int normals[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    std::string text="xof 0302txt 0064\nMesh Cube {24;";
    for(unsigned face=0;face<6;++face)for(unsigned corner=0;corner<4;++corner) {
        const unsigned index=faces[face][corner];
        for(unsigned axis=0;axis<3;++axis)text+=std::to_string((index&(1u<<axis)? .5:-.5)*extent)+";";
        text+=face==5 && corner==3?";":",";
    }
    text+="6;";
    for(unsigned face=0;face<6;++face) {
        text+="4;";for(unsigned corner=0;corner<4;++corner)text+=std::to_string(face*4+corner)+(corner==3?";":",");
        text+=face==5?";":",";
    }
    text+="MeshMaterialList {6;6;0,1,2,3,4,5;";
    for(unsigned face=0;face<6;++face)text+="Material {0.8;0.7;0.6;1;; 8;1;1;1;;0;0;0;;}";
    text+="} MeshNormals {6;";
    for(unsigned face=0;face<6;++face){for(int n:normals[face])text+=std::to_string(n)+";";text+=face==5?";":",";}
    text+="6;";
    for(unsigned face=0;face<6;++face){text+="4;";for(unsigned c=0;c<4;++c)text+=std::to_string(face)+(c==3?";":",");text+=face==5?";":",";}
    text+="} MeshTextureCoords {24;";
    for(unsigned face=0;face<6;++face)for(unsigned corner=0;corner<4;++corner) {
        const unsigned index=faces[face][corner];const unsigned u=face<2?2:0,v=face<2?1:face<4?2:1;
        text+=std::to_string(index&(1u<<u)?1:0)+";"+std::to_string(index&(1u<<v)?1:0)+";";
        text+=face==5 && corner==3?";":",";
    }
    return text+="}}";
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
inline void presentationInstall(const std::filesystem::path& path) {
    masterInstall(path);
    write(path / "Anims/ClsO1All.avi",test::fixtures::classicAvi());
    write(path / "Anims/ClsM1All.avi",test::fixtures::classicAvi());
    size_t i=0;
    for (const char* name : {"Shake","ClkDown","ClkUp","Writing","Seven","Eight","Tick","GameOver"}) {
        auto data = wave(); ++i;
        test::fixtures::put16(data,44,uint16_t(i*1000)); test::fixtures::put16(data,46,uint16_t(-int(i*1000)));
        write(path / "Waves" / (std::string(name)+".wav"),data);
    }
}
// Entirely invented flat panels and indexed animation frames.
inline void artworkInstall(const std::filesystem::path& path) {
    presentationInstall(path);
    for (const auto* prefix : {"ClsO1", "ClsM1"}) {
        for (bool names : {false,true}) {
            const int w=names?497:143,h=names?32:480,stride=(w*3+3)&~3;
            auto b=bitmap();b.resize(54+size_t(stride)*h, names?40:180);
            test::fixtures::put32(b,2,uint32_t(b.size()));test::fixtures::put32(b,18,w);
            test::fixtures::put32(b,22,h);test::fixtures::put32(b,34,stride*h);
            write(path / "Bitmaps" / (std::string(prefix)+(names?"N.bmp":"W.bmp")),b);
        }
        const bool master=std::string(prefix)=="ClsM1";
        const int w=master?80:68,h=master?140:128;
        std::vector<std::vector<uint8_t>> packets;
        for(int frame=0;frame<(master?39:38);++frame) {
            std::vector<uint8_t> packet;
            for(int block=0;block<w*h/16;++block){packet.push_back(uint8_t(frame+1));packet.push_back(0x80);}
            packets.push_back(std::move(packet));
        }
        write(path / "Anims" / (std::string(prefix)+"Tim.avi"),test::fixtures::avi8(w,h,packets));
    }
}
} // namespace reluva::boggle::fixtures
