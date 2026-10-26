// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Tests for the NetImmerse engine's readers against files built here,
// following docs/formats/alphateam/, so no game data is needed.

#include "engines/netimmerse/pac_archive.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::netimmerse;

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

struct PacFile {
    std::string name;
    std::string data;
    uint64_t modified = 0;
    uint32_t flags = 0;
};

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}
void set32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// An archive laid out as the retail ones: header, directory, 4 zero
// bytes, then the files in the directory's order.
std::vector<uint8_t> buildPac(const std::vector<PacFile>& files) {
    std::vector<uint8_t> dir;
    std::vector<size_t> offsetAt;
    for (const PacFile& f : files) {
        dir.insert(dir.end(), f.name.begin(), f.name.end());
        dir.push_back(0);
        put32(dir, f.flags);
        offsetAt.push_back(dir.size());
        put32(dir, 0); // offset, set below
        put32(dir, uint32_t(f.data.size()));
        put32(dir, uint32_t(f.modified));
        put32(dir, uint32_t(f.modified >> 32));
    }
    put32(dir, 0);
    const uint32_t dataStart = uint32_t(20 + dir.size());
    uint32_t at = dataStart;
    for (size_t i = 0; i < files.size(); ++i) {
        set32(dir, offsetAt[i], at);
        at += uint32_t(files[i].data.size());
    }
    std::vector<uint8_t> out = {'P', 'A', 'C', 'K'};
    put32(out, at); // the archive's size
    put32(out, 0);
    put32(out, dataStart);
    put32(out, uint32_t(files.size()));
    out.insert(out.end(), dir.begin(), dir.end());
    for (const PacFile& f : files)
        out.insert(out.end(), f.data.begin(), f.data.end());
    return out;
}

std::filesystem::path writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

std::string asString(const std::vector<uint8_t>& v) { return std::string(v.begin(), v.end()); }

// 2000-08-21 12:00:00 UTC.
constexpr uint64_t kFileTime = 116444736000000000ULL + 966859200ULL * 10000000ULL;

const std::vector<PacFile> kFiles = {
    {"ABADI MT CONDENSED.DDF", "FNT2Abadi", kFileTime, 0},
    {"EMPTY.TXT", "", 0, 0},
    {"M0020.PUZ", "PUZVER 1\r\nNEARFAR 20 10000\r\n", kFileTime + 10000000, 0},
};

void testRead(const std::filesystem::path& tmp) {
    const auto path = writeFile(tmp / "test.pac", buildPac(kFiles));
    PacArchive pac;
    CHECK(!pac.open(path));
    CHECK(pac.entries().size() == 3);
    CHECK(pac.list() == std::vector<std::string>({"ABADI MT CONDENSED.DDF", "EMPTY.TXT", "M0020.PUZ"}));
    CHECK(pac.dataStart() == 20 + (23 + 20) + (10 + 20) + (10 + 20) + 4);

    const PacArchive::Entry* e = pac.entry("abadi mt condensed.ddf");
    CHECK(e && e->offset == pac.dataStart() && e->size == 9 && e->modified == kFileTime);
    CHECK(PacArchive::unixTime(kFileTime) == 966859200);
    CHECK(PacArchive::unixTime(0) == 0);

    std::vector<uint8_t> data;
    CHECK(!pac.read("m0020.puz", data) && asString(data) == kFiles[2].data);
    CHECK(!pac.read("Empty.txt", data) && data.empty());
    CHECK(pac.has("ABADI MT CONDENSED.DDF") && !pac.has("missing.tga"));
    CHECK(pac.read("missing.tga", data));
}

void testSearchSet(const std::filesystem::path& tmp) {
    auto low = std::make_unique<PacArchive>(), high = std::make_unique<PacArchive>();
    CHECK(!low->open(writeFile(tmp / "low.pac", buildPac({{"A.TXT", "low"}, {"B.TXT", "only low"}}))));
    CHECK(!high->open(writeFile(tmp / "high.pac", buildPac({{"A.TXT", "high"}}))));
    SearchSet set;
    set.add(std::move(low), 0);
    set.add(std::move(high), 1);
    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && asString(data) == "high");
    CHECK(!set.read("b.txt", data) && asString(data) == "only low");
}

void testFlags(const std::filesystem::path& tmp) {
    // Listed, but not read, since what the flags mean isn't known.
    PacArchive pac;
    CHECK(!pac.open(writeFile(tmp / "flags.pac", buildPac({{"PACKED.DAT", "xyz", 0, 1}}))));
    CHECK(pac.entries().size() == 1 && pac.entries()[0].flags == 1);
    std::vector<uint8_t> data;
    CHECK(pac.read("packed.dat", data));
}

void testRejectsDamage(const std::filesystem::path& tmp) {
    const std::vector<uint8_t> good = buildPac(kFiles);
    auto fails = [&](std::vector<uint8_t> bytes) {
        PacArchive pac;
        return bool(pac.open(writeFile(tmp / "bad.pac", bytes)));
    };
    CHECK(!fails(good));
    CHECK(fails({}));
    CHECK(fails({'P', 'A', 'C', 'K'}));

    std::vector<uint8_t> b = good;
    b[0] = 'X';
    CHECK(fails(b)); // magic

    b = good;
    b.pop_back();
    CHECK(fails(b)); // shorter than the header says

    b = good;
    set32(b, 12, uint32_t(good.size() + 1));
    CHECK(fails(b)); // data start past the end

    b = good;
    set32(b, 16, 1000);
    CHECK(fails(b)); // more files than the directory can hold

    b = good;
    set32(b, 16, 4);
    CHECK(fails(b)); // the fourth entry would be read from the zero bytes and data

    b = good;
    set32(b, 20 + 23 + 4, 5);
    CHECK(fails(b)); // first file's data inside the directory

    b = good;
    set32(b, 20 + 23 + 8, uint32_t(good.size()));
    CHECK(fails(b)); // first file's data past the end

    // A directory whose last name has no terminating zero.
    std::vector<uint8_t> noZero = {'P', 'A', 'C', 'K'};
    put32(noZero, 20 + 30);
    put32(noZero, 0);
    put32(noZero, 20 + 30);
    put32(noZero, 0);
    noZero.resize(noZero.size() + 30, 'A');
    CHECK(!fails(noZero)); // no files: the name isn't looked at
    set32(noZero, 16, 1);
    CHECK(fails(noZero));
}

} // namespace

int main() {
    const auto tmp = std::filesystem::temp_directory_path() / "reluva_netimmerse_tests";
    std::filesystem::create_directories(tmp);
    testRead(tmp);
    testSearchSet(tmp);
    testFlags(tmp);
    testRejectsDamage(tmp);
    std::filesystem::remove_all(tmp);
    if (failures)
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    else
        std::printf("All tests passed\n");
    return failures ? 1 : 0;
}
