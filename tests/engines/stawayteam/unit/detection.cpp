// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's unit tests: detection, and importing from the CD.

#include "engines/registry.h"

#include "tests/support/test.h"

#include <algorithm>
#include <fstream>

using namespace reluva;

namespace {

void testRegistered() {
    const MetaEngine *engine = findEngine("stawayteam");
    CHECK(engine != nullptr);
}

// A folder with none of a release's files isn't recognised.
void testEmptyFolder(const std::filesystem::path &tmp) {
    const MetaEngine *engine = findEngine("stawayteam");
    CHECK(engine && engine->detect(tmp).empty());
}

// A release not in the table is found by its files' names alone, as an
// unknown release.
void testUnknownRelease(const std::filesystem::path &tmp) {
    for (const char *name : {"Star Trek.exe", "DATA.dat"})
        std::ofstream(tmp / name) << "x";
    const MetaEngine *engine = findEngine("stawayteam");
    const auto found = engine ? engine->detect(tmp) : std::vector<DetectedGame>{};
    CHECK(found.size() == 1);
    CHECK(!found.empty() && found[0].gameId == "stawayteam" &&
          found[0].description == "Star Trek: Away Team (unknown release)");
}

// The program alone isn't the game: its data is all in DATA.dat.
void testProgramAlone(const std::filesystem::path &tmp) {
    std::ofstream(tmp / "Star Trek.exe") << "x";
    const MetaEngine *engine = findEngine("stawayteam");
    CHECK(engine && engine->detect(tmp).empty());
}

void putFile(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

bool hasFile(const ImportPlan &plan, const std::string &to) {
    return std::any_of(plan.files.begin(), plan.files.end(), [&](const auto &f) { return f.to == to; });
}

// The CD holds the install loose in Setup/, with files Reluva doesn't use.
void testImportFromCd(const std::filesystem::path &tmp) {
    const auto setup = tmp / "cd" / "Setup";
    for (const char *name : {"Star Trek.exe", "DATA.dat", "texture.dat", "binkw32.dll", "DATA/Movies/Intro.bik",
                             "DATA/Movies/Mission 01/Mission 01 Final.bik"})
        putFile(setup / name);
    putFile(tmp / "cd" / "AUTORUN.EXE");
    const MetaEngine *engine = findEngine("stawayteam");
    if (!engine)
        return;
    auto plans = engine->planImport(tmp / "cd");
    CHECK(plans.size() == 1);
    if (plans.empty())
        return;
    const ImportPlan &plan = plans[0];
    CHECK(plan.game.gameId == "stawayteam" && plan.notes.empty());
    CHECK(plan.files.size() == 5);
    CHECK(hasFile(plan, "Star Trek.exe") && hasFile(plan, "DATA.dat") && hasFile(plan, "texture.dat"));
    CHECK(hasFile(plan, "DATA/Movies/Intro.bik") && hasFile(plan, "DATA/Movies/Mission 01/Mission 01 Final.bik"));
    CHECK(plan.addPatchLabel.empty() && plan.addFolderLabel.empty());
    // Copied, the folder is the game.
    CHECK(!runImport(plan, tmp / "library", [](uint64_t, uint64_t) { return true; }));
    CHECK(engine->detect(tmp / "library").size() == 1);
}

// An install without its movies is imported, with a note saying so.
void testImportWithoutMovies(const std::filesystem::path &tmp) {
    for (const char *name : {"Star Trek.exe", "DATA.dat"})
        putFile(tmp / "install" / name);
    const MetaEngine *engine = findEngine("stawayteam");
    const auto plans = engine ? engine->planImport(tmp / "install") : std::vector<ImportPlan>{};
    CHECK(plans.size() == 1 && plans[0].files.size() == 2 && plans[0].notes.size() == 1);
    CHECK(engine && engine->planImport(tmp / "nothing").empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("stawayteam.unit.detection",
           {
               {"registered", [](const std::filesystem::path &) { testRegistered(); }},
               {"empty folder", [](const std::filesystem::path &tmp) { testEmptyFolder(tmp); }},
               {"unknown release", [](const std::filesystem::path &tmp) { testUnknownRelease(tmp); }},
               {"program alone", [](const std::filesystem::path &tmp) { testProgramAlone(tmp); }},
               {"import from CD", [](const std::filesystem::path &tmp) { testImportFromCd(tmp); }},
               {"import without movies", [](const std::filesystem::path &tmp) { testImportWithoutMovies(tmp); }},
           });
