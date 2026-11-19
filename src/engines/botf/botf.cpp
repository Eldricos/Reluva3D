// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Birth of the Federation: the engine's MetaEngine (its name, the
// releases it recognises, making an Engine to run one) and its Engine,
// which so far runs the main menu and the start of a new game: the game
// setup screen, then the empire selection screen
// (docs/engines/botf/systems/screens.md).

#include "common/zip.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/game_files.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/ui/empire_screen.h"
#include "engines/botf/ui/main_menu.h"
#include "engines/botf/ui/setup_screen.h"
#include "engines/engine.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace reluva::botf {

namespace {

// Detection keys on the data files: trek.exe is only SafeDisc's loader
// (docs/engines/botf/README.md), and a patched or replaced one shouldn't
// stop a copy being recognised.
const std::vector<GameVariant> kVariants = {
    {"botf",
     "Star Trek: Birth of the Federation",
     "en",
     "windows",
     {{"stbof.res", "2e888c1eaefa901da9e2dbd6229798a5", 79901501},
      {"english.snd", "806bf768303d60745c792fae58ec506f", 39509316}}},
    // stbof.res as on the CD's GERMAN folder; german.snd isn't hashed yet.
    {"botf",
     "Star Trek: Birth of the Federation",
     "de",
     "windows",
     {{"stbof.res", "21df64c3479690c70f50bfc0bed3792b", 79385299}, {"german.snd", "", -1}}},
};

// The same game by its files' names alone, for releases not in kVariants:
// used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"botf",
     "Star Trek: Birth of the Federation (unknown release)",
     "",
     "windows",
     {{"stbof.res", "", -1}, {"sfx.snd", "", -1}}},
};

constexpr int kScreenW = 800; // the game's own resolution
constexpr int kScreenH = 600;

/**
 * The game running: the main menu, and from its New Single Player Game the
 * game setup screen, then the empire selection screen. There's no game to
 * start yet, so the empire screen's Accept comes back to the main menu.
 */
class BotfEngine : public Engine {
public:
    BotfEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const GameFiles files(_target.path);
        const auto stbof = files.find("stbof.res");
        if (!stbof)
            return Error::fail("No stbof.res in " + _target.path.string());
        if (Error err = _stbof.open(*stbof))
            return err;
        // Without lexicon.dic the screens show their English words.
        std::vector<uint8_t> data;
        if (!_stbof.read("lexicon.dic", data))
            _lexicon.load(data);
        Assets assets(_stbof);
        MainMenu menu(assets, _lexicon);
        SetupScreen setup(assets, _lexicon);
        EmpireScreen empires(assets, _lexicon);
        if (Error err = menu.load())
            return err;
        if (Error err = setup.load())
            return err;
        if (Error err = empires.load())
            return err;

        if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());
        Surface screen;
        screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
        enum class Showing { kMenu, kSetup, kEmpires };
        Showing showing = Showing::kMenu;
        bool redraw = true;
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (showing == Showing::kMenu) {
                    // Esc on the main menu leaves, as its Quit Game does: a
                    // convenience the game hasn't got.
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
                        return Error::success();
                    // The menu's other choices (loading, the options, the
                    // Hall of Fame, multiplayer) aren't there yet, and do
                    // nothing.
                    const MainMenu::Choice c = menu.handle(ev);
                    if (c == MainMenu::Choice::kQuit)
                        return Error::success();
                    if (c == MainMenu::Choice::kNewSinglePlayer) {
                        setup.setup() = GameSetup{};
                        showing = Showing::kSetup;
                    }
                } else if (showing == Showing::kSetup) {
                    const SetupScreen::Result r = setup.handle(ev);
                    if (r == SetupScreen::Result::kCancelled)
                        showing = Showing::kMenu;
                    if (r == SetupScreen::Result::kAccepted) {
                        showing = Showing::kEmpires;
                        empires.reset();
                        empires.setLevels(setup.setup().civilization);
                    }
                } else {
                    const EmpireScreen::Result r = empires.handle(ev);
                    if (r == EmpireScreen::Result::kAccepted)
                        showing = Showing::kMenu;
                    if (r == EmpireScreen::Result::kCancelled)
                        showing = Showing::kSetup;
                }
                redraw = redraw || ev.type != EventType::kKeyUp;
            }
            if (redraw) {
                std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
                if (showing == Showing::kMenu)
                    menu.draw(screen);
                else if (showing == Showing::kSetup)
                    setup.draw(screen);
                else
                    empires.draw(screen);
                redraw = false;
            }
            _sys.updateScreen(screen);
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    ZipArchive _stbof;
    Lexicon _lexicon;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a BotfEngine to run one.
 */
class BotfMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "botf"; }
    const char *name() const override { return "Star Trek: Birth of the Federation"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<BotfEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::botf

namespace reluva {

const MetaEngine &botfMetaEngine() {
    static const botf::BotfMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
