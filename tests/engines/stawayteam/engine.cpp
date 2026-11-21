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
#include "engines/viewer_registry.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/record.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;

namespace {

// The headless system with a player who presses keys, one a frame, then
// closes the window; a checksum of the screen is kept as each key is
// pressed, and at the end.
class ScriptedSystem : public HeadlessSystem {
public:
    explicit ScriptedSystem(std::vector<uint32_t> keys = {}) : _keys(std::move(keys)) {}

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        _screen = test::Hash().bytes(s.pixels).hex();
    }
    bool pollEvent(Event &ev) override {
        if (frames() == _lastFrame || _next > _keys.size())
            return false;
        _lastFrame = frames();
        screens.push_back(_screen);
        if (_next == _keys.size()) {
            ++_next;
            ev.type = EventType::kQuit;
            return true;
        }
        ev.type = EventType::kKeyDown;
        ev.key = _keys[_next++];
        return true;
    }

    std::vector<std::string> screens;

private:
    std::vector<uint32_t> _keys;
    size_t _next = 0;
    int _lastFrame = 0; // the first key waits for the first frame
    std::string _screen;
};

// DATA.dat as the game's is: a ZIP whose end record is signed "RE\5\6".
void writeData(const std::filesystem::path &path, const std::vector<test::ZipFile> &files) {
    auto zip = test::buildZip(files);
    zip[zip.size() - 22] = 'R';
    zip[zip.size() - 21] = 'E';
    test::writeFile(path, zip);
}

GameTarget targetFor(const std::filesystem::path &dir) {
    GameTarget target;
    target.target = "stawayteam-test";
    target.engineId = "stawayteam";
    target.gameId = "stawayteam";
    target.description = "Star Trek: Away Team (test)";
    target.path = dir;
    return target;
}

// A run of the game in `dir`: its error, or "ok", and the frames shown.
std::string play(const MetaEngine &engine, const std::filesystem::path &dir, int &frames) {
    ScriptedSystem sys;
    const Error err = engine.create(sys, targetFor(dir))->run();
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
    // Two pictures: a raw 2x2 texture, and a sprite with a blended pixel.
    const auto texture =
        stawayteam::frameFile(2, 2, 0, 0, 0x20, {{{0x00, 0xf8, 0xe0, 0x07}, {0x1f, 0x00, 0xff, 0xff}}}, true);
    const auto sprite =
        stawayteam::frameFile(3, 1, 1, 1, 0x2024, {{{0x81, 0x02, 0xe0, 0x07}}, {{0x80}}, {{0x01, 0x1f, 0x00, 0x82}}});
    const std::vector<test::ZipFile> files = {
        {"Rules.txt", test::toBytes("CStarTrekRules\r\n{\r\n\tFirst Map Name=Test Map\r\n}\r\n"), {}},
        {"Cache/Textures/Test.frm16", texture, {}},
        {"Cache/Models/Icon.frm16", sprite, {}}};
    writeData(dir / "DATA.dat", files);
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    rec.add("detect.description", found.empty() ? "" : found[0].description);

    // Started, it reads the rules, shows its screen and leaves when closed.
    int frames = 0;
    rec.add("play.result", play(*engine, dir, frames));
    rec.add("play.frames", static_cast<int64_t>(frames));

    // The data viewer: the sprite on each background (they show through
    // its transparent and blended pixels), then the texture, zoomed, the
    // next folder's, and without the status line; a checksum of each screen.
    if (const MetaViewer *viewer = findViewer("stawayteam")) {
        ScriptedSystem sys({'b', 'b', key::kRight, 'z', 'f', 'i'});
        const Error err = viewer->create(sys, targetFor(dir))->run();
        rec.add("viewer.result", err ? err.message : "ok");
        for (size_t i = 0; i < sys.screens.size(); ++i)
            rec.add("viewer.screen " + std::to_string(i), sys.screens[i]);
    } else {
        test::fail("stawayteam has no viewer");
    }

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
