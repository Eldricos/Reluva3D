// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco's unit tests: a copy's files, the resource names in the
// executable's string table, how a picture is cut into frames, and the
// save folder's LEGO.INI.

#include "engines/legoloco/formats/object_definition.h"
#include "engines/legoloco/game_files.h"
#include "engines/legoloco/host/game_process.h"

#include "tests/engines/legoloco/install.h"
#include "tests/support/test.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

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

std::string readText(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

// The save folder's LEGO.INI: made from the copy's, its folders C:'s; then
// kept, with what was written into it, its folders made C:'s again.
void testSavedIni(const std::filesystem::path &tmp) {
    const std::filesystem::path game = tmp / "game";
    const std::filesystem::path save = tmp / "save";
    std::filesystem::create_directories(game / "Exe");
    std::ofstream(game / "Exe" / "LEGO.INI", std::ios::binary)
        << "[DIRECTORIES]\r\nRes=d:\\loco\\art-res\r\n[Locale]\r\nLanguage=DANISH\r\n";
    // Without the program the load stops, the INI written already.
    CHECK(bool(GameProcess(game, save).load()));
    const std::filesystem::path ini = save / "Exe" / "LEGO.INI";
    CHECK(readText(ini) == "[DIRECTORIES]\r\nRes=C:\\art-res\r\n[Locale]\r\nLanguage=DANISH\r\n");
    std::ofstream(ini, std::ios::binary | std::ios::app) << "[USER]\r\nName=Bea\r\n";
    std::ofstream(game / "Exe" / "LEGO.INI", std::ios::binary) << "[Locale]\r\nLanguage=ENGLISH\r\n";
    CHECK(bool(GameProcess(game, save).load()));
    CHECK(readText(ini) == "[DIRECTORIES]\r\nRes=C:\\art-res\r\n[Locale]\r\nLanguage=DANISH\r\n[USER]\r\nName=Bea\r\n");
}

} // namespace

static const reluva::test::Suite
    kSuite("legoloco.unit.game_files",
           {
               {"resource names", [](const std::filesystem::path &) { testResourceNames(); }},
               {"open", [](const std::filesystem::path &tmp) { testOpen(tmp); }},
               {"no executable", [](const std::filesystem::path &tmp) { testNoExecutable(tmp); }},
               {"frame width", [](const std::filesystem::path &) { testFrameWidth(); }},
               {"saved ini", [](const std::filesystem::path &tmp) { testSavedIni(tmp / "ini"); }},
           });
