// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the Angel Game Engine's readers against files built here,
// following docs/formats/age/, so no game data is needed.

#include "engines/age/ar_archive.h"
#include "engines/age/filesystem.h"
#include "engines/age/games.h"
#include "engines/age/viewers.h"
#include "zip_writer.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::age;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, \
                         __LINE__, #cond);                              \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

using test::toBytes;

struct ArFile {
    std::string name;              // ending in '/' for a folder
    std::vector<uint8_t> data;     // contents
    std::vector<uint8_t> deflated; // if not empty: stored as these bytes
};

void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v[at + size_t(i)] = uint8_t(x >> (8 * i));
}

size_t roundUp(size_t n) { return (n + 2047) / 2048 * 2048; }

// An archive laid out as the retail ones are: the header in the first
// 2048 bytes, then the directory and the names, each padded to 2048, then
// each file's data starting on a 2048-byte boundary.
std::vector<uint8_t> buildAr(const std::vector<ArFile>& files) {
    std::string names;
    std::vector<uint32_t> nameAt;
    for (const ArFile& f : files) {
        nameAt.push_back(uint32_t(names.size()));
        names += f.name;
        names += '\0';
    }
    const size_t dirSize = roundUp(files.size() * 16), namesSize = roundUp(names.size());
    std::vector<uint8_t> out(2048 + dirSize + namesSize);
    std::copy(names.begin(), names.end(), out.begin() + std::ptrdiff_t(2048 + dirSize));
    out[0] = 'D', out[1] = 'A', out[2] = 'V', out[3] = 'E';
    put32(out, 4, uint32_t(files.size()));
    put32(out, 8, uint32_t(dirSize));
    put32(out, 12, uint32_t(namesSize));
    for (size_t i = 0; i < files.size(); ++i) {
        const ArFile& f = files[i];
        const std::vector<uint8_t>& body = f.deflated.empty() ? f.data : f.deflated;
        const size_t at = 2048 + i * 16;
        put32(out, at, nameAt[i]);
        put32(out, at + 4, uint32_t(out.size()));
        put32(out, at + 8, uint32_t(f.data.size()));
        put32(out, at + 12, uint32_t(body.size()));
        out.insert(out.end(), body.begin(), body.end());
        out.resize(roundUp(out.size()));
    }
    return out;
}

// "textures/borg/borgfloor1\n" 12 times, raw-deflated by zlib.
std::string repeatedText() {
    std::string text;
    for (int i = 0; i < 12; ++i)
        text += "textures/borg/borgfloor1\n";
    return text;
}
const std::vector<uint8_t> kDeflated = {0x2b, 0x49, 0xad, 0x28, 0x29, 0x2d, 0x4a, 0x2d, 0xd6, 0x4f,
                                        0xca, 0x2f, 0x4a, 0x07, 0x13, 0x69, 0x39, 0xf9, 0xf9, 0x45,
                                        0x86, 0x5c, 0x25, 0xa3, 0x12, 0xe8, 0x12, 0x00};

void testArchive(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    const auto path = tmp / "test.ar";
    const std::string text = repeatedText();
    test::writeFile(path, buildAr({{"tune/", {}, {}},
                                   {"tune/CVS/", {}, {}},
                                   {"tune/vpbug.info", toBytes("BaseName=vpbug\r\n"), {}},
                                   {"city/sf.cinfo", toBytes(text), kDeflated},
                                   {"empty.txt", {}, {}}}));
    ArArchive ar;
    CHECK(!ar.open(path));
    CHECK(ar.entries().size() == 3); // the folders are left out
    CHECK(ar.list() == (std::vector<std::string>{"tune/vpbug.info", "city/sf.cinfo", "empty.txt"}));
    CHECK(ar.has("TUNE\\VPBUG.INFO") && !ar.has("tune/") && !ar.has("tune"));
    CHECK(ar.entry("city/sf.cinfo")->deflated() && !ar.entry("tune/vpbug.info")->deflated());
    std::vector<uint8_t> data;
    CHECK(!ar.read("tune/vpbug.info", data) && data == toBytes("BaseName=vpbug\r\n"));
    CHECK(!ar.read("City/SF.cinfo", data) && data == toBytes(text));
    CHECK(!ar.read("empty.txt", data) && data.empty());
    CHECK(ar.read("missing.txt", data));

    // Damaged deflate data: the file opens, reading it fails.
    std::vector<uint8_t> bad = kDeflated;
    bad[0] ^= 0xff;
    test::writeFile(path, buildAr({{"a.txt", toBytes(text), bad}}));
    CHECK(!ar.open(path) && ar.read("a.txt", data));

    // Not an archive; a directory running past the end; a file running
    // past the end; a name without its terminating zero.
    test::writeFile(path, std::vector<uint8_t>(4096, 0));
    CHECK(ar.open(path));
    test::writeFile(path, toBytes("DAVE"));
    CHECK(ar.open(path));
    auto archive = buildAr({{"a.txt", toBytes("abc"), {}}});
    put32(archive, 4, 1000); // 1000 entries don't fit the 2048-byte directory
    test::writeFile(path, archive);
    CHECK(ar.open(path));
    archive = buildAr({{"a.txt", toBytes("abc"), {}}});
    put32(archive, 2048 + 12, 1 << 20); // stored size
    test::writeFile(path, archive);
    CHECK(ar.open(path));
    archive = buildAr({{"a.txt", toBytes("abc"), {}}});
    put32(archive, 2048, 2047); // the name starts at the last byte of the names, which isn't zero...
    archive[2048 + 2048 + 2047] = 'x';
    test::writeFile(path, archive);
    CHECK(ar.open(path));
}

void testMount(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    std::vector<std::string> warnings;
    SearchSet set;
    std::filesystem::create_directories(tmp);
    CHECK(mountGame(set, tmp, warnings)); // no archives

    test::writeFile(tmp / "mm2core.ar", buildAr({{"tune/a.txt", toBytes("core"), {}},
                                                 {"tune/b.txt", toBytes("core"), {}},
                                                 {"tune/c.txt", toBytes("core"), {}}}));
    test::writeFile(tmp / "VPMOD.AR", buildAr({{"tune/b.txt", toBytes("mod"), {}},
                                               {"tune/c.txt", toBytes("mod"), {}}}));
    test::writeFile(tmp / "broken.ar", toBytes("not an archive"));
    test::writeFile(tmp / "readme.txt", toBytes("not in an archive"));
    test::writeFile(tmp / "tune" / "c.txt", toBytes("loose"));
    set.clear();
    CHECK(!mountGame(set, tmp, warnings));
    CHECK(warnings.size() == 1); // broken.ar
    CHECK(set.size() == 3);      // two archives and the loose files
    std::vector<uint8_t> data;
    // Later names (in lower case: vpmod after mm2core) win; loose files
    // over every archive.
    CHECK(!set.read("tune/a.txt", data) && data == toBytes("core"));
    CHECK(!set.read("tune/b.txt", data) && data == toBytes("mod"));
    CHECK(!set.read("TUNE/C.TXT", data) && data == toBytes("loose"));
    CHECK(!set.read("readme.txt", data));
}

void testDetection(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    test::writeFile(tmp / "MM2CORE.AR", buildAr({}));
    CHECK(detectFromTable("age", detectionTable(), tmp).empty()); // not the retail file
    CHECK(detectFromTable("age", fallbackTable(), tmp).empty());  // no program
    test::writeFile(tmp / "MIDTOWN2.EXE", toBytes("MZ"));
    const auto found = detectFromTable("age", fallbackTable(), tmp);
    CHECK(found.size() == 1 && found[0].gameId == "mm2");
    CHECK(findGame("mm2") && !findGame("mm1"));
}

void testFileLines() {
    bool isText = false;
    auto lines = fileLines(toBytes("type: a\r\nvehCarSim {\r\n\tMass 1000\r\n}"), isText);
    CHECK(isText);
    CHECK(lines == (std::vector<std::string>{"type: a", "vehCarSim {", "    Mass 1000", "}"}));
    lines = fileLines(toBytes("caf\xe9\n"), isText);
    CHECK(isText && lines.size() == 1 && lines[0] == "caf\xc3\xa9"); // Latin-1 shown as UTF-8

    std::vector<uint8_t> binary = toBytes("PKG3FILE");
    binary.push_back(0);
    binary.push_back(0x7f);
    lines = fileLines(binary, isText);
    CHECK(!isText && lines.size() == 1);
    CHECK(lines[0].rfind("00000000  50 4b 47 33 46 49 4c 45  00 7f", 0) == 0);
    CHECK(lines[0].substr(lines[0].size() - 10) == "PKG3FILE..");
}

} // namespace

int main() {
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_age_tests";
    testArchive(tmp / "archive");
    testMount(tmp / "mount");
    testDetection(tmp / "detect");
    testFileLines();
    std::filesystem::remove_all(tmp);
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all age tests passed\n");
    return 0;
}
