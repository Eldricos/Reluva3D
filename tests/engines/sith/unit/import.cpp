// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: importing Jedi Knight from copies of its CDs, and
// running from what the import makes.
//
//   reluva-tests sith.unit.import

#include "engines/sith/folders.h"
#include "engines/sith/games.h"
#include "engines/sith/import.h"
#include "emulation/x86/cdaudio.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/test.h"

#include <algorithm>
#include <fstream>

using namespace reluva;
using namespace reluva::sith;
using namespace reluva::sith::testing;

namespace {

// CD 1 as it is, the names in capitals.
void makeFullCd1(const stdfs::path &cd) {
    makeCd(cd, 1);
    for (const char *name : {"GAMEDATA/EXE/JK.EXE", "GAMEDATA/EXE/SMACKW32.DLL", "GAMEDATA/MININSTALL/RES1HI.GOB",
                             "GAMEDATA/MININSTALL/RES1LOW.GOB", "GAMEDATA/RESOURCE/RES2.GOB",
                             "GAMEDATA/EPISODE/JK1.GOB", "GAMEDATA/EPISODE/JK1CTF.GOB", "GAMEDATA/EPISODE/JK1MP.GOB",
                             "GAMEDATA/CONTROLS/GRAVIS GAMEPAD PRO.CTL", "JEDI.EXE"})
        touch(cd / name);
}

bool copies(const ImportPlan &plan, const std::string &to) {
    return std::any_of(plan.files.begin(), plan.files.end(), [&](const ImportPlan::File &f) { return f.to == to; });
}

void testPlan(const stdfs::path &tmp) {
    const stdfs::path cd1 = tmp / "Disc 1";
    makeFullCd1(cd1);
    std::vector<ImportPlan> plans = planImport(cd1);
    CHECK(plans.size() == 1);
    if (plans.empty())
        return;
    ImportPlan &plan = plans[0];
    CHECK(plan.game.engineId == "sith");
    CHECK(plan.game.gameId == "jk");
    CHECK(plan.game.description == "Jedi Knight: Dark Forces II (unknown release)");

    // The installer's files, under the install's names; not the launcher
    // nor the smaller sounds.
    for (const char *to : {"JK.EXE", "SMACKW32.DLL", "Resource/Res1hi.gob", "Resource/Res2.gob", "Episode/JK1.GOB",
                           "Episode/JK1CTF.GOB", "Episode/JK1MP.GOB", "Controls/GRAVIS GAMEPAD PRO.CTL",
                           "cd1/GAMEDATA/RESOURCE/JK_.CD", "cd1/GAMEDATA/RESOURCE/VIDEO/01-02A.SMK"})
        CHECK(copies(plan, to));
    CHECK(plan.files.size() == 10);
    CHECK(plan.notes.size() == 1);

    // CD 2 adds its cut scenes, once; CD 1 again, or another folder, adds nothing.
    std::string message;
    const stdfs::path cd2 = tmp / "Disc 2";
    makeCd(cd2, 2);
    CHECK(addToImport(plan, cd2, message));
    CHECK(copies(plan, "cd2/GAMEDATA/RESOURCE/VIDEO/23A.SMK"));
    CHECK(plan.notes.empty());
    CHECK(!addToImport(plan, cd2, message));
    CHECK(message == "CD 2 is being copied already");
    CHECK(!addToImport(plan, cd1, message));
    CHECK(!addToImport(plan, tmp / "nothing", message));
    CHECK(message == "not one of Jedi Knight's CDs, nor a copy of one");

    // What the import makes is recognised, and finds both CDs in itself.
    const stdfs::path install = tmp / "imported";
    CHECK(!runImport(plan, install, nullptr));
    CHECK(detectGames(install).size() == 1);
    const GameFolders f = findFolders(install, {});
    CHECK(f.cds[0] == install / "cd1");
    CHECK(f.cds[1] == install / "cd2");
}

void writeFile(const stdfs::path &path, const std::string &text) {
    stdfs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// Each CD's music comes with it: CD 1's cue sheet and its audio track's
// image, not the data track's; CD 2's ripped tracks; not another cue
// sheet holding only data.
void testMusic(const stdfs::path &tmp) {
    const stdfs::path cd1 = tmp / "Disc 1";
    makeFullCd1(cd1);
    writeFile(cd1 / "JK1.cue", "FILE \"JK1 (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
                               "FILE \"JK1 (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n");
    writeFile(cd1 / "JK1 (Track 1).bin", std::string(2352, 'd'));
    writeFile(cd1 / "JK1 (Track 2).bin", std::string(2352, 'a'));
    writeFile(cd1 / "data.cue", "FILE \"JK1 (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    const stdfs::path cd2 = tmp / "Disc 2";
    makeCd(cd2, 2);
    writeFile(cd2 / "Track02.wav", "");
    writeFile(cd2 / "track3.wav", "");
    writeFile(cd2 / "TRACK04.flac", "");
    writeFile(cd2 / "track05.ogg", "");
    std::vector<ImportPlan> plans = planImport(cd1);
    CHECK(plans.size() == 1);
    if (plans.empty())
        return;
    ImportPlan &plan = plans[0];
    std::string message;
    CHECK(addToImport(plan, cd2, message));
    CHECK(copies(plan, "cd1/JK1.cue") && copies(plan, "cd1/JK1 (Track 2).bin"));
    CHECK(!copies(plan, "cd1/JK1 (Track 1).bin") && !copies(plan, "cd1/data.cue"));
    CHECK(copies(plan, "cd2/Track02.wav") && !copies(plan, "cd2/track3.wav"));
    CHECK(copies(plan, "cd2/TRACK04.flac") && copies(plan, "cd2/track05.ogg"));

    // The imported CD 1 plays its music from the copies.
    const stdfs::path install = tmp / "imported";
    CHECK(!runImport(plan, install, nullptr));
    const x86::CdAudio::Disc disc = x86::CdAudio::discIn(findFolders(install, {}).cds[0]);
    CHECK(disc.tracks == 2 && disc.lengthMs(2) > 0 && disc.load(2));
}

// CD 2 alone, an install, or a CD 1 without its executable isn't imported
// from; one without the sounds' archive is, with a note.
void testNotFrom(const stdfs::path &tmp) {
    makeCd(tmp / "two", 2);
    CHECK(planImport(tmp / "two").empty());

    makeInstall(tmp / "install");
    CHECK(planImport(tmp / "install").empty());

    const stdfs::path cd = tmp / "partial";
    makeFullCd1(cd);
    stdfs::remove(cd / "GAMEDATA/EXE/JK.EXE");
    CHECK(planImport(cd).empty());

    touch(cd / "GAMEDATA/EXE/JK.EXE");
    stdfs::remove(cd / "GAMEDATA/MININSTALL/RES1HI.GOB");
    const std::vector<ImportPlan> plans = planImport(cd);
    CHECK(plans.size() == 1);
    CHECK(!plans.empty() && plans[0].notes.size() == 2 &&
          plans[0].notes[0] == "Not on this copy of the CD, so the game won't run: GAMEDATA/MININSTALL/RES1HI.GOB");
}

} // namespace

static const reluva::test::Suite kSuite("sith.unit.import",
                                        {
                                            {"plan", [](const std::filesystem::path &tmp) { testPlan(tmp); }},
                                            {"music", [](const std::filesystem::path &tmp) { testMusic(tmp); }},
                                            {"not from", [](const std::filesystem::path &tmp) { testNotFrom(tmp); }},
                                        });
