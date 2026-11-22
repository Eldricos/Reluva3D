// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Birth of the Federation: the engine's MetaEngine (its name, the
// releases it recognises, making an Engine to run one) and its Engine,
// which so far runs the main menu and its screens (the options, loading and
// saving, the Hall of Fame, multiplayer setup) and the start of a new game: the game setup screen, then the empire selection screen
// (docs/engines/botf/systems/screens.md).

#include "common/log.h"
#include "common/zip.h"
#include "engines/botf/formats/lexicon.h"
#include "engines/botf/game_files.h"
#include "engines/botf/menu_audio.h"
#include "engines/botf/ui/assets.h"
#include "engines/botf/ui/empire_screen.h"
#include "engines/botf/ui/fame_screen.h"
#include "engines/botf/ui/galaxy_screen.h"
#include "engines/botf/ui/load_save_screen.h"
#include "engines/botf/ui/main_menu.h"
#include "engines/botf/ui/multiplayer_screen.h"
#include "engines/botf/ui/options_screen.h"
#include "engines/botf/ui/setup_screen.h"
#include "engines/engine.h"
#include "audio/wav.h"
#include "video/movie.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
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

// How long the typing cursors (Save Game, Multiplayer Setup) are shown,
// then hidden: measured on a recording of the game, frame by frame.
constexpr uint64_t kCursorFlashMillis = 300;

constexpr int kScreenW = 800; // the game's own resolution
constexpr int kScreenH = 600;

// The install's file `name` (lower case), in any case, if it's there.
std::optional<std::filesystem::path> saveFile(const std::filesystem::path &dir, const std::string &name) {
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string file = entry.path().filename().string();
        for (char &c : file)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (file == name)
            return entry.path();
    }
    return std::nullopt;
}

// Whether the install has an autosave Continue can go on from: auto.sav
// (any case) holding a saved game, not a fresh install's empty file.
// Without it the game's Continue is dimmed; last.sav makes no difference
// (the user, in the game).
bool hasAutosave(const std::filesystem::path &dir) {
    SaveSummary save;
    const auto path = saveFile(dir, "auto.sav");
    return path && !readSaveSummary(*path, save);
}

// Opens the saved game at `path` on the galaxy screen, as its player sees
// it; fails if the save can't be read.
Error openSavedGame(const std::filesystem::path &path, GalaxyScreen &galaxy) {
    GameState game;
    if (Error err = GameState::load(path, game))
        return err;
    if (game.data().summary.empire < 0)
        return Error::fail(path.string() + ": the player's empire isn't one of the five");
    if (Error err = galaxy.load(game.player()))
        return err;
    galaxy.open(std::move(game));
    return Error::success();
}

// Saves the galaxy's game in Save Game's slot `slot`, with `description`
// (UTF-8): into the slot's file, whatever its case, or a new gameN.sav.
Error saveGame(const std::filesystem::path &dir, int slot, const std::string &description, GalaxyScreen &galaxy) {
    const std::string name = "game" + std::to_string(slot) + ".sav";
    const std::filesystem::path path = saveFile(dir, name).value_or(dir / name);
    GameState &game = galaxy.state();
    const std::string was = game.data().summary.description;
    game.data().summary.description = description;
    if (Error err = game.save(path)) {
        game.data().summary.description = was;
        return err;
    }
    game.data().summary.name = name;
    return Error::success();
}

// The saved games in the install's slots, game0.sav to game11.sav (any
// case). Only the install's own: the CD has the tutorials' saves too, but a
// slot emptied in the install should stay empty. A save that can't be read
// shows as an empty slot.
LoadSaveScreen::Slots findSaves(const std::filesystem::path &dir) {
    LoadSaveScreen::Slots slots;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        for (char &c : name)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        for (int i = 0; i < LoadSaveScreen::kSlots; ++i)
            if (name == "game" + std::to_string(i) + ".sav") {
                SaveSummary save;
                if (!readSaveSummary(entry.path(), save))
                    slots[size_t(i)] = save;
            }
    }
    return slots;
}

// The opening movie, as the game plays it before the main menu: the
// CD's open.avi (Cinepak, 800x300, no sound), in the middle of the 800x600
// screen, with the install's intro.wav as its sound (the same 75 seconds);
// a key or a click skips it. Stbof.ini's MOVIEON=OFF leaves it out
// (docs/engines/botf/systems/movies.md). False if the window was closed.
bool playOpening(System &sys, const GameFiles &files, audio::Mixer *mixer) {
    if (auto it = files.ini().find("MOVIEON"); it != files.ini().end() && it->second == "OFF")
        return true;
    const auto movie = files.find("open.avi");
    if (!movie)
        return true;
    video::MovieOptions options;
    options.screenWidth = kScreenW;
    options.screenHeight = kScreenH;
    if (const auto wav = files.find("intro.wav")) {
        std::ifstream in(*wav, std::ios::binary);
        const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto sound = std::make_shared<audio::Sound>();
        if (!audio::decodeWav(data, *sound))
            options.soundtrack = std::move(sound);
    }
    bool quit = false;
    video::playMovie(sys, mixer, *movie, quit, options);
    return !quit;
}

// Sets `values` in the install's Stbof.ini, as the game does when the
// options or a new game's setup are accepted; without the file, nothing.
void writeStbofIni(const GameFiles &files, const std::map<std::string, std::string> &values) {
    if (!files.iniPath())
        return;
    std::string text;
    {
        std::ifstream in(*files.iniPath(), std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string updated = updateStbofIni(text, values);
    if (updated != text)
        std::ofstream(*files.iniPath(), std::ios::binary).write(updated.data(), std::streamsize(updated.size()));
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
 * The game running: the main menu, its options, Load Game and Hall of Fame
 * screens, and
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
        LoadSaveScreen loading(assets, _lexicon);
        GalaxyScreen galaxy(assets, _lexicon);
        FameScreen fame(assets, _lexicon);
        MultiplayerScreen multiplayer(assets, _lexicon);
        SetupScreen setup(assets, _lexicon);
        EmpireScreen empires(assets, _lexicon);
        if (Error err = menu.load())
            return err;
        if (auto it = files.ini().find("VERSION"); it != files.ini().end())
            menu.setVersion(it->second);
        menu.setGameInProgress(false, hasAutosave(_target.path));
        if (Error err = options.load())
            return err;
        if (Error err = loading.load())
            return err;
        if (Error err = fame.load())
            return err;
        if (Error err = multiplayer.load())
            return err;
        if (Error err = setup.load())
            return err;
        if (Error err = empires.load())
            return err;

        if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        MenuAudio audio(_sys);
        audio.open(files);
        audio.setMovieVolume(_options);
        if (!playOpening(_sys, files, audio.mixer()))
            return Error::success();
        audio.setVolumes(_options);
        audio.startMusic();
        const auto sounds = [&audio](UiSound s) { audio.play(s); };
        options.setSoundHandler(sounds);
        loading.setSoundHandler(sounds);
        fame.setSoundHandler(sounds);
        multiplayer.setSoundHandler(sounds);
        setup.setSoundHandler(sounds);
        empires.setSoundHandler(sounds);
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());
        Surface screen;
        screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
        enum class Showing { kMenu, kOptions, kLoad, kFame, kMultiplayer, kSetup, kEmpires, kGalaxy };
        Showing showing = Showing::kMenu;
        Showing shown = showing; // before the event
        bool redraw = true;
        bool cursorShown = true;
        // A game is in progress once one is opened on the galaxy screen: the
        // main menu's Continue goes back to it, and Save Game saves it.
        bool inGame = false;
        uint64_t galaxyFrame = 0; // the galaxy's strip's frame last drawn
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (showing == Showing::kMenu) {
                    // Esc on the main menu goes back to the game in progress,
                    // or leaves, as its Quit Game does: conveniences the game
                    // hasn't got.
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                        if (!inGame)
                            return Error::success();
                        showing = shown = Showing::kGalaxy;
                        redraw = true;
                        continue;
                    }
                    const MainMenu::Choice c = menu.handle(ev);
                    // Continue goes back to the game in progress, or opens
                    // the autosave's galaxy.
                    if (c == MainMenu::Choice::kContinue && inGame) {
                        showing = Showing::kGalaxy;
                    } else if (c == MainMenu::Choice::kContinue) {
                        if (const auto path = saveFile(_target.path, "auto.sav")) {
                            if (Error err = openSavedGame(*path, galaxy)) {
                                log::warning("%s", err.message.c_str());
                            } else {
                                showing = Showing::kGalaxy;
                                inGame = true;
                            }
                        }
                    }
                    if (c == MainMenu::Choice::kQuit)
                        return Error::success();
                    if (c == MainMenu::Choice::kNewSinglePlayer) {
                        setup.setup() = lastSetup;
                        preferencesToSetup(_options, setup.setup());
                        showing = Showing::kSetup;
                        audio.play(MenuAudio::Move::kToSetup);
                    }
                    if (c == MainMenu::Choice::kLoadGame) {
                        loading.open(findSaves(_target.path), LoadSaveScreen::Mode::kLoad);
                        showing = Showing::kLoad;
                        audio.play(MenuAudio::Move::kToLoad);
                    }
                    // Only with a game in progress: an empty slot is given its
                    // description, as the game's.
                    if (c == MainMenu::Choice::kSaveGame && inGame) {
                        const SaveSummary &game = galaxy.state().data().summary;
                        const int size = game.galaxySize >= 0 ? game.galaxySize : galaxy.state().galaxy().size;
                        loading.open(findSaves(_target.path), LoadSaveScreen::Mode::kSave,
                                     saveDescription(_lexicon, galaxy.state().player(), game.turn, size,
                                                     std::max(game.galaxyShape, 0)),
                                     galaxy.state().player());
                        _sys.setTextInput(true);
                        showing = Showing::kLoad;
                        audio.play(MenuAudio::Move::kToSave);
                    }
                    if (c == MainMenu::Choice::kNewMultiplayer) {
                        multiplayer.open();
                        _sys.setTextInput(true);
                        showing = Showing::kMultiplayer;
                        audio.play(MenuAudio::Move::kToMultiplayer);
                    }
                    if (c == MainMenu::Choice::kHallOfFame) {
                        // Reluva's own list, empty until it can finish a
                        // game: the game's first ten entries aren't in its
                        // files (docs/engines/botf/KNOWN_DIFFERENCES.md).
                        fame.open({});
                        showing = Showing::kFame;
                        audio.play(MenuAudio::Move::kToHallOfFame);
                    }
                    if (c == MainMenu::Choice::kOptions) {
                        options.open(_options);
                        showing = Showing::kOptions;
                        audio.play(MenuAudio::Move::kToOptions);
                    }
                } else if (showing == Showing::kOptions) {
                    const OptionsScreen::Result r = options.handle(ev);
                    if (r == OptionsScreen::Result::kAccepted) {
                        _options = options.options();
                        writeStbofIni(files, optionsIniValues(_options));
                    }
                    if (r != OptionsScreen::Result::kOpen)
                        showing = Showing::kMenu;
                } else if (showing == Showing::kMultiplayer) {
                    // There's no one to connect to yet: only Cancel leaves.
                    if (multiplayer.handle(ev) != MultiplayerScreen::Result::kOpen) {
                        _sys.setTextInput(false);
                        showing = Showing::kMenu;
                    }
                } else if (showing == Showing::kFame) {
                    if (fame.handle(ev))
                        showing = Showing::kMenu;
                } else if (showing == Showing::kLoad) {
                    // Loading opens the chosen game's galaxy; saving writes
                    // the game in progress and goes back to it, as the
                    // manual says ("to save the game and to resume play").
                    const LoadSaveScreen::Result r = loading.handle(ev);
                    if (r != LoadSaveScreen::Result::kOpen) {
                        _sys.setTextInput(false);
                        showing = Showing::kMenu;
                    }
                    if (r == LoadSaveScreen::Result::kAccepted && loading.mode() == LoadSaveScreen::Mode::kSave) {
                        if (Error err = saveGame(_target.path, loading.chosen(), loading.description(), galaxy)) {
                            log::warning("%s", err.message.c_str());
                        } else {
                            showing = Showing::kGalaxy;
                            audio.play(MenuAudio::Move::kSaved);
                        }
                    }
                    if (r == LoadSaveScreen::Result::kAccepted && loading.mode() == LoadSaveScreen::Mode::kLoad) {
                        const std::string name = "game" + std::to_string(loading.chosen()) + ".sav";
                        if (const auto path = saveFile(_target.path, name)) {
                            if (Error err = openSavedGame(*path, galaxy)) {
                                log::warning("%s", err.message.c_str());
                            } else {
                                showing = Showing::kGalaxy;
                                inGame = true;
                            }
                        }
                    }
                } else if (showing == Showing::kGalaxy) {
                    // Escape goes back to the main menu, the game still in
                    // progress: Reluva's way there, as the game's own
                    // menus aren't there yet.
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                        menu.setGameInProgress(true);
                        showing = Showing::kMenu;
                    }
                    (void)galaxy.handle(ev);
                } else if (showing == Showing::kSetup) {
                    const SetupScreen::Result r = setup.handle(ev);
                    if (r == SetupScreen::Result::kCancelled)
                        showing = Showing::kMenu;
                    if (r == SetupScreen::Result::kAccepted) {
                        preferencesToOptions(setup.setup(), _options);
                        lastSetup = setup.setup();
                        // The game keeps a new game's setup, and the
                        // preferences it shares with the options.
                        auto values = setupIniValues(lastSetup);
                        values.merge(optionsIniValues(_options));
                        writeStbofIni(files, values);
                        showing = Showing::kEmpires;
                        audio.play(MenuAudio::Move::kToEmpires);
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
                if (showing != shown && showing == Showing::kMenu) {
                    menu.forgetPointer();
                    audio.play(MenuAudio::Move::kToMenu);
                }
                shown = showing;
                redraw = redraw || ev.type != EventType::kKeyUp;
            }
            // The options' volumes as they're set, so a volume's sample, and
            // the music, are heard at the new one; Cancel puts them back.
            audio.setVolumes(showing == Showing::kOptions ? options.options() : _options);
            audio.update();
            // A pulsing button changes with time: the main menu's, and the
            // empire selection screen's names.
            menu.setTime(_sys.getMillis());
            empires.setTime(_sys.getMillis());
            galaxy.setTime(_sys.getMillis());
            if ((showing == Showing::kMenu && menu.pulsing()) || (showing == Showing::kEmpires && empires.pulsing()))
                redraw = true;
            // The galaxy's selected system turns: drawn again at its star's
            // rate, the fastest of its animations.
            if (showing == Showing::kGalaxy && _sys.getMillis() / GalaxyScreen::kFastestFrameMillis != galaxyFrame) {
                galaxyFrame = _sys.getMillis() / GalaxyScreen::kFastestFrameMillis;
                redraw = true;
            }
            // A timer's arrow held steps on.
            if (showing == Showing::kSetup && setup.tick(_sys.getMillis()))
                redraw = true;
            if (showing == Showing::kOptions && options.tick(_sys.getMillis()))
                redraw = true;
            if (showing == Showing::kLoad || showing == Showing::kMultiplayer) {
                const bool cursor = _sys.getMillis() / kCursorFlashMillis % 2 == 0;
                if (cursor != cursorShown) {
                    cursorShown = cursor;
                    loading.setCursorShown(cursor);
                    multiplayer.setCursorShown(cursor);
                    redraw = true;
                }
            }
            if (redraw) {
                std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
                if (showing == Showing::kMenu)
                    menu.draw(screen);
                else if (showing == Showing::kOptions)
                    options.draw(screen);
                else if (showing == Showing::kLoad)
                    loading.draw(screen);
                else if (showing == Showing::kFame)
                    fame.draw(screen);
                else if (showing == Showing::kMultiplayer)
                    multiplayer.draw(screen);
                else if (showing == Showing::kSetup)
                    setup.draw(screen);
                else if (showing == Showing::kGalaxy)
                    galaxy.draw(screen);
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
