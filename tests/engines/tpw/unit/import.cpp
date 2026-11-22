// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Importing SimCoaster from its CD: a small CD built here, laid out as
// the US one is (docs/engines/tpw/games/simcoaster/README.md).

#include "engines/registry.h"

#include "tests/support/installshield_writer.h"
#include "tests/support/test.h"

#include <fstream>
#include <iterator>
#include <map>

using namespace reluva;

namespace {

using test::CabinetFile;
using test::toBytes;

std::vector<uint8_t> readAll(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The CD: the data group, the movie in a group of its own, a DLL setup
// installs but Reluva leaves, Game.exe and the language's loose folders,
// one of whose files the data group has too.
void makeCd(const std::filesystem::path &cd) {
    std::vector<CabinetFile> files = {
        {"", "ui.wad", toBytes("front end")},
        {"", "Challenges.sam", toBytes("challenges")},
        {"Language\\American", "OBJECT_NAMES.str", toBytes("names")},
        {"global\\Speech", "speechHD.SDT", toBytes("the cabinet's speech")},
        {"", "", {}},
        {"", "TPinc30.avi", toBytes("movie"), false},
        {"", "QMixer.dll", toBytes("mixer")},
    };
    test::writeCabinet({cd}, files, {{"Data", 0, 4}, {"Training AVI", 5, 5}, {"Program DLLs", 6, 6}}, 0x0100600c);
    test::writeFile(cd / "Game.exe", toBytes("program"));
    test::writeFile(cd / "Speech" / "American" / "speechHD.SDT", toBytes("the CD's speech"));
    test::writeFile(cd / "Speech" / "American" / "vssver.scc", toBytes("source control"));
    test::writeFile(cd / "CoasterData" / "American" / "Alpine Ace.cos", toBytes("coaster"));
    test::writeFile(cd / "PostCardData" / "American" / "postcard" / "stamp.tga", toBytes("stamp"));
    test::writeFile(cd / "PostCardData" / "American" / "My Postcards" / "index.html", toBytes("album"));
}

void testImport(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("tpw");
    if (!engine)
        return test::fail("tpw isn't registered");
    makeCd(tmp / "cd");
    const std::vector<ImportPlan> plans = engine->planImport(tmp / "cd");
    CHECK(plans.size() == 1);
    if (plans.size() != 1)
        return;
    const ImportPlan &plan = plans[0];
    CHECK(plan.game.gameId == "simcoaster" && plan.game.description == "SimCoaster (unknown release)");
    CHECK(plan.notes.empty());
    std::map<std::string, std::string> wanted = {
        {"Data/ui.wad", "front end"},
        {"Data/Challenges.sam", "challenges"},
        {"Data/Language/American/OBJECT_NAMES.str", "names"},
        {"Data/global/Speech/speechHD.SDT", "the CD's speech"},
        {"TPinc30.avi", "movie"},
        {"Game.exe", "program"},
        {"Data/my coasters/Alpine Ace.cos", "coaster"},
        {"Data/postcard/stamp.tga", "stamp"},
        {"My Postcards/index.html", "album"},
    };
    CHECK(plan.files.size() == wanted.size());
    CHECK(!runImport(plan, tmp / "game", [](uint64_t, uint64_t) { return true; }));
    for (const auto &[to, bytes] : wanted)
        CHECK(readAll(tmp / "game" / to) == toBytes(bytes));
    CHECK(!std::filesystem::exists(tmp / "game" / "QMixer.dll"));
    const auto found = engine->detect(tmp / "game");
    CHECK(found.size() == 1 && found[0].gameId == "simcoaster");
}

// Without Game.exe beside it, the cabinet isn't taken for the game's.
void testNotTheGame(const std::filesystem::path &tmp) {
    makeCd(tmp / "cd");
    std::filesystem::remove(tmp / "cd" / "Game.exe");
    const MetaEngine *engine = findEngine("tpw");
    CHECK(engine && engine->planImport(tmp / "cd").empty());
}

// A loose folder missing (a copy of the CD made without it) is said.
void testFolderMissing(const std::filesystem::path &tmp) {
    makeCd(tmp / "cd");
    std::filesystem::remove_all(tmp / "cd" / "CoasterData");
    const MetaEngine *engine = findEngine("tpw");
    const auto plans = engine ? engine->planImport(tmp / "cd") : std::vector<ImportPlan>{};
    CHECK(plans.size() == 1 && plans[0].notes.size() == 1 &&
          plans[0].notes[0].find("CoasterData/American") != std::string::npos);
}

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.import", {
                                  {"from the CD", [](const std::filesystem::path &tmp) { testImport(tmp); }},
                                  {"not the game", [](const std::filesystem::path &tmp) { testNotTheGame(tmp); }},
                                  {"folder missing", [](const std::filesystem::path &tmp) { testFolderMissing(tmp); }},
                              });
