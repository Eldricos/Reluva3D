// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Need for Speed engine's unit tests of finding its games: each game and
// its CD, the folders and file listings, and the game's files by kind.
//
//   reluva-tests nfs.unit.detection

#include "engines/registry.h"
#include "tests/engines/nfs/fixtures.h"

using namespace reluva::nfs::testing;

namespace {

void testDetection(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "detect/Need For Speed III";
    makeInstall(install);

    // The files exist but aren't the known release's, so only the
    // fallback table knows them.
    CHECK(detectFromTable("nfs", detectionTable(), install).empty());
    auto found = detectFromTable("nfs", fallbackTable(), install);
    CHECK(found.size() == 1);
    if (!found.empty()) {
        CHECK(found[0].gameId == "nfs3");
        CHECK(found[0].engineId == "nfs");
    }

    // The CD is found as the game too (its names are lower case).
    const stdfs::path cd = tmp / "detect/NFS3-CD";
    makeCd(cd);
    CHECK(detectFromTable("nfs", fallbackTable(), cd).size() == 1);

    // Without the first track it isn't the game.
    CHECK(detectFromTable("nfs", fallbackTable(), tmp / "detect").empty());

    CHECK(findGame("nfs3") != nullptr);
    CHECK(findGame("nfs4") == nullptr);
}

void testFolders(const stdfs::path &tmp) {
    const stdfs::path root = tmp / "folders";
    const stdfs::path install = root / "Need For Speed III";
    makeInstall(install);

    // Nothing beside the install: no CD.
    GameFolders f = findFolders(install, {}, "nfs3.exe");
    CHECK(f.install == install);
    CHECK(f.cd.empty());

    // A folder beside it without the executable isn't taken for the CD.
    touch(root / "Music/GameData/Audio/pc/alpirock.mus");
    CHECK(findFolders(install, {}, "nfs3.exe").cd.empty());

    // A copy of the CD beside the install is.
    makeCd(root / "NFS3-CD");
    f = findFolders(install, {}, "nfs3.exe");
    CHECK(f.cd == root / "NFS3-CD");
    CHECK(f.cdSource == "folder beside the install");

    // An empty cdpath is as none.
    CHECK(findFolders(install, {{"cdpath", ""}}, "nfs3.exe").cd == root / "NFS3-CD");

    // cdpath wins, absolute or relative to the install.
    makeCd(root / "Elsewhere");
    f = findFolders(install, {{"cdpath", (root / "Elsewhere").string()}}, "nfs3.exe");
    CHECK(f.cd == root / "Elsewhere");
    CHECK(f.cdSource == "cdpath setting");
    f = findFolders(install, {{"cdpath", "../Elsewhere"}}, "nfs3.exe");
    CHECK(f.cd == install / "../Elsewhere");

    // A cdpath without the CD's files gives no CD, rather than another.
    CHECK(findFolders(install, {{"cdpath", (root / "Nowhere").string()}}, "nfs3.exe").cd.empty());

    // Music copied into the install (or the game added from the CD): the
    // install is the CD.
    touch(install / "GameData/Audio/pc/hometech.mus");
    f = findFolders(install, {}, "nfs3.exe");
    CHECK(f.cd == install);
    CHECK(f.cdSource == "install folder");
}

void testListings(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "listings";
    makeInstall(install);

    auto tracks = trackFolders(install);
    CHECK(tracks.size() == 2);
    if (tracks.size() == 2)
        CHECK(tracks[0] == "trk000" && tracks[1] == "trk001");

    // Folders with a car.viv, not Traffic's.
    auto cars = carFolders(install);
    CHECK(cars.size() == 2);
    if (cars.size() == 2)
        CHECK(cars[0] == "Diab" && cars[1] == "f355");

    const stdfs::path cd = tmp / "listings-cd";
    makeCd(cd);
    auto music = filesWithExtension(cd, "GameData/Audio/pc", ".mus");
    CHECK(music.size() == 2); // alpirock.mus and show1.MUS, not the .map
    CHECK(filesWithExtension(cd, "FeData/movies", ".mad").size() == 1);
    CHECK(filesWithExtension(install, "FeData/movies", ".mad").empty());
    CHECK(filesWithExtension(stdfs::path(), "FeData/movies", ".mad").empty());
}

void testGameFiles(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "files/install";
    const stdfs::path cd = tmp / "files/cd";
    touch(install / "FeData/text/text.eng", "eng");
    touch(install / "FeData/art/title.qfs", "install's");
    const Bytes viv = makeViv({{"carp.txt", "Serial"}, {"dash.qfs", "dash"}});
    touch(install / "GameData/CarModel/Diab/car.viv", std::string(viv.begin(), viv.end()));
    // The CD has the same title in its own spelling, and the music.
    touch(cd / "fedata/art/TITLE.QFS", "cd's");
    touch(cd / "gamedata/audio/pc/alpirock.mus", "music");
    touch(cd / "broken.viv", "not an archive");

    GameFolders folders;
    folders.install = install;
    folders.cd = cd;
    GameFiles files;
    files.scan(folders);

    auto names = [](const std::vector<FileRef> &refs) {
        std::vector<std::string> out;
        for (const auto &r : refs)
            out.push_back(r.name);
        return out;
    };
    // The install's title, not the CD's; the .viv's contents after it.
    CHECK(names(files.withExtensions({".qfs"})) ==
          (std::vector<std::string>{"FeData/art/title.qfs", "GameData/CarModel/Diab/car.viv/dash.qfs"}));
    CHECK(names(files.withExtensions({".mus", ".txt"})) ==
          (std::vector<std::string>{"GameData/CarModel/Diab/car.viv/carp.txt", "gamedata/audio/pc/alpirock.mus"}));
    CHECK(names(files.textTables()) == (std::vector<std::string>{"FeData/text/text.eng"}));
    CHECK(names(files.withExtensions({".viv"})).size() == 2); // the broken one is listed, with nothing in it

    Bytes data;
    for (const auto &ref : files.withExtensions({".qfs", ".txt"})) {
        CHECK(!files.read(ref, data));
        const std::string s(data.begin(), data.end());
        CHECK(s == "install's" || s == "dash" || s == "Serial");
    }
    FileRef missing{"x", install / "GameData/CarModel/Diab/car.viv", "nothing.bin"};
    CHECK(files.read(missing, data));
}

void testNfs2seDetection(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "detect/Need For Speed II SE";
    makeNfs2seInstall(install);
    const stdfs::path cd = tmp / "detect/NFS2SE-CD";
    makeNfs2seCd(cd);

    // Not the known release's files: only the fallback table knows them.
    CHECK(detectFromTable("nfs", detectionTable(), install).empty());
    auto found = detectFromTable("nfs", fallbackTable(), install);
    CHECK(found.size() == 1);
    if (!found.empty())
        CHECK(found[0].gameId == "nfs2se");

    // The CD has both programs, so both of the game's entries match it,
    // but the engine finds one game there.
    CHECK(detectFromTable("nfs", fallbackTable(), cd).size() == 2);
    const MetaEngine *engine = findEngine("nfs");
    CHECK(engine != nullptr);
    if (engine) {
        found = engine->detect(cd);
        CHECK(found.size() == 1);
        if (!found.empty())
            CHECK(found[0].gameId == "nfs2se");
    }

    // An install with only the program for 3dfx cards is the game too.
    touch(tmp / "detect/3dfx/nfs2sea.exe");
    touch(tmp / "detect/3dfx/GameData/Sim/CarData/cardata.viv");
    CHECK(detectFromTable("nfs", fallbackTable(), tmp / "detect/3dfx").size() == 1);

    // Without the cars' data it isn't.
    touch(tmp / "detect/program-only/nfs2sen.exe");
    CHECK(detectFromTable("nfs", fallbackTable(), tmp / "detect/program-only").empty());

    const GameInfo *game = findGame("nfs2se");
    CHECK(game != nullptr);
    if (game) {
        CHECK(game->game == Game::kNfs2Se);
        CHECK(std::string(game->executable) == "nfs2sen.exe");
    }
}

void testNfs2seFolders(const stdfs::path &tmp) {
    const stdfs::path root = tmp / "folders";
    const stdfs::path install = root / "Need For Speed II SE";
    makeNfs2seInstall(install);
    const GameInfo &game = *findGame("nfs2se");

    // The tracks are on the CD: without one, the game's files aren't all
    // there, and the error says where to put the CD's.
    GameFolders f = findFolders(install, {}, game.executable);
    CHECK(f.cd.empty());
    Error err = checkFolders(game, f);
    CHECK(err);
    CHECK(err.message.find("cdpath") != std::string::npos);

    // A copy of the CD beside the install, found by its program and music.
    makeNfs2seCd(root / "NFS2SE-CD");
    f = findFolders(install, {}, game.executable);
    CHECK(f.cd == root / "NFS2SE-CD");
    CHECK(!checkFolders(game, f));

    // A CD's copy without tracks: the error names both folders.
    const stdfs::path bare = root / "Bare-CD";
    touch(bare / "GAMEDATA/AUDIO/PC/HIMROCK.MUS");
    f = findFolders(install, {{"cdpath", bare.string()}}, game.executable);
    CHECK(f.cd == bare);
    err = checkFolders(game, f);
    CHECK(err);
    CHECK(err.message.find("No tracks") != std::string::npos);

    // NFS III's check is its own: NFS II SE's folders aren't NFS III's.
    CHECK(checkFolders(*findGame("nfs3"), findFolders(install, {}, game.executable)));
}

void testNfs2seListings(const stdfs::path &tmp) {
    const stdfs::path install = tmp / "listings/install";
    const stdfs::path cd = tmp / "listings/cd";
    makeNfs2seInstall(install);
    makeNfs2seCd(cd);
    GameFolders folders;
    folders.install = install;
    folders.cd = cd;

    // A track copied into the install as well as on the CD counts once.
    touch(install / "GameData/Tracks/se/tr00.trk");
    CHECK(nfs2se::tracks(folders) == (std::vector<std::string>{"tr00", "TR02"}));

    // The models in the cars' data, sorted, without the other files.
    CHECK(nfs2se::carModels(folders) == (std::vector<std::string>{"BMW5", "f355"}));
    GameFolders noCars;
    noCars.install = tmp / "listings/empty";
    CHECK(nfs2se::carModels(noCars).empty());

    // The software program from the install, the 3dfx one from the CD.
    const nfs2se::Programs programs = nfs2se::programs(folders);
    CHECK(programs.software == install / "nfs2sen.exe");
    CHECK(programs.glide == cd / "NFS2SEA.EXE");
    CHECK(nfs2se::programs(noCars).software.empty());

    CHECK(filesWithExtension(cd, "GameData/Audio/pc", ".mus").size() == 1);
    CHECK(filesWithExtension(cd, "FeData/pc/movies", ".dct").size() == 1);

    // The string tables: the front end's and the race messages'.
    touch(install / "GameData/DashHud/pc/TEXTSIM.ENG");
    // The race's palette: the loose hud.fsh's entry, not the HUD
    // archive's empty one.
    Bytes palette;
    putShapeHeader(palette, 0x2a, 0, 1, 1, 0, 0, 0, 0);
    palette.insert(palette.end(), {0x30, 0x20, 0x10, 0xff});
    const Bytes hud = makeShapeBank({{"!pal", palette}});
    const Bytes archive = makeViv({{"hud.fsh", ""}});
    touch(install / "GameData/DashHud/pc/HUDDATA.VIV", std::string(archive.begin(), archive.end()));
    touch(install / "GameData/DashHud/pc/HUD.FSH", std::string(hud.begin(), hud.end()));
    GameFiles files;
    files.scan(folders);
    std::vector<std::string> tables;
    for (const FileRef &f : files.textTables())
        tables.push_back(f.name);
    CHECK(tables == (std::vector<std::string>{"FeData/pc/text/TEXT.ENG", "GameData/DashHud/pc/TEXTSIM.ENG"}));
    CHECK(nfs2se::racePalette(files) == (std::vector<uint8_t>{0x10, 0x20, 0x30, 0xff}));
    files.scan(noCars);
    CHECK(nfs2se::racePalette(files).empty());
}

} // namespace

static const reluva::test::Suite
    kSuite("nfs.unit.detection", {
                                     {"detection", [](const stdfs::path &tmp) { testDetection(tmp); }},
                                     {"folders", [](const stdfs::path &tmp) { testFolders(tmp); }},
                                     {"listings", [](const stdfs::path &tmp) { testListings(tmp); }},
                                     {"game files", [](const stdfs::path &tmp) { testGameFiles(tmp); }},
                                     {"nfs2se detection", [](const stdfs::path &tmp) { testNfs2seDetection(tmp); }},
                                     {"nfs2se folders", [](const stdfs::path &tmp) { testNfs2seFolders(tmp); }},
                                     {"nfs2se listings", [](const stdfs::path &tmp) { testNfs2seListings(tmp); }},
                                 });
