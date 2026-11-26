// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: telling each game's CDs apart, and finding them for
// an install.
//
//   reluva-tests sith.unit.folders

#include "engines/sith/folders.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/test.h"

using namespace reluva::sith;
using namespace reluva::sith::testing;

namespace {

const GameInfo &jk() {
    return *findGame("jk");
}

void testCdNumber(const stdfs::path &tmp) {
    makeCd(tmp / "one", 1);
    makeCd(tmp / "two", 2);
    CHECK(cdNumber(jk(), tmp / "one") == 1);
    CHECK(cdNumber(jk(), tmp / "two") == 2);
    CHECK(cdNumber(jk(), tmp / "none") == 0);

    // Another marker, or one of another length, is neither CD.
    writeBytes(tmp / "other/GAMEDATA/RESOURCE/JK_.CD", {0x00, 0x00, 0x00, 0x00});
    CHECK(cdNumber(jk(), tmp / "other") == 0);
    writeBytes(tmp / "long/GAMEDATA/RESOURCE/JK_.CD", {0xc4, 0x32, 0x92, 0x69, 0x00});
    CHECK(cdNumber(jk(), tmp / "long") == 0);

    // Names in lower case, as a copy may have them.
    writeBytes(tmp / "lower/gamedata/resource/jk_.cd", {0x84, 0x33, 0x92, 0x69});
    CHECK(cdNumber(jk(), tmp / "lower") == 2);
}

// Mysteries of the Sith's own CD is its first; its second is Jedi
// Knight's CD 2, and Jedi Knight's CD 1 is none of its CDs.
void testMotsCds(const stdfs::path &tmp) {
    const GameInfo &mots = *findGame("mots");
    makeMotsCd(tmp / "mots");
    makeCd(tmp / "one", 1);
    makeCd(tmp / "two", 2);
    CHECK(cdNumber(mots, tmp / "mots") == 1);
    CHECK(cdNumber(mots, tmp / "two") == 2);
    CHECK(cdNumber(mots, tmp / "one") == 0);
    CHECK(cdNumber(jk(), tmp / "mots") == 0);

    // The install's program tells the game, and so its CDs.
    const stdfs::path install = tmp / "Mysteries of the Sith";
    makeMotsInstall(install);
    GameFolders f = findFolders(install, {});
    CHECK(f.game && std::string(f.game->gameId) == "mots");
    CHECK(f.cds[0] == tmp / "mots");
    CHECK(f.cds[1] == tmp / "two");
    makeInstall(tmp / "Jedi Knight");
    f = findFolders(tmp / "Jedi Knight", {});
    CHECK(f.game && std::string(f.game->gameId) == "jk" && f.cds[0] == tmp / "one");
}

void testBeside(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "Jedi Knight";
    makeInstall(install);
    GameFolders f = findFolders(install, {});
    CHECK(f.install == install);
    CHECK(f.cds[0].empty() && f.cds[1].empty());

    // Copies of the CDs beside the install, whatever they're called.
    makeCd(tmp / "Disc 2", 2);
    makeCd(tmp / "Disc 1", 1);
    f = findFolders(install, {});
    CHECK(f.cds[0] == tmp / "Disc 1");
    CHECK(f.cds[1] == tmp / "Disc 2");
    CHECK(f.cdSources[0] == "folder beside the install");
}

// Copies in the install (as an import makes them) come before those beside it.
void testInInstall(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "Jedi Knight";
    makeInstall(install);
    makeCd(tmp / "Disc 1", 1);
    makeCd(install / "cd1", 1);
    const GameFolders f = findFolders(install, {});
    CHECK(f.cds[0] == install / "cd1");
    CHECK(f.cdSources[0] == "folder in the install");
    CHECK(f.cds[1].empty());
}

void testSettings(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "Jedi Knight";
    makeInstall(install);
    makeCd(tmp / "Disc 1", 1);
    makeCd(tmp / "Disc 2", 2);
    makeCd(tmp / "elsewhere/two", 2);

    // A setting, relative to the install, wins.
    GameFolders f = findFolders(install, {{"cd2path", "../elsewhere/two"}});
    CHECK(f.cds[1] == install / "../elsewhere/two");
    CHECK(f.cdSources[1] == "cd2path setting");
    CHECK(f.cds[0] == tmp / "Disc 1");

    // One naming the other CD isn't used, nor is any other.
    f = findFolders(install, {{"cd1path", (tmp / "Disc 2").string()}});
    CHECK(f.cds[0].empty());
    CHECK(f.cds[1] == tmp / "Disc 2");
}

} // namespace

static const reluva::test::Suite
    kSuite("sith.unit.folders",
           {
               {"CD number", [](const std::filesystem::path &tmp) { testCdNumber(tmp); }},
               {"Mysteries of the Sith's CDs", [](const std::filesystem::path &tmp) { testMotsCds(tmp); }},
               {"beside the install", [](const std::filesystem::path &tmp) { testBeside(tmp); }},
               {"in the install", [](const std::filesystem::path &tmp) { testInInstall(tmp); }},
               {"settings", [](const std::filesystem::path &tmp) { testSettings(tmp); }},
           });
