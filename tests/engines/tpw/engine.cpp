// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Theme Park World as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/tpw/data/engine-record.txt.
//
//   reluva-tests tpw.engine [--update]
//
// So far: the install found as an unknown release of SimCoaster, its Data
// folder mounted, archives and loose files, and the data viewer run on it. Add steps as the engine
// learns to read and run its games (see tests/engines/idtech3/engine.cpp),
// then --update and review the record.

#include "common/events.h"
#include "common/headless_system.h"
#include "engines/registry.h"
#include "engines/tpw/game_files.h"
#include "engines/viewer_registry.h"

#include "tests/engines/tpw/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <fstream>

using namespace reluva;

namespace {

// The viewer's system: Tab at each 100 ms, through every page and round to
// the first, then Esc; it keeps the titles and a checksum of the screens.
struct ViewerRun : HeadlessSystem {
    test::Hash screens;
    std::vector<std::string> titles;
    int keys = 0;
    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels.data(), s.pixels.size());
    }
    void setWindowTitle(const std::string &t) override {
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event &ev) override {
        if (getMillis() < uint64_t(keys + 1) * 100)
            return false;
        ev = Event{};
        ev.type = EventType::kKeyDown;
        ev.key = ++keys < 9 ? key::kTab : key::kEscape;
        return true;
    }
};

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    using namespace tpw::testing;
    std::filesystem::create_directories(dir / "Data");
    for (const char *name : {"Game.exe", "Data/Challenges.sam"})
        std::ofstream(dir / name) << name;
    test::writeFile(dir / "Data" / "ui.wad", makeWad({{"!frame.MD2", toBytes("a frame"), true},
                                                      {"stexture\\button.tga", toBytes("a button")},
                                                      {"qickload.txt", toBytes("button.tga\r\n"), true}}));
    test::writeFile(dir / "Data" / "ui" / "cursors" / "pick.ani", toBytes("a cursor"));
    test::Record rec;
    const MetaEngine *engine = findEngine("tpw");
    if (!engine)
        return test::fail("tpw isn't registered");
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    for (const DetectedGame &g : found)
        rec.add("detect.game", g.gameId + ": " + g.description);

    SearchSet files;
    std::vector<std::string> warnings;
    if (Error err = tpw::mountGame(files, dir, warnings))
        return test::fail(err.message);
    rec.add("files.warnings", int64_t(warnings.size()));
    test::Hash all;
    for (const std::string &name : files.list()) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return test::fail(err.message);
        all.text(name).bytes(data);
        rec.add("files.file", name + " " + std::to_string(data.size()));
    }
    rec.add("files.all", all.hex());

    const MetaViewer *viewer = findViewer("tpw");
    if (!viewer)
        return test::fail("tpw has no viewer");
    ViewerRun sys;
    GameTarget target{"tpw", "tpw", "simcoaster", "SimCoaster (test)", dir, {}, {}};
    const Error err = viewer->create(sys, target)->run();
    rec.add("viewer.result", err ? err.message : "ok");
    for (const std::string &title : sys.titles)
        rec.add("viewer.title", title);
    rec.add("viewer.screens", sys.screens.hex());
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/tpw/data/engine-record.txt", "tpw.engine",
              "tests/engines/tpw/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("tpw.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
