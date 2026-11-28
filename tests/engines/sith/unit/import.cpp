// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: importing Jedi Knight and Mysteries of the Sith from
// copies or images of their CDs, and running from what the import makes.
//
//   reluva-tests sith.unit.import

#include "engines/sith/folders.h"
#include "engines/sith/games.h"
#include "engines/sith/import.h"
#include "emulation/x86/cdaudio.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/iso_writer.h"
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

// A CD's image, a cue sheet beside its data track and an audio track's.
void makeImage(const stdfs::path &folder, const std::vector<test::IsoEntry> &files, const std::string &volume) {
    writeBytes(folder / "Disc (Track 01).bin", test::makeIsoImage(files, volume, true));
    writeBytes(folder / "Disc (Track 02).bin", std::vector<uint8_t>(2352 * 4, 0x11));
    writeFile(folder / "Disc.cue", "FILE \"Disc (Track 01).bin\" BINARY\n  TRACK 01 MODE1/2352\n"
                                   "    INDEX 01 00:00:00\nFILE \"Disc (Track 02).bin\" BINARY\n"
                                   "  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n");
}

std::vector<uint8_t> readAll(const stdfs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// Mysteries of the Sith from its CD's image: the installer's files and the
// cut scenes taken off the data track, the music's track and cue sheet
// copied; Jedi Knight's CD 2 added with "Add other disc...", its marker
// alone, as the game asks for it once.
void testMotsImage(const stdfs::path &tmp) {
    const std::vector<uint8_t> mark = {0x29, 0x69, 0x42, 0x3b};
    const std::vector<uint8_t> program = {'M', 'Z', 1, 2, 3};
    std::vector<test::IsoEntry> files = {{"GAMEDATA/RESOURCE/JK_.CD", mark},
                                         {"GAMEDATA/RESOURCE/VIDEO/S1L1ECS.SAN", {5, 6}},
                                         {"GAMEDATA/CONTROLS/GRAVIS GAMEPAD PRO.CTM", {7}},
                                         {"SITH.EXE", {8}}};
    for (const char *name : {"GAMEDATA/EXE/JKM.EXE", "GAMEDATA/EXE/OLHOOK.DLL", "GAMEDATA/MININSTALL/JKMSNDLO.GOO",
                             "GAMEDATA/RESOURCE/JKMRES.GOO", "GAMEDATA/EPISODE/JKM.GOO", "GAMEDATA/EPISODE/JKM_KFY.GOO",
                             "GAMEDATA/EPISODE/JKM_MP.GOO", "GAMEDATA/EPISODE/JKM_SABER.GOO"})
        files.push_back({name, program});
    const stdfs::path image = tmp / "MotS-DiskImage";
    makeImage(image, files, "SITH");
    std::vector<ImportPlan> plans = planImport(image);
    CHECK(plans.size() == 1);
    if (plans.empty())
        return;
    ImportPlan &plan = plans[0];
    CHECK(plan.game.gameId == "mots");
    CHECK(plan.game.description == "Jedi Knight: Mysteries of the Sith (unknown release)");
    for (const char *to : {"JKM.EXE", "OLHOOK.DLL", "Resource/JKMsndLO.goo", "Resource/JKMRES.GOO", "Episode/JKM.GOO",
                           "Episode/JKM_KFY.GOO", "Episode/JKM_MP.GOO", "Episode/JKM_SABER.GOO",
                           "Controls/GRAVIS GAMEPAD PRO.CTM", "cd1/GAMEDATA/RESOURCE/JK_.CD",
                           "cd1/GAMEDATA/RESOURCE/VIDEO/S1L1ECS.SAN", "cd1/Disc.cue", "cd1/Disc (Track 02).bin"})
        CHECK(copies(plan, to));
    CHECK(plan.files.size() == 13);
    CHECK(plan.notes.size() == 1 && plan.notes[0].find("Jedi Knight's CD 2") != std::string::npos);

    // Jedi Knight's CD 1 isn't one of its CDs; its CD 2 adds only its marker.
    std::string message;
    makeFullCd1(tmp / "Disc 1");
    CHECK(!addToImport(plan, tmp / "Disc 1", message));
    CHECK(message == "neither Mysteries of the Sith's CD nor Jedi Knight's CD 2, nor a copy of one");
    makeCd(tmp / "Disc 2", 2);
    CHECK(addToImport(plan, tmp / "Disc 2", message));
    CHECK(copies(plan, "cd2/GAMEDATA/RESOURCE/JK_.CD") && !copies(plan, "cd2/GAMEDATA/RESOURCE/VIDEO/23A.SMK"));
    CHECK(plan.notes.empty());

    // What it makes is recognised, and finds both CDs in itself.
    const stdfs::path install = tmp / "imported";
    CHECK(!runImport(plan, install, nullptr));
    const std::vector<DetectedGame> found = detectGames(install);
    CHECK(found.size() == 1 && found[0].gameId == "mots");
    CHECK(readAll(install / "JKM.EXE") == program);
    const GameFolders f = findFolders(install, {});
    CHECK(f.cds[0] == install / "cd1" && f.cds[1] == install / "cd2");
    const x86::CdAudio::Disc disc = x86::CdAudio::discIn(f.cds[0]);
    CHECK(disc.tracks == 2 && disc.load(2));
}

// Jedi Knight's CDs as images too, CD 2 added from its own; a folder with
// an image of something else isn't a CD.
void testJkImages(const stdfs::path &tmp) {
    std::vector<test::IsoEntry> cd1 = {{"GAMEDATA/RESOURCE/JK_.CD", {0xc4, 0x32, 0x92, 0x69}},
                                       {"GAMEDATA/RESOURCE/VIDEO/01-02A.SMK", {1}}};
    for (const char *name : {"GAMEDATA/EXE/JK.EXE", "GAMEDATA/EXE/SMACKW32.DLL", "GAMEDATA/MININSTALL/RES1HI.GOB",
                             "GAMEDATA/RESOURCE/RES2.GOB", "GAMEDATA/EPISODE/JK1.GOB", "GAMEDATA/EPISODE/JK1CTF.GOB",
                             "GAMEDATA/EPISODE/JK1MP.GOB"})
        cd1.push_back({name, {2}});
    makeImage(tmp / "cd1", cd1, "JEDI_KNIGHT_1");
    makeImage(tmp / "cd2",
              {{"GAMEDATA/RESOURCE/JK_.CD", {0x84, 0x33, 0x92, 0x69}}, {"GAMEDATA/RESOURCE/VIDEO/23A.SMK", {3}}},
              "JEDI_KNIGHT_2");
    makeImage(tmp / "other", {{"README.TXT", {4}}}, "OTHER");
    std::vector<ImportPlan> plans = planImport(tmp / "cd1" / "Disc.cue");
    CHECK(plans.size() == 1 && plans[0].game.gameId == "jk");
    if (plans.empty())
        return;
    std::string message;
    CHECK(!addToImport(plans[0], tmp / "other", message));
    CHECK(addToImport(plans[0], tmp / "cd2", message));
    CHECK(copies(plans[0], "cd2/GAMEDATA/RESOURCE/VIDEO/23A.SMK") && copies(plans[0], "cd2/Disc.cue"));
    CHECK(planImport(tmp / "cd2").empty() && planImport(tmp / "other").empty());
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

static const reluva::test::Suite
    kSuite("sith.unit.import",
           {
               {"plan", [](const std::filesystem::path &tmp) { testPlan(tmp); }},
               {"music", [](const std::filesystem::path &tmp) { testMusic(tmp); }},
               {"not from", [](const std::filesystem::path &tmp) { testNotFrom(tmp); }},
               {"Mysteries of the Sith's image", [](const std::filesystem::path &tmp) { testMotsImage(tmp); }},
               {"Jedi Knight's images", [](const std::filesystem::path &tmp) { testJkImages(tmp); }},
           });
