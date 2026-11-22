// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: .gob archives, and the install's set of them.
//
//   reluva-tests sith.unit.gob

#include "engines/sith/formats/gob.h"
#include "engines/sith/game_files.h"

#include "common/bytes.h"
#include "tests/engines/sith/fixtures.h"
#include "tests/support/test.h"

#include <cstring>
#include <string>
#include <utility>

using namespace reluva;
using namespace reluva::sith;
using namespace reluva::sith::testing;

namespace {

std::vector<uint8_t> bytesOf(const std::string &s) {
    return {s.begin(), s.end()};
}

void testRead(const stdfs::path &tmp) {
    const stdfs::path path = tmp / "test.gob";
    writeBytes(path, makeGob({{"misc\\cmp\\dflt.cmp", bytesOf("colormap")}, {"jkl\\01narshadda.jkl", bytesOf("lvl")}}));
    GobArchive gob;
    CHECK(!gob.open(path));
    CHECK(gob.entries().size() == 2);
    CHECK(gob.list()[0] == "misc\\cmp\\dflt.cmp");
    CHECK(gob.describe() == "test.gob");

    // Names are found with either separator and in any case.
    CHECK(gob.has("misc/cmp/dflt.cmp"));
    CHECK(gob.has("JKL\\01NARSHADDA.JKL"));
    CHECK(!gob.has("misc/cmp/other.cmp"));
    std::vector<uint8_t> out;
    CHECK(!gob.read("jkl/01narshadda.jkl", out));
    CHECK(out == bytesOf("lvl"));
    CHECK(gob.read("nothing", out));
}

void testBad(const stdfs::path &tmp) {
    GobArchive gob;
    CHECK(gob.open(tmp / "missing.gob"));

    writeBytes(tmp / "short.gob", bytesOf("GOB "));
    CHECK(gob.open(tmp / "short.gob"));

    std::vector<uint8_t> data = makeGob({{"a.txt", bytesOf("a")}});
    data[4] = 0x15;
    writeBytes(tmp / "version.gob", data);
    CHECK(gob.open(tmp / "version.gob"));

    // A file running past the end.
    data = makeGob({{"a.txt", bytesOf("a")}});
    writeLE32(&data[kGobHeaderSize + 4 + 4], 1000);
    writeBytes(tmp / "past.gob", data);
    CHECK(gob.open(tmp / "past.gob"));
}

void testGameFiles(const stdfs::path &tmp) {
    GameFiles files;
    CHECK(files.open(tmp / "none"));

    const stdfs::path install = tmp / "Jedi Knight";
    writeBytes(install / "Resource/Res2.gob", makeGob({{"misc\\cmp\\dflt.cmp", bytesOf("res2")}}));
    writeBytes(install / "Resource/Res1hi.gob", makeGob({{"ui\\jkstrings.uni", bytesOf("res1")}}));
    writeBytes(install / "Episode/JK1.GOB", makeGob({{"misc\\cmp\\dflt.cmp", bytesOf("episode")}}));
    touch(install / "Episode/notes.txt");
    CHECK(!files.open(install));
    CHECK(files.archives().size() == 3);
    if (files.archives().size() == 3) {
        CHECK(files.archives()[0]->describe() == "Res1hi.gob");
        CHECK(files.archives()[1]->describe() == "Res2.gob");
        CHECK(files.archives()[2]->describe() == "JK1.GOB");
    }
    std::vector<uint8_t> out;
    CHECK(!files.read("misc/cmp/dflt.cmp", out));
    CHECK(out == bytesOf("res2"));
    CHECK(files.find("ui/jkstrings.uni") == files.archives()[0].get());
    CHECK(files.read("nothing", out));
}

} // namespace

static const reluva::test::Suite
    kSuite("sith.unit.gob", {
                                {"read", [](const std::filesystem::path &tmp) { testRead(tmp); }},
                                {"bad", [](const std::filesystem::path &tmp) { testBad(tmp); }},
                                {"game files", [](const std::filesystem::path &tmp) { testGameFiles(tmp); }},
                            });
