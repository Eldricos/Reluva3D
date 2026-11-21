// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: importing the game from its CD, from an install,
// or from both, into one folder it runs from.

#include "engines/legoisland/detection.h"
#include "engines/registry.h"

#include "tests/support/test.h"

#include <algorithm>
#include <fstream>

using namespace reluva;
using namespace reluva::legoisland;

namespace {

void touch(const std::filesystem::path &file) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "not the game's";
}

// The game's own files as the installer copies them, under `dir`, in the
// case `lego` and `data` are spelt.
void writeInstall(const std::filesystem::path &dir, const std::string &lego, const std::string &data) {
    touch(dir / "ISLE.EXE");
    touch(dir / "LEGO1.DLL");
    touch(dir / lego / data / "WORLD.WDB");
    touch(dir / lego / data / "ACT1INF.DTA");
    touch(dir / lego / (lego == "lego" ? "Scripts" : "SCRIPTS") / "NOCD.SI");
}

// The CD as it is: its installer's DATA/DISK, and every archive in
// LEGO/SCRIPTS.
void writeCd(const std::filesystem::path &dir) {
    touch(dir / "SETUP.EXE");
    writeInstall(dir / "DATA" / "DISK", "LEGO", "DATA");
    for (const std::string &name : cdScripts())
        touch(dir / "LEGO" / "SCRIPTS" / name);
}

bool plans(const ImportPlan &plan, const std::string &to) {
    return std::any_of(plan.files.begin(), plan.files.end(), [&](const ImportPlan::File &f) { return f.to == to; });
}

bool notes(const ImportPlan &plan, const std::string &words) {
    return std::any_of(plan.notes.begin(), plan.notes.end(),
                       [&](const std::string &n) { return n.find(words) != std::string::npos; });
}

// The CD alone is all the game needs: its installer's files where the
// installer puts them, its archives beside NOCD.SI, and not the setup.
void testFromCd(const std::filesystem::path &tmp) {
    writeCd(tmp / "cd");
    const MetaEngine *engine = findEngine("legoisland");
    CHECK(engine != nullptr);
    std::vector<ImportPlan> found = engine->planImport(tmp / "cd");
    CHECK(found.size() == 1);
    if (found.empty())
        return;
    const ImportPlan &plan = found[0];
    CHECK(plan.game.gameId == kGameId);
    CHECK(plans(plan, "ISLE.EXE") && plans(plan, "LEGO1.DLL"));
    CHECK(plans(plan, "lego/data/WORLD.WDB") && plans(plan, "lego/data/ACT1INF.DTA"));
    CHECK(plans(plan, "lego/Scripts/ISLE/ISLE.SI") && plans(plan, "lego/Scripts/INTRO.SI"));
    CHECK(!plans(plan, "SETUP.EXE") && !plans(plan, "LEGO/DATA/WORLD.WDB"));
    CHECK(std::count_if(plan.files.begin(), plan.files.end(),
                        [](const ImportPlan::File &f) { return f.to == "lego/Scripts/NOCD.SI"; }) == 1);
    CHECK(plan.notes.size() == 1 && notes(plan, "everything"));
    CHECK(plan.addPatchLabel.empty() && !plan.addFolderLabel.empty());

    // Imported, it's the game, with both parts in one folder.
    CHECK(!runImport(plan, tmp / "game", [](uint64_t, uint64_t) { return true; }));
    const std::vector<DetectedGame> games = engine->detect(tmp / "game");
    CHECK(games.size() == 1 && games[0].gameId == kGameId);
    const std::vector<GamePart> parts = findGameParts(tmp / "game");
    CHECK(parts.size() == 2);
}

// An install without the CD: imported, with a note that the CD's scripts
// are wanted; the CD added after, as "Add CD or installed game..." does.
void testInstallThenCd(const std::filesystem::path &tmp) {
    writeInstall(tmp / "install", "lego", "data");
    touch(tmp / "install" / "save" / "History.gsi");
    writeCd(tmp / "cd");
    const MetaEngine *engine = findEngine("legoisland");
    std::vector<ImportPlan> found = engine->planImport(tmp / "install");
    CHECK(found.size() == 1);
    if (found.empty())
        return;
    ImportPlan plan = found[0];
    CHECK(plans(plan, "LEGO1.DLL") && plans(plan, "save/History.gsi"));
    CHECK(plan.notes.size() == 1 && notes(plan, "SCRIPTS folder isn't here"));
    std::string message;
    CHECK(engine->addToImport(plan, tmp / "cd", message));
    CHECK(plans(plan, "lego/Scripts/ACT3/ACT3.SI"));
    CHECK(plan.notes.size() == 1 && notes(plan, "everything"));
    // Again, or something else: refused, saying why.
    CHECK(!engine->addToImport(plan, tmp / "cd", message) && !message.empty());
    touch(tmp / "other" / "x.txt");
    CHECK(!engine->addToImport(plan, tmp / "other", message) && message.find("LEGO Island") != std::string::npos);
}

// The CD's scripts without the game's own files, then a copy missing
// some archives: each said in a note.
void testWhatsMissing(const std::filesystem::path &tmp) {
    touch(tmp / "scripts" / "LEGO" / "SCRIPTS" / "ISLE" / "ISLE.SI");
    const MetaEngine *engine = findEngine("legoisland");
    std::vector<ImportPlan> found = engine->planImport(tmp / "scripts");
    CHECK(found.size() == 1);
    if (found.empty())
        return;
    const ImportPlan &plan = found[0];
    CHECK(plans(plan, "lego/Scripts/ISLE/ISLE.SI"));
    CHECK(plan.notes.size() == 2 && notes(plan, "own files aren't here") && notes(plan, "not found: ACT2"));
}

// A folder with neither part isn't offered.
void testNothing(const std::filesystem::path &tmp) {
    touch(tmp / "LEGO1.DLL");
    const MetaEngine *engine = findEngine("legoisland");
    CHECK(engine && engine->planImport(tmp).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("legoisland.unit.import",
           {
               {"from the CD", [](const std::filesystem::path &tmp) { testFromCd(tmp); }},
               {"install, then the CD", [](const std::filesystem::path &tmp) { testInstallThenCd(tmp); }},
               {"what's missing", [](const std::filesystem::path &tmp) { testWhatsMissing(tmp); }},
               {"nothing", [](const std::filesystem::path &tmp) { testNothing(tmp); }},
           });
