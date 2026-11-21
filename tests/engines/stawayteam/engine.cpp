// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/stawayteam/data/engine-record.txt.
//
//   reluva-tests stawayteam.engine [--update]

#include "common/headless_system.h"
#include "engines/registry.h"

#include "tests/support/record.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

// The headless system with a player who closes the window once the first
// frame is shown.
class QuittingSystem : public HeadlessSystem {
public:
    bool pollEvent(Event &ev) override {
        if (frames() == 0 || _quit)
            return false;
        _quit = true;
        ev.type = EventType::kQuit;
        return true;
    }

private:
    bool _quit = false;
};

// DATA.dat as the game's is: a ZIP whose end record is signed "RE\5\6".
void writeData(const std::filesystem::path &path, const std::vector<test::ZipFile> &files) {
    auto zip = test::buildZip(files);
    zip[zip.size() - 22] = 'R';
    zip[zip.size() - 21] = 'E';
    test::writeFile(path, zip);
}

// A run of the game in `dir`: its error, or "ok", and the frames shown.
std::string play(const MetaEngine &engine, const std::filesystem::path &dir, int &frames) {
    GameTarget target;
    target.target = "stawayteam-test";
    target.engineId = "stawayteam";
    target.gameId = "stawayteam";
    target.description = "Star Trek: Away Team (test)";
    target.path = dir;
    QuittingSystem sys;
    const Error err = engine.create(sys, target)->run();
    frames = sys.frames();
    return err ? err.message : "ok";
}

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("stawayteam");
    if (!engine)
        return test::fail("stawayteam isn't registered");
    const std::filesystem::path empty = tmp / "empty";
    std::filesystem::create_directories(empty);
    rec.add("detect.empty", static_cast<int64_t>(engine->detect(empty).size()));

    // An install of its own: the program, and an archive with the rules.
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    test::writeFile(dir / "Star Trek.exe", test::toBytes("not the game's"));
    writeData(dir / "DATA.dat",
              {{"Rules.txt", test::toBytes("CStarTrekRules\r\n{\r\n\tFirst Map Name=Test Map\r\n}\r\n"), {}}});
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    rec.add("detect.description", found.empty() ? "" : found[0].description);

    // Started, it reads the rules, shows its screen and leaves when closed.
    int frames = 0;
    rec.add("play.result", play(*engine, dir, frames));
    rec.add("play.frames", static_cast<int64_t>(frames));

    // Rules that aren't an object, then none: it says what's wrong.
    writeData(dir / "DATA.dat", {{"Rules.txt", test::toBytes("CStarTrekRules\r\n{\r\n"), {}}});
    rec.add("play.bad rules", play(*engine, dir, frames));
    writeData(dir / "DATA.dat", {{"Other.txt", test::toBytes("x"), {}}});
    rec.add("play.no rules", play(*engine, dir, frames));

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/stawayteam/data/engine-record.txt", "stawayteam.engine",
              "tests/engines/stawayteam/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("stawayteam.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
