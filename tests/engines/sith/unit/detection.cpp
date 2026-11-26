// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: recognising Jedi Knight's and Mysteries of the Sith's
// installs.
//
//   reluva-tests sith.unit.detection

#include "engines/registry.h"
#include "engines/sith/games.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/test.h"

using namespace reluva;
using namespace reluva::sith;
using namespace reluva::sith::testing;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("sith");
    CHECK(engine != nullptr);
    CHECK(findGame("jk") != nullptr);
    const GameInfo *mots = findGame("mots");
    CHECK(mots && std::string(mots->program) == "JKM.EXE");
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const stdfs::path &tmp) {
    const MetaEngine *engine = findEngine("sith");
    CHECK(engine && engine->detect(tmp).empty());
}

// The files are there but aren't the known release's: only the fallback
// table knows them.
void testUnknownRelease(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "Jedi Knight";
    makeInstall(install);
    CHECK(detectFromTable("sith", detectionTable(), install).empty());
    const std::vector<DetectedGame> found = findEngine("sith")->detect(install);
    CHECK(found.size() == 1);
    if (!found.empty()) {
        CHECK(found[0].engineId == "sith");
        CHECK(found[0].gameId == "jk");
        CHECK(found[0].description == "Jedi Knight: Dark Forces II (unknown release)");
    }
}

// Mysteries of the Sith by its own program and archives.
void testMots(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "Mysteries of the Sith";
    makeMotsInstall(install);
    const std::vector<DetectedGame> found = sith::detectGames(install);
    CHECK(found.size() == 1 && found[0].gameId == "mots" &&
          found[0].description == "Jedi Knight: Mysteries of the Sith (unknown release)");
    CHECK(gameInstalled(install) == findGame("mots"));
    CHECK(gameInstalled(tmp) == nullptr);
}

// Names are matched whatever their case, as on Windows.
void testNamesCase(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "jk";
    touch(install / "jk.exe");
    touch(install / "RESOURCE/RES2.GOB");
    touch(install / "episode/jk1.gob");
    CHECK(sith::detectGames(install).size() == 1);
}

// Without its single-player episode it isn't the game; a CD isn't an
// install (its files are under GAMEDATA).
void testNotTheGame(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "partial";
    touch(install / "JK.EXE");
    touch(install / "Resource/Res2.gob");
    CHECK(sith::detectGames(install).empty());

    const stdfs::path cd = tmp / "cd";
    makeCd(cd, 1);
    touch(cd / "GAMEDATA/EXE/JK.EXE");
    touch(cd / "GAMEDATA/RESOURCE/RES2.GOB");
    touch(cd / "GAMEDATA/EPISODE/JK1.GOB");
    CHECK(sith::detectGames(cd).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("sith.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
               {"names' case", [](const std::filesystem::path &tmp) { testNamesCase(tmp); }},
               {"Mysteries of the Sith", [](const std::filesystem::path &tmp) { testMots(tmp); }},
               {"not the game", [](const std::filesystem::path &tmp) { testNotTheGame(tmp); }},
           });
