// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Generations' unit tests: the game found in a folder (by its
// release's signatures, or its executable), its CDs, importing it with
// them, and the files then searched as the game runs.

#include "common/md5.h"
#include "engines/registry.h"
#include "engines/stgenerations/detection.h"
#include "engines/stgenerations/formats/resources.h"
#include "tests/engines/stgenerations/support.h"
#include "tests/support/zip_writer.h"

#include <algorithm>

using namespace reluva;
using namespace reluva::stgenerations;

namespace {

// A Windows executable naming the game in UTF-16, as version resources do.
std::vector<uint8_t> makeFakeExe() {
    std::vector<uint8_t> exe(0x200, 0);
    exe[0] = 'M';
    exe[1] = 'Z';
    exe[0x3C] = 0x80;
    exe[0x80] = 'P';
    exe[0x81] = 'E';
    const char* title = "Star Trek Generations";
    for (size_t i = 0; title[i]; i++)
        exe[0x100 + i * 2] = uint8_t(title[i]);
    return exe;
}

const char* const kInstalled[] = {"COMMON.DAT", "OPTIONS.DAT", "AMARGOS1.DAT", "AMARGOS2.DAT", "ANTILIOS.DAT",
                                  "ARVADA1.DAT", "BERSUS.DAT",  "ENTERP.DAT",   "EPSION.DAT",   "GALORN1.DAT",
                                  "GALORN2.DAT", "HALEE1.DAT",  "QURASH.DAT",   "VERID1.DAT"};
const char* const kDisc1[] = {"COMBAT.DAT",       "STELLAR.DAT",      "MUSIC.1",          "MOVIE/MOV_01.AVI",
                              "MOVIE/MOV_02.AVI", "MOVIE/MOV_03.AVI", "MOVIE/MOV_05.AVI", "MOVIE/MOV_07.AVI",
                              "MOVIE/MOV_08.AVI", "MOVIE/MOV_18.AVI"};
const char* const kDisc2[] = {"COMBAT.DAT",       "STELLAR.DAT",      "MUSIC.2",          "MOVIE/MOV_02.AVI",
                              "MOVIE/MOV_06.AVI", "MOVIE/MOV_09.AVI", "MOVIE/MOV_10.AVI", "MOVIE/MOV_13.AVI",
                              "MOVIE/MOV_14.AVI", "MOVIE/MOV_15.AVI", "MOVIE/MOV_16.AVI"};

// The game as its setup installs it, in `dir`; COMMON.DAT a real ZIP.
void writeInstall(const stgtest::TempDir& t, const std::string& dir) {
    for (const char* name : kInstalled)
        t.writeFile(dir + "DATA/" + name, "installed");
    t.writeFile(dir + "DATA/COMMON.DAT", test::buildZip({{"short.txt", test::toBytes("abcabcabcabc"), {}}}));
    t.writeFile(dir + "DATA/3HEALTH.BMP", "a loose bitmap");
    t.writeFile(dir + "sgens.exe", makeFakeExe());
}

// A CD as it is: its setup's files, and GENS/.
void writeDisc(const stgtest::TempDir& t, const std::string& dir, int disc) {
    t.writeFile(dir + "SETUP.EXE", "setup");
    t.writeFile(dir + "GENS/COMMON.DAT", "on the CD");
    for (const char* name : disc == 1 ? std::vector<const char*>(std::begin(kDisc1), std::end(kDisc1))
                                      : std::vector<const char*>(std::begin(kDisc2), std::end(kDisc2)))
        t.writeFile(dir + "GENS/" + name, "cd" + std::to_string(disc));
}

bool plans(const ImportPlan& plan, const std::string& to) {
    return std::any_of(plan.files.begin(), plan.files.end(), [&](const ImportPlan::File& f) { return f.to == to; });
}

std::string read(const ResourceManager& r, const std::string& name, size_t n) {
    std::unique_ptr<SeekableReadStream> s = r.open({"", name});
    return s ? s->readFixedString(uint32_t(n)) : std::string("(none)");
}

} // namespace

static void registered_and_empty() {
    const MetaEngine* engine = findEngine("stgenerations");
    CHECK(engine != nullptr);
    if (!engine)
        return;
    stgtest::TempDir dir;
    dir.writeFile("README.TXT", "nothing here");
    CHECK(engine->detect(dir.path()).empty());
    CHECK(engine->planImport(dir.path()).empty());
}

// A release in the table by its files' signatures; one that isn't, by its
// executable naming the game (STG-Engine's fallback detection).
static void releases() {
    stgtest::TempDir dir;
    writeInstall(dir, "");
    Md5 md5;
    const std::vector<uint8_t> exe = makeFakeExe();
    md5.update(exe.data(), exe.size());
    const std::vector<GameVariant> table = {
        {"stgenerations", "Known release", "en", "windows", {{"SGENS.EXE", md5.hexDigest(), int64_t(exe.size())}}}};
    bool unknown = true;
    std::vector<DetectedGame> found = detectInstall("stgenerations", table, dir.path(), &unknown);
    CHECK(found.size() == 1 && !unknown && found[0].description == "Known release");

    found = detectInstall("stgenerations", {}, dir.path(), &unknown);
    CHECK(found.size() == 1 && unknown && found[0].platform == "windows");
    // Without its executable it isn't the game.
    stgtest::TempDir bare;
    bare.writeFile("DATA/COMMON.DAT", "x");
    CHECK(detectInstall("stgenerations", {}, bare.path()).empty());

    // Through the engine: found, and said to be without its CDs.
    const MetaEngine* engine = findEngine("stgenerations");
    found = engine->detect(dir.path());
    CHECK(found.size() == 1 && found[0].gameId == "stgenerations" &&
          found[0].description.find("without its CDs") != std::string::npos);
}

// An install and both CDs side by side, as copied off the discs: found, and
// imported with what the game needs from each CD.
static void import_with_both_cds() {
    stgtest::TempDir t;
    writeInstall(t, "Generations/");
    writeDisc(t, "Disk 1/", 1);
    writeDisc(t, "Disk 2/", 2);

    const std::vector<GamePart> parts = findGameParts(t.path());
    CHECK_EQUAL(parts.size(), size_t(3));

    const MetaEngine* engine = findEngine("stgenerations");
    std::vector<ImportPlan> found = engine->planImport(t.path());
    CHECK_EQUAL(found.size(), size_t(1));
    if (found.empty())
        return;
    const ImportPlan& plan = found[0];
    CHECK(plans(plan, "sgens.exe") && plans(plan, "DATA/COMMON.DAT") && plans(plan, "DATA/3HEALTH.BMP"));
    CHECK(plans(plan, "disc1/GENS/MUSIC.1") && plans(plan, "disc2/GENS/MUSIC.2"));
    CHECK(plans(plan, "disc1/GENS/STELLAR.DAT") && !plans(plan, "disc2/GENS/STELLAR.DAT")); // the same: once
    CHECK(plans(plan, "disc2/GENS/MOVIE/MOV_16.AVI") && plans(plan, "disc1/GENS/MOVIE/MOV_02.AVI"));
    CHECK(!plans(plan, "disc1/GENS/COMMON.DAT") && !plans(plan, "disc1/SETUP.EXE")); // installed, or the setup's
    CHECK(plan.notes.size() == 1 && plan.notes[0].find("everything") != std::string::npos);
    CHECK(plan.addPatchLabel.empty() && !plan.addFolderLabel.empty());

    // Imported, it's the game with its CDs.
    stgtest::TempDir dest;
    CHECK(!runImport(plan, dest.path() / "stg", [](uint64_t, uint64_t) { return true; }));
    const std::vector<DetectedGame> games = engine->detect(dest.path() / "stg");
    CHECK(games.size() == 1 && games[0].description.find("without") == std::string::npos);
    CHECK_EQUAL(findGameParts(dest.path() / "stg").size(), size_t(3));

    // ... whose files are searched as the game runs: the installed copy
    // first, then each CD, then COMMON.DAT's members by name.
    ResourceManager resources;
    resources.addSearchPaths(dest.path() / "stg", {}, {});
    CHECK(resources.checkDataFiles() && resources.missingDataFiles().empty());
    CHECK_EQUAL(read(resources, "options.dat", 9), std::string("installed"));
    CHECK_EQUAL(read(resources, "movie/mov_16.avi", 3), std::string("cd2"));
    CHECK_EQUAL(read(resources, "MUSIC.1", 3), std::string("cd1"));
    resources.openArchives();
    CHECK(resources.getArchive("common.dat") != nullptr);
    CHECK(resources.getArchive("VERID1.DAT") == nullptr); // not a ZIP here
    CHECK_EQUAL(read(resources, "short.txt", 12), std::string("abcabcabcabc"));
    CHECK_EQUAL(read(resources, "SGENS.EXE", 2), std::string("MZ"));
}

// Started from one CD, the rest added as the launcher's "Add CD or
// installed game..." does.
static void import_piece_by_piece() {
    stgtest::TempDir t;
    writeInstall(t, "install/");
    writeDisc(t, "cd1/", 1);
    writeDisc(t, "cd2/", 2);
    const MetaEngine* engine = findEngine("stgenerations");
    std::vector<ImportPlan> found = engine->planImport(t.path() / "cd2");
    CHECK_EQUAL(found.size(), size_t(1));
    if (found.empty())
        return;
    ImportPlan plan = found[0];
    CHECK(plans(plan, "disc2/GENS/MUSIC.2") && plans(plan, "disc2/GENS/STELLAR.DAT"));
    CHECK(plan.notes.size() == 2); // no install; disc 1's files missing
    std::string message;
    CHECK(engine->addToImport(plan, t.path() / "install", message));
    CHECK(plans(plan, "sgens.exe") && plan.notes.size() == 1);
    CHECK(engine->addToImport(plan, t.path() / "cd1", message));
    CHECK(plans(plan, "disc1/GENS/MUSIC.1") && !plans(plan, "disc1/GENS/STELLAR.DAT")); // from disc 2 already
    CHECK(plan.notes.size() == 1 && plan.notes[0].find("everything") != std::string::npos);
    // Again, or something else: refused, saying why.
    CHECK(!engine->addToImport(plan, t.path() / "cd1", message) && !message.empty());
    stgtest::TempDir other;
    other.writeFile("x.txt", "x");
    CHECK(!engine->addToImport(plan, other.path(), message) && message.find("Generations") != std::string::npos);
}

// The CDs' folders given (STG-Engine's --cd1 and --cd2, the cd1 and cd2
// settings here): searched as an import's are, the installed files first.
static void cd_folders_given() {
    stgtest::TempDir install, cd1, cd2;
    writeInstall(install, "");
    writeDisc(cd1, "", 1);
    writeDisc(cd2, "", 2);
    ResourceManager resources;
    resources.addSearchPaths(install.path(), cd1.path(), {});
    CHECK(!resources.checkDataFiles());
    CHECK(!resources.missingDataFiles().empty() && resources.missingDataFiles()[0] == "MUSIC.2");
    resources.addSearchPaths(install.path(), cd1.path(), cd2.path());
    CHECK(resources.checkDataFiles());
    CHECK_EQUAL(read(resources, "COMMON.DAT", 2), std::string("PK")); // the installed one, not the CD's
    CHECK_EQUAL(read(resources, "movie/mov_16.avi", 3), std::string("cd2"));
    CHECK_EQUAL(resources.getDataDirs().size(), size_t(3));
}

static const reluva::test::Suite suite("stgenerations.unit.detection", {
    {"registered and empty", [](const std::filesystem::path&) { registered_and_empty(); }},
    {"releases", [](const std::filesystem::path&) { releases(); }},
    {"import with both cds", [](const std::filesystem::path&) { import_with_both_cds(); }},
    {"import piece by piece", [](const std::filesystem::path&) { import_piece_by_piece(); }},
    {"cd folders given", [](const std::filesystem::path&) { cd_folders_given(); }},
});
