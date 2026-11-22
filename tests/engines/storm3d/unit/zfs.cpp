// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D's unit tests: ZFS archives, on archives built here.

#include "common/bytes.h"
#include "engines/storm3d/formats/zfs_archive.h"

#include "tests/support/test.h"

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;
using reluva::storm3d::ZfsArchive;

namespace {

struct File {
    std::string name;
    std::string text;
};

constexpr uint32_t kKey = 0x2e8ed45b;

// An archive as Armada's are laid out: the header, then for each block of
// `perBlock` entries its table and then its files' data, encoded.
std::vector<uint8_t> makeArchive(const std::vector<File> &files, uint32_t perBlock, uint32_t nameLength = 16) {
    const uint32_t entrySize = nameLength + 20;
    std::vector<uint8_t> out(28);
    std::memcpy(out.data(), "ZFSF", 4);
    writeLE32(&out[4], 1);
    writeLE32(&out[8], nameLength);
    writeLE32(&out[12], perBlock);
    writeLE32(&out[16], uint32_t(files.size()));
    writeLE32(&out[20], kKey);
    writeLE32(&out[24], 28);
    size_t previous = 0;
    for (size_t first = 0; first < files.size(); first += perBlock) {
        const size_t block = out.size();
        if (previous)
            writeLE32(&out[previous], uint32_t(block));
        previous = block;
        out.resize(block + 4 + size_t(perBlock) * entrySize);
        for (size_t i = first; i < files.size() && i < first + perBlock; ++i) {
            uint8_t *e = &out[block + 4 + (i - first) * entrySize];
            std::memcpy(e, files[i].name.data(), files[i].name.size());
            std::vector<uint8_t> data(files[i].text.begin(), files[i].text.end());
            ZfsArchive::decode(data, kKey); // XOR both ways
            writeLE32(e + nameLength, uint32_t(out.size()));
            writeLE32(e + nameLength + 4, uint32_t(i));
            writeLE32(e + nameLength + 8, uint32_t(data.size()));
            writeLE32(e + nameLength + 12, 952660953);
            out.insert(out.end(), data.begin(), data.end());
        }
    }
    return out;
}

std::filesystem::path write(const std::filesystem::path &tmp, const std::vector<uint8_t> &bytes) {
    const std::filesystem::path path = tmp / "test.zfs";
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

std::string text(const std::vector<uint8_t> &data) {
    return std::string(data.begin(), data.end());
}

// Files across two blocks are listed in order and read back as written;
// a name filling its 16 bytes and a size that isn't a multiple of 4 too.
void testRead(const std::filesystem::path &tmp) {
    const std::vector<File> files = {
        {"alien.odf", "name = \"alien\"\r\n"},
        {"testobjectives.t", "OBJECTIVES:"},
        {"fed1.tt", "fbase.odf    0\r\n"},
    };
    ZfsArchive zfs;
    CHECK(!zfs.open(write(tmp, makeArchive(files, 2))));
    CHECK(zfs.key() == kKey);
    CHECK(zfs.entries().size() == 3);
    std::vector<uint8_t> data;
    for (size_t i = 0; i < files.size(); ++i) {
        CHECK(zfs.entries()[i].name == files[i].name);
        CHECK(zfs.entries()[i].index == i);
        CHECK(!zfs.read(files[i].name, data) && text(data) == files[i].text);
    }
    CHECK(zfs.has("ALIEN.ODF"));
    CHECK(!zfs.has("alien"));
    CHECK(zfs.read("missing.odf", data));
}

// The last size % 4 bytes are stored as they are.
void testDecodeTail() {
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x00, 0x0d, 0x0a};
    ZfsArchive::decode(data, 0x2e8ed45b);
    CHECK((data == std::vector<uint8_t>{0x5b, 0xd4, 0x8e, 0x2e, 0x0d, 0x0a}));
}

// Damage is refused at open: another magic, a count the tables don't
// match, a file past the end.
void testRefused(const std::filesystem::path &tmp) {
    const std::vector<File> files = {{"a.odf", "a = 1"}, {"b.odf", "b = 2"}};
    ZfsArchive zfs;

    std::vector<uint8_t> bytes = makeArchive(files, 100);
    bytes[0] = 'X';
    CHECK(zfs.open(write(tmp, bytes)));

    bytes = makeArchive(files, 100);
    writeLE32(&bytes[16], 3);
    CHECK(zfs.open(write(tmp, bytes)));

    bytes = makeArchive(files, 100);
    bytes.resize(bytes.size() - 1);
    CHECK(zfs.open(write(tmp, bytes)));

    CHECK(!zfs.open(write(tmp, makeArchive(files, 100))));
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.unit.zfs",
                                        {
                                            {"read", [](const std::filesystem::path &tmp) { testRead(tmp); }},
                                            {"decode tail", [](const std::filesystem::path &) { testDecodeTail(); }},
                                            {"refused", [](const std::filesystem::path &tmp) { testRefused(tmp); }},
                                        });
