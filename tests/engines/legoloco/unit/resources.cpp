// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: the resource file (resource.RFH and .RFD) and its
// compressed entries, on files built here.

#include "engines/legoloco/formats/resource_file.h"

#include "tests/support/test.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::legoloco;

namespace {

using Bytes = std::vector<uint8_t>;

void put32(Bytes &b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}

// A compressed entry over a three-leaf tree: node 256 is ('a', 'b'), the
// root 257 is (256, 'c'), so 'a' is 00, 'b' 01 and 'c' 1, the bits taken
// from each byte's lowest first.
Bytes pack(const std::string &text) {
    Bytes b;
    put32(b, uint32_t(text.size()));
    put32(b, 257);
    b.resize(8 + 512 * 4, 0);
    auto node = [&](uint32_t n, uint16_t left, uint16_t right) {
        b[8 + n * 4] = uint8_t(left);
        b[9 + n * 4] = uint8_t(left >> 8);
        b[10 + n * 4] = uint8_t(right);
        b[11 + n * 4] = uint8_t(right >> 8);
    };
    node(256, 'a', 'b');
    node(257, 256, 'c');
    std::vector<int> bits;
    for (char c : text) {
        if (c == 'c') {
            bits.push_back(1);
        } else {
            bits.push_back(0);
            bits.push_back(c == 'b' ? 1 : 0);
        }
    }
    const size_t start = b.size();
    b.resize(start + (bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        b[start + i / 8] |= uint8_t(bits[i] << (i % 8));
    return b;
}

struct Entry {
    std::string name;
    Bytes data;
    bool compressed;
};

void writeFile(const std::filesystem::path &path, const Bytes &b) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(b.data()), std::streamsize(b.size()));
}

// resource.RFH and resource.RFD under `dir`, holding `entries` in order.
void writeResourceFile(const std::filesystem::path &dir, const std::vector<Entry> &entries) {
    Bytes rfh;
    Bytes rfd;
    for (const Entry &e : entries) {
        put32(rfh, uint32_t(e.name.size() + 1));
        rfh.insert(rfh.end(), e.name.begin(), e.name.end());
        rfh.push_back(0);
        put32(rfh, uint32_t(e.data.size()));
        put32(rfh, e.compressed ? 1 : 0);
        rfd.insert(rfd.end(), e.data.begin(), e.data.end());
    }
    writeFile(dir / "resource.RFH", rfh);
    writeFile(dir / "resource.RFD", rfd);
}

void testUnpack() {
    Bytes out;
    CHECK(!unpackResource(pack("abcabcca"), out));
    CHECK(std::string(out.begin(), out.end()) == "abcabcca");
}

void testUnpackCutShort() {
    Bytes packed = pack("cccccccccccccccc");
    packed.pop_back();
    Bytes out;
    CHECK(bool(unpackResource(packed, out)));
}

void testUnpackLeafRoot() {
    Bytes packed = pack("abc");
    packed[4] = 'a';
    packed[5] = 0;
    Bytes out;
    CHECK(bool(unpackResource(packed, out)));
}

void testUnpackEmpty() {
    Bytes out{1, 2, 3};
    CHECK(!unpackResource(pack(""), out));
    CHECK(out.empty());
}

void testOpenAndRead(const std::filesystem::path &tmp) {
    writeResourceFile(tmp, {{"ee.ini", Bytes{'x', 'y'}, false},
                            {"roads\\Half-VWint.dat", pack("cab"), true},
                            {"sounds\\a b\\001.wav", Bytes{'R', 'I', 'F', 'F'}, false}});
    ResourceFile rf;
    CHECK(!rf.open(tmp));
    CHECK(rf.entries().size() == 3);
    CHECK(rf.list().size() == 3 && rf.list()[1] == "roads\\Half-VWint.dat");
    CHECK(rf.entries()[1].compressed && rf.entries()[1].offset == 2);
    CHECK(rf.entries()[2].offset == 2 + rf.entries()[1].size);

    Bytes out;
    CHECK(!rf.read("ROADS/half-vwint.DAT", out));
    CHECK(std::string(out.begin(), out.end()) == "cab");
    CHECK(!rf.read("sounds\\a b\\001.wav", out));
    CHECK(std::string(out.begin(), out.end()) == "RIFF");
    CHECK(!rf.readRaw(1, out) && out.size() == rf.entries()[1].size);
    CHECK(!rf.has("roads/missing.dat"));
    CHECK(bool(rf.read("roads/missing.dat", out)));
}

void testIndexCutShort(const std::filesystem::path &tmp) {
    writeResourceFile(tmp, {{"ee.ini", Bytes{'x'}, false}});
    Bytes rfh;
    put32(rfh, 7);
    rfh.insert(rfh.end(), {'e', 'e', '.', 'i', 'n', 'i', 0});
    put32(rfh, 1);
    writeFile(tmp / "resource.RFH", rfh);
    ResourceFile rf;
    CHECK(bool(rf.open(tmp)));
}

void testDataShorterThanIndex(const std::filesystem::path &tmp) {
    writeResourceFile(tmp, {{"ee.ini", Bytes{'x', 'y', 'z'}, false}});
    writeFile(tmp / "resource.RFD", Bytes{'x'});
    ResourceFile rf;
    CHECK(bool(rf.open(tmp)));
}

void testMissingFiles(const std::filesystem::path &tmp) {
    ResourceFile rf;
    CHECK(bool(rf.open(tmp)));
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.resources",
           {
               {"unpack", [](const std::filesystem::path &) { testUnpack(); }},
               {"unpack cut short", [](const std::filesystem::path &) { testUnpackCutShort(); }},
               {"unpack leaf root", [](const std::filesystem::path &) { testUnpackLeafRoot(); }},
               {"unpack empty", [](const std::filesystem::path &) { testUnpackEmpty(); }},
               {"open and read", [](const std::filesystem::path &tmp) { testOpenAndRead(tmp); }},
               {"index cut short", [](const std::filesystem::path &tmp) { testIndexCutShort(tmp); }},
               {"data shorter than index", [](const std::filesystem::path &tmp) { testDataShorterThanIndex(tmp); }},
               {"missing files", [](const std::filesystem::path &tmp) { testMissingFiles(tmp); }},
           });
