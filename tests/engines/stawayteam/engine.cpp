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
			Item Count=4
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
			Menu=CAnimatedMenuInfo
			{
				Menu Name=Config Control Screen
				Menu Definition=CStarTrekConfigControlMenu
				{
					Nernie=Array
					{
						Item Count=1
						Nernie=CStaticTextNernie
						{
							Position=4,2
							Text=AB
							Font=Placar Condensed
							Justification=Left
							Wrap Width=0
						}
					}
					ExitButton Pos=130,110
					ExitButton Text Pos=2,1
					ExitButton Text=E&XIT
					ExitButton Font=Placar Blue
					ResetButton Pos=130,100
					ResetButton Text Pos=2,1
					ResetButton Text=&RESET TO DEFAULT
					ResetButton Font=Placar Blue
					ButtonStart Pos=4,10
					ConfigStart Pos=40,10
					ConfigStart Size=60,100
					Button Spacing=2
					Action Text=Action Button
					Move Text=Move Button
					Inventory Text=Cycle Inventory
					Action Help Text=Acts.
					Move Help Text=Moves.
					Inventory Help Text=Cycles.
					Button1 Pos=1,10
					Button1 Size=10,6
					Button2 Pos=30,10
					Button2 Size=10,6
					Button3 Pos=15,4
					Button3 Size=10,6
					HelpText Pos=1,40
					HelpText Size=40,20
					MouseTitle Pos=0,0
					Mouse Pos=110,10
					Mouse Size=45,70
					KeyChangeList Pos=4,60
					KeyChangeList Size=30,40
					MouseImage Pos=5,12
					ReverseMouse Pos=2,30
				}
			}
			Menu=CAnimatedMenuInfo
			{
				Menu Name=Credits
				Menu Definition=CStarTrekCreditsMenu
				{
					Credit Pos=10,5
					Credit Size=80,30
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
        {"Cache/Interface/Screens/Escape Menu/Escape Menu Background.frm16", solidFrame(100, 60, 0x4208), {}},
        {"Cache/Interface/Screens/Escape Menu/Button Background.frm16", solidFrame(50, 8, 0x001f), {}},
        {"Cache/Interface/Screens/Difficulty Menu/Background.frm16", solidFrame(100, 60, 0x4208), {}},
        {"Cache/Interface/Screens/Difficulty Menu/Button Background.frm16", solidFrame(50, 8, 0x001f), {}},
        {"Cache/Interface/Screens/Difficulty Menu/Toggle Default.frm16", solidFrame(5, 5, 0x8410), {}},
        {"Cache/Interface/Screens/Difficulty Menu/Toggle Press.frm16", solidFrame(5, 5, 0x07e0), {}},
        {"Sounds/Button Action.ogg", tone, {}},
        {"Cache/Interface/Screens/Configure Menu/Background.frm16", solidFrame(120, 120, 0x4208), {}},
        {"Cache/Interface/Screens/Configure Menu/Button Background.frm16", solidFrame(50, 8, 0x001f), {}},
        {"Cache/Interface/Window Texture.frm16", solidFrame(4, 4, 0x3186), {}},
        {"Cache/Fonts/Trek Small.seq16", font, {}},
        {"Cache/Fonts/Small Blue.seq16", font, {}},
        {"Cache/Interface/Screens/Key Config/Background.frm16", solidFrame(160, 120, 0x0010), {}},
        {"Cache/Interface/Screens/Key Config/Exit Default.frm16", solidFrame(20, 6, 0x4208), {}},
        {"Cache/Interface/Screens/Key Config/Reset Default.frm16", solidFrame(20, 6, 0x4208), {}},
        {"Cache/Interface/Screens/Key Config/Mouse.frm16", solidFrame(10, 8, 0xffff), {}},
        {"Cache/Interface/Screens/Sound Options Dialog/Background.frm16", solidFrame(150, 120, 0x4208), {}},
        {"Cache/Interface/Screens/Sound Options Dialog/Button Background.frm16", solidFrame(50, 8, 0x001f), {}},
        {"Cache/Interface/Screens/Sound Options Dialog/Mute Default.frm16", solidFrame(5, 5, 0x07e0), {}},
        {"Cache/Interface/Screens/Sound Options Dialog/Mute Press.frm16", solidFrame(5, 5, 0xf800), {}},
        {"Cache/Interface/Screens/Sound Options Dialog/Button Default.frm16", solidFrame(12, 6, 0x4208), {}},
        {"Cache/Interface/Screens/Sound Options Dialog/Button Press.frm16", solidFrame(12, 6, 0x8410), {}},
        {"Credits.txt", test::toBytes("// a comment\r\n_Heading\r\n+Role\r\nName\r\nRole: Name\r\n"), {}},
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
    // main menu comes, its picture and buttons at their places. S (Start
    // New Game's shortcut) asks the difficulty, N (CANCEL) closes that; C
    // (Credits') shows the credits, R (RESUME) closes them; E (Settings') shows the
    // settings, P unchecks pausing when hit, F (DIFFICULTY SETTING) shows
    // the difficulty window over them, Esc closes it, S (CONFIGURE SOUND)
    // shows the sound's settings, Esc closes them, K (CONFIGURE KEYBOARD)
    // the keyboard's, Esc closes them, C (CONTINUE) closes the settings; Q asks whether to
    // quit, N says no, Q (Quit's shortcut) asks again, and Y quits: the
    // window isn't closed.
    int frames = 0;
    std::vector<std::string> screens;
    rec.add("play.result", play(*engine, dir, frames, &screens,
                                {'s', 'n', 'e', 'p', 'f', key::kEscape, 's', key::kEscape, 'k', key::kEscape, 'c', 'c',
                                 'r', 'q', 'n', 'q', 'y'}));
    rec.add("play.frames", static_cast<int64_t>(frames));
    for (size_t i = 0; i < screens.size(); ++i)
        rec.add("play.screen " + std::to_string(i), screens[i]);

    // The data viewer: past the windows' pictures to the menu's sprite, on
    // each background (they show through its transparent and blended
    // pixels), then past the window texture to the next folder's sprite,
    // zoomed, the next folder's
    // (the texture), and without the status line; then each page in turn:
    // the sequence stepped, the model turned and its next frame, the font,
    // the sound played (no output here), and the objects, the next file's.
    // A checksum of each screen.
    if (const MetaViewer *viewer = findViewer("stawayteam")) {
        ScriptedSystem sys({'f',       'f',       'f', 'f',       'f',         'f',         'b',        'b', 'f',
                            'f',       'z',       'f', 'i',       key::kTab,   '.',         key::kTab,  '.', ']',
                            key::kTab, key::kTab, ' ', key::kTab, key::kRight, key::kRight, key::kRight});
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
