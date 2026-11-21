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

#include "tests/common/data/tone440_ogg.h"
#include "tests/engines/stawayteam/support.h"
#include "tests/support/record.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <filesystem>
#include <iterator>
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
// A run of the game in `dir`: its error, or "ok", the frames shown, and a
// checksum of the screen when the window was closed.
std::string play(const MetaEngine &engine, const std::filesystem::path &dir, int &frames,
                 std::string *screen = nullptr) {
    ScriptedSystem sys;
    const Error err = engine.create(sys, targetFor(dir))->run();
    frames = sys.frames();
    if (screen)
        *screen = sys.screens.empty() ? "" : sys.screens.back();
    return err ? err.message : "ok";
}

// Rules.txt with a main menu: a picture of DATA.dat's, a movie the install
// hasn't got, and a song.
const char *const kRules = "CStarTrekRules\r\n{\r\n\tFirst Map Name=Test Map\r\n\tStartup Song=Tone.ogg\r\n"
                           "\tCustom Rules=CStarTrekCustomRules\r\n\t{\r\n\t\tMenu=Array\r\n\t\t{\r\n"
                           "\t\t\tItem Count=1\r\n\t\t\tMenu=CAnimatedMenuInfo\r\n\t\t\t{\r\n"
                           "\t\t\t\tMenu Name=Main Menu\r\n\t\t\t\tMenu Definition=CStarTrekMainMenu\r\n"
                           "\t\t\t\t{\r\n\t\t\t\t\tNernie=Array\r\n\t\t\t\t\t{\r\n"
                           "\t\t\t\t\t\tItem Count=2\r\n\t\t\t\t\t\tNernie=CMovieNernie\r\n"
                           "\t\t\t\t\t\t{\r\n\t\t\t\t\t\t\tPosition=0,91\r\n"
                           "\t\t\t\t\t\t\tMovie to Play=Missing.bik\r\n\t\t\t\t\t\t}\r\n"
                           "\t\t\t\t\t\tNernie=CImageNernie\r\n\t\t\t\t\t\t{\r\n"
                           "\t\t\t\t\t\t\tPosition=8,439\r\n\t\t\t\t\t\t\tImage=Main Menu/Logo\r\n"
                           "\t\t\t\t\t\t}\r\n\t\t\t\t\t}\r\n\t\t\t\t}\r\n\t\t\t}\r\n\t\t}\r\n"
                           "\t}\r\n}\r\n";

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("stawayteam");
    if (!engine)
        return test::fail("stawayteam isn't registered");
    const std::filesystem::path empty = tmp / "empty";
    std::filesystem::create_directories(empty);
    rec.add("detect.empty", static_cast<int64_t>(engine->detect(empty).size()));

    // An install of its own: the program, and an archive with the rules and
    // a file of each kind the viewer shows.
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    test::writeFile(dir / "Star Trek.exe", test::toBytes("not the game's"));
    // Two pictures: a raw 2x2 texture, and a sprite with a blended pixel.
    const auto texture =
        stawayteam::frameFile(2, 2, 0, 0, 0x20, {{{0x00, 0xf8, 0xe0, 0x07}, {0x1f, 0x00, 0xff, 0xff}}}, true);
    const auto sprite =
        stawayteam::frameFile(3, 1, 1, 1, 0x2024, {{{0x81, 0x02, 0xe0, 0x07}}, {{0x80}}, {{0x01, 0x1f, 0x00, 0x82}}});
    using stawayteam::solidFrame;
    const std::vector<test::ZipFile> files = {
        {"Rules.txt", test::toBytes(kRules), {}},
        {"Cache/Interface/Screens/Main Menu/Logo.frm16", sprite, {}},
        {"Cache/Textures/Test.frm16", texture, {}},
        {"Cache/Models/Icon.frm16", sprite, {}},
        {"Cache/Sequences/Blink.seq16",
         stawayteam::sequenceFile({solidFrame(6, 6, 0xf800), solidFrame(8, 8, 0x001f)}, 4.0f),
         {}},
        {"Cache/Models/Test.mdl16", stawayteam::modelFile("Test", 2, 3), {}},
        {"Cache/Fonts/Test.seq16", stawayteam::fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65), {}},
        {"Music/Tone.ogg", std::vector<uint8_t>(std::begin(kTone440Ogg), std::end(kTone440Ogg)), {}},
        {"Levels/Test.RDS", test::toBytes(":Hello\r\nHello, Away Team.\r\n"), {}}};
    writeData(dir / "DATA.dat", files);
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    rec.add("detect.description", found.empty() ? "" : found[0].description);

    // Started, it reads the rules; its movies aren't there to play, so the
    // main menu's screen comes, its picture at its place, until it's closed.
    int frames = 0;
    std::string screen;
    rec.add("play.result", play(*engine, dir, frames, &screen));
    rec.add("play.frames", static_cast<int64_t>(frames));
    rec.add("play.title screen", screen);

    // The data viewer: the menu's sprite on each background (they show
    // through its transparent and blended pixels), then the other sprite,
    // zoomed, the next folder's (the texture), and without the status line; then each page in turn:
    // the sequence stepped, the model turned and its next frame, the font,
    // the sound played (no output here), and the objects, the next file's.
    // A checksum of each screen.
    if (const MetaViewer *viewer = findViewer("stawayteam")) {
        ScriptedSystem sys({'b', 'b', key::kRight, 'z', 'f', 'i', key::kTab, '.', key::kTab, '.', ']', key::kTab,
                            key::kTab, ' ', key::kTab, key::kRight, key::kRight, key::kRight});
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
