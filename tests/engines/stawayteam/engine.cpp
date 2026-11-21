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

// A run of the game in `dir`, `keys` pressed: its error, or "ok", the
// frames shown, and a checksum of the screen as each key was pressed and
// when the window was closed, if it was.
std::string play(const MetaEngine &engine, const std::filesystem::path &dir, int &frames,
                 std::vector<std::string> *screens = nullptr, std::vector<uint32_t> keys = {}) {
    ScriptedSystem sys(std::move(keys));
    const Error err = engine.create(sys, targetFor(dir))->run();
    frames = sys.frames();
    if (screens)
        *screens = sys.screens;
    return err ? err.message : "ok";
}

// Rules.txt with a main menu (a picture of DATA.dat's, a movie the
// install hasn't got, its buttons' places) and the confirm dialog, and a
// song; its lines end as the game's do.
std::string rules() {
    const std::string text = R"(CStarTrekRules
{
	First Map Name=Test Map
	Startup Song=Tone.ogg
	Custom Rules=CStarTrekCustomRules
	{
		Menu=Array
		{
			Item Count=2
			Menu=CAnimatedMenuInfo
			{
				Menu Name=Main Menu
				Menu Definition=CStarTrekMainMenu
				{
					Nernie=Array
					{
						Item Count=2
						Nernie=CMovieNernie
						{
							Position=0,91
							Movie to Play=Missing.bik
						}
						Nernie=CImageNernie
						{
							Position=8,439
							Image=Main Menu/Logo
						}
					}
					Menu Pos=155,109
					ButtonText Pos=0,5
					Button Spacing=8
				}
			}
			Menu=CAnimatedMenuInfo
			{
				Menu Name=Confirm Dialog
				Menu Definition=CStarTrekConfirmDialogMenu
				{
					TextCenter Pos=299,28
					YesButton Pos=68,88
					YesButton Text Pos=0,4
					YesButton Text=&Yes
					YesButton Font=Placar Blue
					YesButton Press Sound=!None.ogg
					NoButton Pos=222,88
					NoButton Text Pos=0,4
					NoButton Text=&No
					NoButton Font=Placar Blue
					NoButton Press Sound=!None.ogg
				}
			}
		}
	}
}
)";
    std::string out;
    for (char c : text) {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

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
    // A font of two characters, A and B, every other drawn as A.
    const auto font = stawayteam::fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    const std::vector<uint8_t> tone(std::begin(kTone440Ogg), std::end(kTone440Ogg));
    const std::vector<test::ZipFile> files = {
        {"Rules.txt", test::toBytes(rules()), {}},
        {"Cache/Interface/Screens/Main Menu/Logo.frm16", sprite, {}},
        {"Cache/Interface/Screens/Main Menu/Main Button Default.frm16", solidFrame(40, 10, 0x001f), {}},
        {"Cache/Interface/Screens/Main Menu/Main Button Hover.frm16", solidFrame(40, 10, 0x07e0), {}},
        {"Cache/Interface/Screens/Confirm Dialog/Background.frm16", solidFrame(100, 50, 0x8410), {}},
        {"Cache/Interface/Screens/Confirm Dialog/Yes Default.frm16", solidFrame(20, 8, 0x07e0), {}},
        {"Cache/Interface/Screens/Confirm Dialog/No Default.frm16", solidFrame(20, 8, 0xf800), {}},
        {"Cache/Fonts/Placar Condensed.seq16", font, {}},
        {"Cache/Fonts/Placar Blue.seq16", font, {}},
        {"Sounds/Interface/Main Screen Button.ogg", tone, {}},
        {"Sounds/!None.ogg", tone, {}},
        {"Cache/Textures/Test.frm16", texture, {}},
        {"Cache/Models/Icon.frm16", sprite, {}},
        {"Cache/Sequences/Blink.seq16",
         stawayteam::sequenceFile({solidFrame(6, 6, 0xf800), solidFrame(8, 8, 0x001f)}, 4.0f),
         {}},
        {"Cache/Models/Test.mdl16", stawayteam::modelFile("Test", 2, 3), {}},
        {"Cache/Fonts/Test.seq16", font, {}},
        {"Music/Tone.ogg", tone, {}},
        {"Levels/Test.RDS", test::toBytes(":Hello\r\nHello, Away Team.\r\n"), {}}};
    writeData(dir / "DATA.dat", files);
    const auto found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    rec.add("detect.description", found.empty() ? "" : found[0].description);

    // Started, it reads the rules; its movies aren't there to play, so the
    // main menu comes, its picture and buttons at their places. Esc asks
    // whether to quit, N says no, Q (Quit's shortcut) asks again, and Y
    // quits: the window isn't closed.
    int frames = 0;
    std::vector<std::string> screens;
    rec.add("play.result", play(*engine, dir, frames, &screens, {key::kEscape, 'n', 'q', 'y'}));
    rec.add("play.frames", static_cast<int64_t>(frames));
    for (size_t i = 0; i < screens.size(); ++i)
        rec.add("play.screen " + std::to_string(i), screens[i]);

    // The data viewer: past the dialog's pictures to the menu's sprite, on
    // each background (they show through its transparent and blended
    // pixels), then the next folder's sprite, zoomed, the next folder's
    // (the texture), and without the status line; then each page in turn:
    // the sequence stepped, the model turned and its next frame, the font,
    // the sound played (no output here), and the objects, the next file's.
    // A checksum of each screen.
    if (const MetaViewer *viewer = findViewer("stawayteam")) {
        ScriptedSystem sys({'f', 'b', 'b', 'f', 'z', 'f', 'i', key::kTab, '.', key::kTab, '.', ']', key::kTab,
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
