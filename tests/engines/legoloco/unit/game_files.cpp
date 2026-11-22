// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: a copy's files, the resource names in the
// executable's string table, and how a picture is cut into frames.

#include "engines/legoloco/formats/object_definition.h"
#include "engines/legoloco/game_files.h"

#include "tests/engines/legoloco/install.h"
#include "tests/support/test.h"

#include <filesystem>
#include <map>

using namespace reluva;
using namespace reluva::legoloco;

namespace {

// Only the strings that are paths are resources' names.
void testResourceNames() {
    std::map<uint32_t, std::string> names;
    CHECK(!readResourceNames(fixture::stringTableExe(65, {{0, "backdrop\\backdrop"}, {1, "Bruce"}, {2, "a\\b\\c"}}),
                             names));
    CHECK(names.size() == 2 && names[1024] == "backdrop\\backdrop" && names[1026] == "a\\b\\c");
    CHECK(bool(readResourceNames(fixture::Bytes(64, 0), names)));
}

void testOpen(const std::filesystem::path &tmp) {
    fixture::makeInstall(tmp);
    GameFiles files;
    CHECK(!files.open(tmp));
    CHECK(files.resourceNames().size() == 2);
    const int post = files.findResource(3164, "bmp");
    CHECK(post >= 0 && files.resources().entries()[size_t(post)].name == "track\\post.bmp");
    CHECK(files.findResource(3165, "wav") >= 0);
    CHECK(files.findResource(3164, "wav") < 0 && files.findResource(3163, "bmp") < 0);
    CHECK(files.findDirectory("savegame").has_value() && files.findFile("backdrop/green.bmp").has_value());

    // Given its art-res, it finds the rest from there.
    GameFiles fromArtRes;
    CHECK(!fromArtRes.open(tmp / "art-res") && fromArtRes.resourceNames().size() == 2);
}

void testNoExecutable(const std::filesystem::path &tmp) {
    fixture::makeInstall(tmp);
    std::filesystem::remove(tmp / "Exe" / "Loco.exe");
    GameFiles files;
    CHECK(bool(files.open(tmp)));
}

// The game's rule: the width over the frames, rounded down.
void testFrameWidth() {
    CHECK(frameWidth(560, 35) == 16);
    CHECK(frameWidth(592, 35) == 16);
    CHECK(frameWidth(384, 20) == 19);
    CHECK(frameWidth(64, 0) == 64);
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.game_files",
           {
               {"resource names", [](const std::filesystem::path &) { testResourceNames(); }},
               {"open", [](const std::filesystem::path &tmp) { testOpen(tmp); }},
               {"no executable", [](const std::filesystem::path &tmp) { testNoExecutable(tmp); }},
               {"frame width", [](const std::filesystem::path &) { testFrameWidth(); }},
           });
