// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Windows programs' resources: a string table read from a DLL's
// resources (readPeStrings), and Windows Help files (.hlp): topics, text
// and pictures.
//
//   reluva-tests common.unit.windows_resources

#include "common/pe.h"
#include "common/winhelp.h"
#include "tests/support/test.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace reluva;

namespace {

std::vector<uint8_t> stringTableDll() {
    std::vector<uint8_t> f(0x400, 0);
    auto put16 = [&](size_t at, uint32_t v) {
        f[at] = uint8_t(v);
        f[at + 1] = uint8_t(v >> 8);
    };
    auto put32 = [&](size_t at, uint32_t v) {
        put16(at, v & 0xffff);
        put16(at + 2, v >> 16);
    };
    f[0] = 'M';
    f[1] = 'Z';
    put32(0x3c, 0x80);
    put32(0x80, 0x00004550);
    put16(0x84, 0x14c);               // i386
    put16(0x86, 1);                   // a section
    put16(0x94, 224);                 // the optional header's size
    put16(0x98, 0x10b);               // PE32
    put32(0x98 + 32, 0x1000);         // section alignment
    put32(0x98 + 36, 0x200);          // file alignment
    put32(0x98 + 56, 0x2000);         // image size
    put32(0x98 + 60, 0x200);          // headers' size
    put32(0x98 + 92, 16);             // data directories
    put32(0x98 + 96 + 2 * 8, 0x1000); // the resource directory
    put32(0x98 + 96 + 2 * 8 + 4, 0x100);
    const size_t sec = 0x98 + 224;
    std::memcpy(&f[sec], ".rsrc", 5);
    put32(sec + 8, 0x200);   // virtual size
    put32(sec + 12, 0x1000); // RVA
    put32(sec + 16, 0x200);  // raw size
    put32(sec + 20, 0x200);  // raw offset
    // The tree: type 6 -> name 34 -> language 1033 -> a data entry.
    const size_t r = 0x200;
    put16(r + 14, 1);
    put32(r + 16, 6);
    put32(r + 20, 0x80000000 | 0x18);
    put16(r + 0x18 + 14, 1);
    put32(r + 0x18 + 16, 34);
    put32(r + 0x18 + 20, 0x80000000 | 0x30);
    put16(r + 0x30 + 14, 1);
    put32(r + 0x30 + 16, 1033);
    put32(r + 0x30 + 20, 0x48); // the data entry
    put32(r + 0x48, 0x1060);    // the strings' RVA
    put32(r + 0x48 + 4, 0x40);
    // Sixteen strings: "" , "Go!", then "Fünf" at 543, the rest empty.
    size_t at = r + 0x60;
    for (int k = 0; k < 16; ++k) {
        const std::u16string s = k == 1 ? u"Go!" : k == 15 ? u"F\u00fcnf" : u"";
        put16(at, uint32_t(s.size()));
        at += 2;
        for (char16_t c : s) {
            put16(at, c);
            at += 2;
        }
    }
    return f;
}

void testPeStrings() {
    const std::vector<uint8_t> dll = stringTableDll();
    PeImage pe;
    CHECK(!parsePe(dll, pe));
    CHECK(pe.resourceRva == 0x1000 && pe.sections.size() == 1);
    std::map<uint32_t, std::string> strings;
    CHECK(!readPeStrings(dll, pe, strings));
    CHECK(strings.size() == 2 && strings[529] == "Go!" && strings[543] == "F\xc3\xbcnf");
    pe.resourceRva = 0;
    CHECK(readPeStrings(dll, pe, strings));
}

// A Windows Help file made here: its directory (a B+ tree of one leaf),
// |SYSTEM, and |TOPIC uncompressed with one topic of two records.
void testWinHelp() {
    // The help compiler's LZ77: 'a' as it is, then three more from one back.
    const uint8_t packed[] = {0x02, 'a', 0x00, 0x00, 'b'};
    const std::vector<uint8_t> unpacked = winhelp::decompressLz77(packed, sizeof packed);
    CHECK(std::string(unpacked.begin(), unpacked.end()) == "aaaab");

    std::vector<uint8_t> f(16, 0);
    auto u8 = [&](uint32_t v) { f.push_back(uint8_t(v)); };
    auto u16 = [&](uint32_t v) { u8(v & 0xFF), u8(v >> 8 & 0xFF); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF), u16(v >> 16); };
    auto bytes = [&](const std::string &s) { f.insert(f.end(), s.begin(), s.end()); };
    auto fileHeader = [&](uint32_t used) { u32(used), u32(used), u8(0); };

    const uint32_t system = uint32_t(f.size());
    fileHeader(12);
    u16(0x036C), u16(33), u16(1), u32(0), u16(0); // version 4 (minor 33), not compressed

    // The topic: its header record (title "Driving"), then its text:
    // "Hello" (a font) " world" (paragraph's end) "" (the end), with a
    // Windows apostrophe.
    const uint32_t topic = uint32_t(f.size());
    const std::string ld2a("Driving\0", 8), ld2b("Hello\0 world\x92s\0\0", 16);
    const std::vector<uint8_t> ld1b = {0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
                                       0x00, 0x00, 0x80, 0x01, 0x00, 0x82, 0xFF};
    const uint32_t first = 12, second = first + 21 + 28 + uint32_t(ld2a.size());
    const uint32_t topicBytes = 12 + (second - 12) + 21 + uint32_t(ld1b.size() + ld2b.size());
    fileHeader(topicBytes);
    u32(0xFFFFFFFF), u32(first), u32(first); // the block's header
    u32(21 + 28 + uint32_t(ld2a.size())), u32(uint32_t(ld2a.size())), u32(0xFFFFFFFF), u32(second), u32(21 + 28);
    u8(0x02);
    u32(0), u32(0xFFFFFFFF), u32(0xFFFFFFFF), u32(0), u32(0xFFFFFFFF), u32(0xFFFFFFFF), u32(0xFFFFFFFF);
    bytes(ld2a);
    u32(21 + uint32_t(ld1b.size() + ld2b.size())), u32(uint32_t(ld2b.size())), u32(first), u32(0xFFFFFFFF),
        u32(21 + uint32_t(ld1b.size()));
    u8(0x20);
    f.insert(f.end(), ld1b.begin(), ld1b.end());
    bytes(ld2b);

    // A picture: one 2 x 2 DIB of 8 bits a pixel, two colours, its rows
    // (bottom first, 4 bytes each) packed in runs.
    const uint32_t bm0 = uint32_t(f.size());
    const std::vector<uint8_t> runs = {0x82, 1, 0, 2, 0, 0x82, 0, 1, 2, 0}; // 1 0 | 0 0 ; 0 1 | 0 0
    fileHeader(uint32_t(8 + 28 + 8 + runs.size()));
    u16(0x506C), u16(1), u32(8);
    u8(6), u8(1);                           // a DIB, packed in runs
    u16(0), u16(0);                         // resolution
    u8(2), u8(16);                          // 1 plane, 8 bits
    u16(4), u16(4), u16(4), u16(0);         // 2 x 2, 2 colours used, 0 important
    u16(uint32_t(runs.size()) * 2), u16(0); // the packed size, no hot spots
    u32(2 + 18 + 8 + 8), u32(0);            // the data after the colours
    u32(0x0000FF), u32(0x00FF00);           // blue, green (B, G, R, 0)
    f.insert(f.end(), runs.begin(), runs.end());
    CHECK(winhelp::unpackRuns(runs.data(), runs.size()) == std::vector<uint8_t>({1, 0, 0, 0, 0, 1, 0, 0}));

    const uint32_t directory = uint32_t(f.size());
    fileHeader(38 + 1024);
    u16(0x293B), u16(0x0402), u16(1024);
    bytes(std::string("z4\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16));
    u16(0), u16(0), u16(0), u16(0xFFFF), u16(1), u16(1), u32(3);
    const size_t page = f.size();
    u16(0), u16(3), u16(0xFFFF), u16(0xFFFF);
    bytes(std::string("|SYSTEM\0", 8)), u32(system);
    bytes(std::string("|TOPIC\0", 7)), u32(topic);
    bytes(std::string("|bm0\0", 5)), u32(bm0);
    f.resize(page + 1024, 0);

    const uint32_t header[4] = {0x00035F3F, directory, 0xFFFFFFFF, uint32_t(f.size())};
    for (int i = 0; i < 16; ++i)
        f[size_t(i)] = uint8_t(header[i / 4] >> (i % 4 * 8));

    std::vector<winhelp::Topic> topics;
    CHECK(!winhelp::readHelp(f, topics));
    CHECK(topics.size() == 1);
    if (topics.size() == 1) {
        CHECK(topics[0].title == "Driving");
        CHECK(topics[0].text == "Hello world's");
    }
    // The picture: the bottom row first, so (0, 0) is the second row's.
    winhelp::Picture picture;
    CHECK(!winhelp::readPicture(f, 0, picture) && picture.width == 2 && picture.height == 2);
    if (picture.rgba.size() == 16) {
        CHECK(picture.rgba[0] == 0 && picture.rgba[1] == 0 && picture.rgba[2] == 255); // index 0: blue
        CHECK(picture.rgba[5] == 255 && picture.rgba[4] == 0);                         // index 1: green
        CHECK(picture.rgba[8] == 0 && picture.rgba[9] == 255 && picture.rgba[14] == 255);
    }
    CHECK(winhelp::readPicture(f, 1, picture)); // there's no |bm1
    CHECK(winhelp::plainText("a\x02"
                             "5\x02"
                             "b\x03"
                             "c\x01"
                             "7\x01") == "abc");
    f[0] = 0;
    CHECK(winhelp::readHelp(f, topics)); // not a help file
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.windows_resources",
                                        {
                                            {"string table", [](const std::filesystem::path &) { testPeStrings(); }},
                                            {"help file", [](const std::filesystem::path &) { testWinHelp(); }},
                                        });
