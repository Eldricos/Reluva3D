// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Birth of the Federation: the engine's MetaEngine (its name, the
// releases it recognises, making an Engine to run one) and its Engine,
// which so far runs the main menu, the options and Load Game screens and
// the start of a new game: the game setup screen, then the empire selection screen
// (docs/engines/botf/systems/screens.md).

#include "common/zip.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/game_files.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/ui/empire_screen.h"
#include "engines/botf/ui/load_screen.h"
#include "engines/botf/ui/main_menu.h"
#include "engines/botf/ui/options_screen.h"
#include "engines/botf/ui/setup_screen.h"
#include "engines/engine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
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

// The saved games in the install's slots, game0.sav to game11.sav (any
// case). Only the install's own: the CD has the tutorials' saves too, but a
// slot emptied in the install should stay empty. A save that can't be read
// shows as an empty slot.
LoadScreen::Slots findSaves(const std::filesystem::path &dir) {
    LoadScreen::Slots slots;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        for (char &c : name)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        for (int i = 0; i < LoadScreen::kSlots; ++i)
            if (name == "game" + std::to_string(i) + ".sav") {
                SaveSummary save;
                if (!readSaveSummary(entry.path(), save))
                    slots[size_t(i)] = save;
            }
    }
    return slots;
}

// The settings the game setup screen and the options screen share: the
// manual says each can change them.
void preferencesToSetup(const GameOptions &o, GameSetup &s) {
    s.strategicTimer = o.strategicTimer;
    s.tacticalTimer = o.tacticalTimer;
    s.randomEvents = o.randomEvents;
    s.manualCombat = o.manualCombat;
}

void preferencesToOptions(const GameSetup &s, GameOptions &o) {
    o.strategicTimer = s.strategicTimer;
    o.tacticalTimer = s.tacticalTimer;
    o.randomEvents = s.randomEvents;
    o.manualCombat = s.manualCombat;
}

/**
 * The game running: the main menu, its options and Load Game screens, and
 * from its New Single Player Game the game setup screen, then the empire
 * selection screen. There's no game to start or load yet, so the empire
 * screen's Accept and Load Game's come back to the main menu.
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
        _options = optionsFromIni(files.ini());
        // A new game's setup starts from the last one: Stbof.ini's, then the
        // last accepted.
        GameSetup lastSetup = setupFromIni(files.ini());
        Assets assets(_stbof);
        MainMenu menu(assets, _lexicon);
        OptionsScreen options(assets, _lexicon);
        LoadScreen loading(assets, _lexicon);
        SetupScreen setup(assets, _lexicon);
        EmpireScreen empires(assets, _lexicon);
        if (Error err = menu.load())
            return err;
        if (Error err = options.load())
            return err;
        if (Error err = loading.load())
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
        enum class Showing { kMenu, kOptions, kLoad, kSetup, kEmpires };
        Showing showing = Showing::kMenu;
        Showing shown = showing; // before the event
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
                    // The menu's other choices (the Hall of Fame,
                    // multiplayer) aren't there yet, and do nothing.
                    const MainMenu::Choice c = menu.handle(ev);
                    if (c == MainMenu::Choice::kQuit)
                        return Error::success();
                    if (c == MainMenu::Choice::kNewSinglePlayer) {
                        setup.setup() = lastSetup;
                        preferencesToSetup(_options, setup.setup());
                        showing = Showing::kSetup;
                    }
                    if (c == MainMenu::Choice::kLoadGame) {
                        loading.open(findSaves(_target.path));
                        showing = Showing::kLoad;
                    }
                    if (c == MainMenu::Choice::kOptions) {
                        options.open(_options);
                        showing = Showing::kOptions;
                    }
                } else if (showing == Showing::kOptions) {
                    const OptionsScreen::Result r = options.handle(ev);
                    if (r == OptionsScreen::Result::kAccepted)
                        _options = options.options();
                    if (r != OptionsScreen::Result::kOpen)
                        showing = Showing::kMenu;
                } else if (showing == Showing::kLoad) {
                    if (loading.handle(ev) != LoadScreen::Result::kOpen)
                        showing = Showing::kMenu;
                } else if (showing == Showing::kSetup) {
                    const SetupScreen::Result r = setup.handle(ev);
                    if (r == SetupScreen::Result::kCancelled)
                        showing = Showing::kMenu;
                    if (r == SetupScreen::Result::kAccepted) {
                        preferencesToOptions(setup.setup(), _options);
                        lastSetup = setup.setup();
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
                if (showing != shown && showing == Showing::kMenu)
                    menu.forgetPointer();
                shown = showing;
                redraw = redraw || ev.type != EventType::kKeyUp;
            }
            if (redraw) {
                std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
                if (showing == Showing::kMenu)
                    menu.draw(screen);
                else if (showing == Showing::kOptions)
                    options.draw(screen);
                else if (showing == Showing::kLoad)
                    loading.draw(screen);
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
    GameOptions _options; // from Stbof.ini; changes aren't written back yet
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
