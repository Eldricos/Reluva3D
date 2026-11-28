// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which shows an empty screen
// so far. Star Trek: Armada is its first game.

#include "audio/mixer.h"
#include "audio/wav.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/storm3d/formats/preferences.h"
#include "engines/storm3d/formats/shell_progress.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/play/mission_view.h"
#include "engines/storm3d/shell/credits_screen.h"
#include "engines/storm3d/player_files.h"
#include "engines/storm3d/shell/full_screen_movie.h"
#include "engines/storm3d/shell/game_setup_screen.h"
#include "engines/storm3d/shell/load_game_screen.h"
#include "engines/storm3d/shell/loading_screen.h"
#include "engines/storm3d/shell/main_menu.h"
#include "engines/storm3d/shell/multiplayer_screen.h"
#include "engines/storm3d/shell/debriefing_screen.h"
#include "engines/storm3d/shell/options_screen.h"
#include "engines/storm3d/shell/single_player.h"

#include <algorithm>
#include <cstdlib>
#include <random>

namespace reluva::storm3d {

namespace {

// The releases it recognises, as `reluva --hash <files...>` prints them.
// Which files a patch changes isn't known yet, so a release is told by all
// three. See docs/engines/storm3d/games/armada/formats/install.md.
const std::vector<GameVariant> kVariants = {
    // English retail CD, Armada.exe of 10 March 2000 (November 2026).
    {"armada",
     "Star Trek: Armada",
     "en",
     "windows",
     {{"Armada.exe", "ecce0bedbc8159b7fa550319fc866432", 2383917},
      {"assets.zfs", "586ad0c23181ab5ca55dde64b807d8c3", 832202},
      {"objtxt.zfs", "9cf8666631c341618b1db662e0105a7a", 19216}}},
};

// The same game by its files' names alone, for releases not in kVariants
// (patches, other languages). Used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"armada",
     "Star Trek: Armada (unknown release)",
     "",
     "windows",
     {{"Armada.exe", "", -1}, {"assets.zfs", "", -1}, {"objtxt.zfs", "", -1}}},
};

/**
 * The game running: the opening movie, then the front end's main menu
 * (shell/main_menu.h), until the player quits. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class Storm3dEngine : public Engine {
public:
    Storm3dEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (Error err = _files.open(_target.path))
            return err;
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32))
            return err;
        _sys.setWindowTitle(("Reluva - " + _target.description).c_str());
        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        _mixer = audio::startMixerOutput(_sys, mixer) ? nullptr : &mixer;
        loadPlayer();
        applyVolumes();
        Surface screen;
        screen.create(kWidth, kHeight, PixelFormat::kRGBA32);

        bool intro = startIntro();
        MainMenu menu;
        SinglePlayerScreen single;
        OptionsScreen options;
        SettingsScreen settings;
        MultiplayerScreen connection;
        GameSetupScreen setup;
        bool setupOn = false;
        LoadGameScreen loadGame;
        bool loadGameOn = false;
        LoadingScreen loading(_files);
        CreditsScreen credits(_files);
        bool creditsOn = false;
        bool menuOn = false;
        bool singleOn = false;
        bool optionsOn = false;
        bool settingsOn = false;
        bool connectionOn = false;
        bool loadingOn = false;
        MissionView mission(_files);
        mission.setMixer(_mixer);
        bool missionOn = false;
        // Over a skirmish, its own screens: its menu (the Options screen
        // with Surrender, Abort Mission and Return to Game), the settings
        // from it, and the debriefing it ends on.
        OptionsScreen gameMenu;
        bool gameMenuOn = false;
        bool gameSettingsOn = false;
        DebriefingScreen debriefing;
        bool debriefingOn = false;
        // A skirmish launched, waiting for its loading screen: its players.
        std::vector<MissionView::Player> skirmish;
        bool shroud = true;
        bool fog = true;
        Hud::Resources start;
        gfx::Renderer3D *renderer3D = nullptr;
        // Back from the loading screen or the mission to the single-player
        // screen, in 2D.
        auto endLoading = [&]() -> Error {
            loading.stop();
            mission.stop();
            loadingOn = missionOn = false;
            gameMenuOn = gameSettingsOn = debriefingOn = false;
            return _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32);
        };
        // The debriefing, its rows the players' as the Game Setup screen
        // named them, in their colours.
        auto openDebriefing = [&]() -> Error {
            if (Error err = debriefing.load(_files))
                return err;
            std::vector<DebriefingScreen::Row> rows;
            for (const MissionView::Player &p : mission.players()) {
                DebriefingScreen::Row row;
                row.name = p.name;
                row.side = p.side;
                row.colour = {uint8_t(std::lround(std::clamp(p.colour[0], 0.0f, 1.0f) * 255)),
                              uint8_t(std::lround(std::clamp(p.colour[1], 0.0f, 1.0f) * 255)),
                              uint8_t(std::lround(std::clamp(p.colour[2], 0.0f, 1.0f) * 255))};
                auto t = mission.tallies().find(p.slot);
                if (t != mission.tallies().end()) {
                    const MissionView::Tally &n = t->second;
                    row.numbers = {n.collected, n.spent,        n.shipsCreated,   n.stationsCreated,
                                   n.shipsLost, n.stationsLost, n.shipsDestroyed, n.stationsDestroyed};
                }
                rows.push_back(row);
            }
            debriefing.setRows(rows);
            gameMenuOn = gameSettingsOn = false;
            debriefingOn = true;
            return Error::success();
        };
        for (bool running = true; running;) {
            const uint64_t now = _sys.getMillis();
            if (!intro && !menuOn) {
                if (Error err = menu.load(_files, now)) {
                    stopAll();
                    return err;
                }
                menuOn = true;
                // The shell's music, over and over (PlaySoundA's SND_LOOP, 0x546195).
                _music = play("sounds/effects/shellsd0.wav", audio::SoundType::kMusic, true);
            }
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                    running = false;
                } else if (intro) {
                    // Esc, a key or a click ends it, as the game's does.
                    if (ev.type == EventType::kKeyDown || ev.type == EventType::kMouseDown)
                        intro = stopIntro();
                } else if (creditsOn) {
                    // "Press any key to exit": a key or a click.
                    if (ev.type == EventType::kKeyDown || ev.type == EventType::kMouseDown) {
                        credits.stop();
                        creditsOn = false;
                        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32)) {
                            stopAll();
                            return err;
                        }
                    }
                } else if (missionOn && debriefingOn) {
                    if (ev.type == EventType::kMouseMove && debriefing.pointerAt(ev.mouseX, ev.mouseY))
                        play("sounds/effects/cornron.wav", audio::SoundType::kEffects, false);
                    else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft)
                        debriefing.press(ev.mouseX, ev.mouseY);
                    else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
                        const DebriefingChoice choice = debriefing.release(ev.mouseX, ev.mouseY);
                        if (choice != DebriefingChoice::kNone)
                            play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                        if (choice == DebriefingChoice::kQuit) {
                            running = false;
                        } else if (choice == DebriefingChoice::kContinue) {
                            // Back to the Multiplayer Connection screen, as the
                            // original's Continue went.
                            if (Error err = endLoading()) {
                                stopAll();
                                return err;
                            }
                            setupOn = false;
                            _sys.setTextInput(true);
                            connectionOn = true;
                        }
                    }
                } else if (missionOn && gameSettingsOn) {
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                        gameSettingsOn = false;
                    } else if (ev.type == EventType::kMouseMove && settings.pointerAt(ev.mouseX, ev.mouseY)) {
                        play("sounds/effects/cornron.wav", audio::SoundType::kEffects, false);
                    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
                        settings.press(ev.mouseX, ev.mouseY);
                    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
                        if (settings.release(ev.mouseX, ev.mouseY))
                            gameSettingsOn = false;
                        applyVolumes();
                    }
                } else if (missionOn && gameMenuOn) {
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                        gameMenuOn = false;
                    } else if (ev.type == EventType::kMouseMove && gameMenu.pointerAt(ev.mouseX, ev.mouseY)) {
                        play("sounds/effects/cornron.wav", audio::SoundType::kEffects, false);
                    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
                        gameMenu.press(ev.mouseX, ev.mouseY);
                    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
                        const OptionsChoice choice = gameMenu.release(ev.mouseX, ev.mouseY);
                        if (choice != OptionsChoice::kNone)
                            play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                        if (choice == OptionsChoice::kReturn) {
                            gameMenuOn = false;
                        } else if (choice == OptionsChoice::kSurrender) {
                            // Straight to the debriefing, as the original went.
                            mission.surrender();
                            if (Error err = openDebriefing()) {
                                stopAll();
                                return err;
                            }
                        } else if (choice == OptionsChoice::kAbort) {
                            // Its box's Yes: to the debriefing, as Surrender
                            // goes, as the original went.
                            if (Error err = openDebriefing()) {
                                stopAll();
                                return err;
                            }
                        } else if (choice == OptionsChoice::kGraphics || choice == OptionsChoice::kSound ||
                                   choice == OptionsChoice::kGame) {
                            if (Error err = settings.load(_files, &_preferences)) {
                                stopAll();
                                return err;
                            }
                            settings.open(choice == OptionsChoice::kGraphics ? SettingsPage::kGraphics
                                          : choice == OptionsChoice::kSound  ? SettingsPage::kSound
                                                                             : SettingsPage::kGame);
                            gameSettingsOn = true;
                        }
                    }
                } else if (missionOn && ev.type == EventType::kKeyDown && ev.key == key::kEscape &&
                           !mission.players().empty()) {
                    // A skirmish's menu, as its MENU button opens it.
                    if (Error err = gameMenu.load(_files, true)) {
                        stopAll();
                        return err;
                    }
                    gameMenuOn = true;
                } else if (missionOn && !(ev.type == EventType::kKeyDown && ev.key == key::kEscape)) {
                    mission.handleEvent(ev);
                } else if (loadingOn || missionOn) {
                    // Esc gives up on a campaign mission: its own menu comes
                    // later.
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                        if (Error err = endLoading()) {
                            stopAll();
                            return err;
                        }
                    }
                } else if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                    // Out a screen, as its Previous Menu does; out of the game from the menu.
                    if (setupOn)
                        setupOn = false;
                    else if (settingsOn)
                        settingsOn = false;
                    else if (loadGameOn)
                        loadGameOn = false;
                    else if (connectionOn)
                        connectionOn = leaveConnection();
                    else if (singleOn || optionsOn)
                        singleOn = optionsOn = false;
                    else
                        running = false;
                } else if (connectionOn && ev.type == EventType::kTextInput) {
                    connection.type(ev.text);
                } else if (connectionOn && ev.type == EventType::kKeyDown && ev.key == key::kBackspace) {
                    connection.erase();
                } else if (ev.type == EventType::kMouseMove) {
                    if (singleOn       ? single.pointerAt(ev.mouseX, ev.mouseY, now)
                        : setupOn      ? setup.pointerAt(ev.mouseX, ev.mouseY)
                        : loadGameOn   ? loadGame.pointerAt(ev.mouseX, ev.mouseY)
                        : settingsOn   ? settings.pointerAt(ev.mouseX, ev.mouseY)
                        : connectionOn ? connection.pointerAt(ev.mouseX, ev.mouseY)
                        : optionsOn    ? options.pointerAt(ev.mouseX, ev.mouseY)
                                       : menu.pointerAt(ev.mouseX, ev.mouseY, now))
                        play("sounds/effects/cornron.wav", audio::SoundType::kEffects, false);
                } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
                    if (setupOn)
                        setup.press(ev.mouseX, ev.mouseY);
                    else if (loadGameOn)
                        loadGame.press(ev.mouseX, ev.mouseY);
                    else if (singleOn)
                        single.press(ev.mouseX, ev.mouseY);
                    else if (settingsOn)
                        settings.press(ev.mouseX, ev.mouseY);
                    else if (connectionOn)
                        connection.press(ev.mouseX, ev.mouseY);
                    else if (optionsOn)
                        options.press(ev.mouseX, ev.mouseY);
                    else
                        menu.press(ev.mouseX, ev.mouseY);
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && setupOn) {
                    const SetupChoice choice = setup.release(ev.mouseX, ev.mouseY);
                    if (choice != SetupChoice::kNone)
                        play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                    if (choice == SetupChoice::kPreviousMenu) {
                        setupOn = false;
                    } else if (choice == SetupChoice::kMainMenu) {
                        setupOn = false;
                        connectionOn = leaveConnection();
                    } else if (choice == SetupChoice::kLaunch) {
                        skirmish.clear();
                        // Random sides and places rolled as the game rolls
                        // them at launch, its generator's own numbers aside;
                        // a "seed" setting makes them the same each time.
                        auto seed = _target.settings.find("seed");
                        std::mt19937 rolls(seed != _target.settings.end()
                                               ? uint32_t(std::strtoul(seed->second.c_str(), nullptr, 10))
                                               : std::random_device{}());
                        auto below = [&rolls](int n) { return int(rolls() % uint32_t(std::max(n, 1))); };
                        size_t playing = 0;
                        for (const GameSetupScreen::Slot &slot : setup.slots())
                            playing += slot.who != GameSetupScreen::Who::kEmpty;
                        const std::vector<int> places =
                            setup.randomPlacement() ? GameSetupScreen::placeAtRandom(setup.starts(), playing, below)
                                                    : std::vector<int>{};
                        for (size_t i = 0; i < setup.slots().size(); ++i) {
                            GameSetupScreen::Slot slot = setup.slots()[i];
                            if (slot.who == GameSetupScreen::Who::kEmpty)
                                continue;
                            if (slot.side == 0)
                                slot.side = GameSetupScreen::randomSide(below(4));
                            // A player's number is its start position, as the
                            // game's; with Fixed Placement each player in slot
                            // order takes the lowest start not taken (0x4bb9e0).
                            MissionView::Player p;
                            const size_t k = skirmish.size();
                            p.slot = k < places.size()           ? places[k]
                                     : k < setup.starts().size() ? setup.starts()[k]
                                                                 : int(k) + 1;
                            p.race = GameSetupScreen::raceOf(slot, i);
                            p.colour = MapScene::teamColourOf(GameSetupScreen::colourName(slot.colour), _files.files())
                                           .value_or(gfx::Vec3{0.5f, 0.5f, 0.5f});
                            p.human = slot.who == GameSetupScreen::Who::kPlayer;
                            p.stances = slot.who == GameSetupScreen::Who::kComputerMedium ||
                                        slot.who == GameSetupScreen::Who::kComputerHard;
                            p.team = slot.team;
                            // The debriefing names a computer in the program's
                            // own words ("Computer Medium"), not the setup's.
                            p.name = slot.who == GameSetupScreen::Who::kComputerEasy     ? "Computer Easy"
                                     : slot.who == GameSetupScreen::Who::kComputerMedium ? "Computer Medium"
                                     : slot.who == GameSetupScreen::Who::kComputerHard   ? "Computer Hard"
                                                                                         : setup.whoText(slot);
                            p.side = setup.sideText(slot, i);
                            skirmish.push_back(p);
                        }
                        shroud = setup.shroud();
                        fog = setup.fog();
                        start = {setup.dilithium(), setup.crew(), 0, setup.officers()};
                        if (Error err = _sys.init3D(kWidth, kHeight, renderer3D)) {
                            stopAll();
                            return err;
                        }
                        if (Error err = loading.start(renderer3D, setup.map())) {
                            stopAll();
                            return err;
                        }
                        setupOn = false;
                        loadingOn = true;
                    }
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && loadGameOn) {
                    if (loadGame.release(ev.mouseX, ev.mouseY)) {
                        play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                        loadGameOn = false;
                    }
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && singleOn) {
                    const SingleChoice choice = single.release(ev.mouseX, ev.mouseY, now);
                    if (choice != SingleChoice::kNone)
                        play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                    if (choice == SingleChoice::kPreviousMenu) {
                        singleOn = false;
                    } else if (choice == SingleChoice::kLoadGame) {
                        if (Error err = loadGame.load(_files)) {
                            stopAll();
                            return err;
                        }
                        loadGameOn = true;
                    } else if (choice == SingleChoice::kStartMission) {
                        if (Error err = _sys.init3D(kWidth, kHeight, renderer3D)) {
                            stopAll();
                            return err;
                        }
                        skirmish.clear();
                        if (Error err = loading.start(renderer3D, single.mission())) {
                            stopAll();
                            return err;
                        }
                        loadingOn = true;
                    }
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && settingsOn) {
                    if (settings.release(ev.mouseX, ev.mouseY))
                        settingsOn = false;
                    applyVolumes();
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && connectionOn) {
                    // The ways to play need the network and the game itself:
                    // only Previous Menu does anything yet.
                    const ConnectionChoice choice = connection.release(ev.mouseX, ev.mouseY);
                    if (choice != ConnectionChoice::kNone)
                        play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                    if (choice == ConnectionChoice::kPreviousMenu) {
                        connectionOn = leaveConnection();
                    } else if (choice == ConnectionChoice::kInstantAction) {
                        // The name typed, or the one the player's progress keeps.
                        const std::string name = connection.name().empty() ? _progress.player : connection.name();
                        if (Error err = setup.load(_files, name)) {
                            stopAll();
                            return err;
                        }
                        setupOn = true;
                    }
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && optionsOn) {
                    const OptionsChoice choice = options.release(ev.mouseX, ev.mouseY);
                    if (choice != OptionsChoice::kNone)
                        play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                    if (choice == OptionsChoice::kPreviousMenu) {
                        optionsOn = false;
                    } else if (choice != OptionsChoice::kNone) {
                        if (Error err = settings.load(_files, &_preferences)) {
                            stopAll();
                            return err;
                        }
                        settings.open(choice == OptionsChoice::kGraphics ? SettingsPage::kGraphics
                                      : choice == OptionsChoice::kSound  ? SettingsPage::kSound
                                                                         : SettingsPage::kGame);
                        settingsOn = true;
                    }
                } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
                    const MenuChoice choice = menu.release(ev.mouseX, ev.mouseY);
                    if (choice != MenuChoice::kNone)
                        play("sounds/effects/cornrclk.wav", audio::SoundType::kEffects, false);
                    if (choice == MenuChoice::kQuit) {
                        running = false;
                    } else if (choice == MenuChoice::kReplayIntro) {
                        stopSound(_music);
                        menuOn = false;
                        intro = startIntro();
                    } else if (choice == MenuChoice::kSinglePlayer) {
                        if (Error err = single.load(_files, now)) {
                            stopAll();
                            return err;
                        }
                        single.setProgress(_progress);
                        singleOn = true;
                    } else if (choice == MenuChoice::kCredits) {
                        gfx::Renderer3D *renderer = nullptr;
                        if (Error err = _sys.init3D(kWidth, kHeight, renderer)) {
                            stopAll();
                            return err;
                        }
                        if (Error err = credits.start(renderer, now)) {
                            stopAll();
                            return err;
                        }
                        creditsOn = true;
                    } else if (choice == MenuChoice::kMultiplayer) {
                        if (Error err = connection.load(_files)) {
                            stopAll();
                            return err;
                        }
                        _sys.setTextInput(true);
                        connectionOn = true;
                    } else if (choice == MenuChoice::kOptions) {
                        if (Error err = options.load(_files)) {
                            stopAll();
                            return err;
                        }
                        optionsOn = true;
                    }
                }
            }
            if (creditsOn) {
                // Back to the menu after the last page.
                if (!credits.draw(now)) {
                    credits.stop();
                    creditsOn = false;
                    if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32)) {
                        stopAll();
                        return err;
                    }
                }
                _sys.delayMillis(kFrameMs);
                continue;
            }
            if (missionOn) {
                if (debriefingOn || gameSettingsOn || gameMenuOn) {
                    if (debriefingOn)
                        debriefing.draw(screen);
                    else if (gameSettingsOn)
                        settings.draw(screen);
                    else
                        gameMenu.draw(screen);
                    mission.drawScreen(screen, now);
                } else {
                    mission.draw(now);
                    if (mission.takeMenuRequest()) {
                        if (Error err = gameMenu.load(_files, true)) {
                            stopAll();
                            return err;
                        }
                        gameMenuOn = true;
                    } else if (mission.ending() != MissionView::Ending::kNone) {
                        if (Error err = openDebriefing()) {
                            stopAll();
                            return err;
                        }
                    }
                }
                _sys.delayMillis(kFrameMs);
                continue;
            }
            if (loadingOn) {
                // The files read, the screen drawn in 3D; then the mission,
                // seen from Camera_1, the campaign's player's ("Premonitions'"
                // first frame of play), its scripts running. No shroud yet.
                const bool more = loading.step();
                loading.draw(now);
                if (!more) {
                    loading.stop();
                    loadingOn = false;
                    if (!skirmish.empty())
                        mission.setAlliedVictory(setup.teams());
                    Error err = skirmish.empty() ? mission.start(renderer3D, single.mission(), 1, -1, now)
                                                 : mission.startSkirmish(renderer3D, setup.map(), skirmish,
                                                                         {shroud, fog}, start, now, setup.techLevel());
                    if (err) {
                        stopAll();
                        return err;
                    }
                    missionOn = true;
                    // The mission's own music instead of the shell's.
                    stopSound(_music);
                }
                _sys.delayMillis(kFrameMs);
                continue;
            }
            if (intro) {
                if (_intro.update(now))
                    _intro.draw(screen);
                else
                    intro = stopIntro();
            } else if (setupOn) {
                setup.draw(screen);
            } else if (loadGameOn) {
                loadGame.draw(screen);
            } else if (singleOn) {
                single.draw(screen, now);
            } else if (settingsOn) {
                settings.draw(screen);
            } else if (connectionOn) {
                connection.draw(screen);
            } else if (optionsOn) {
                options.draw(screen);
            } else if (menuOn) {
                menu.draw(screen, now);
            }
            _sys.updateScreen(screen);
            _sys.delayMillis(kFrameMs);
        }
        stopAll();
        // The game writes its preferences as it ends.
        const std::string prf = _preferences.text();
        (void)player().write(kPreferences, std::vector<uint8_t>(prf.begin(), prf.end()));
        return Error::success();
    }

private:
    PlayerFiles player() const { return PlayerFiles(_files, _target.savePath); }
    // Back from the Multiplayer Connection screen: typing off again.
    bool leaveConnection() {
        _sys.setTextInput(false);
        return false;
    }
    // The player's preferences and progress, as the game keeps them; a new
    // player's if they aren't there.
    void loadPlayer() {
        std::vector<uint8_t> data;
        _preferences = player().read(kPreferences, data)
                           ? Preferences::defaults(kDriver)
                           : Preferences::parse(
                                 std::string_view(reinterpret_cast<const char *>(data.data()), data.size()), kDriver);
        if (player().read("shell.set", data) || _progress.parse(data))
            _progress = ShellProgress{};
    }
    // The volumes, 0 to 10, on the mixer's scale.
    void applyVolumes() {
        if (!_mixer)
            return;
        constexpr std::pair<size_t, audio::SoundType> kVolumes[] = {
            {0, audio::SoundType::kMusic}, {1, audio::SoundType::kEffects}, {2, audio::SoundType::kSpeech}};
        for (const auto &[field, type] : kVolumes)
            _mixer->setTypeVolume(type, int(_preferences.number(Preferences::kSound, field) * audio::kMaxVolume / 10));
    }

    // The opening movie: the install's if it's been copied there, as the
    // game looks for it first; the installer leaves it on the CD.
    bool startIntro() {
        if (_intro.open(_files, "animations/stintro.bik"))
            return false;
        _intro.start(_sys.getMillis());
        if (_mixer && _intro.sound())
            _introSound = _mixer->play(_intro.sound(), audio::SoundType::kMusic);
        return true;
    }
    bool stopIntro() {
        stopSound(_introSound);
        return false;
    }
    audio::SoundHandle play(const std::string &name, audio::SoundType type, bool loop) {
        if (!_mixer)
            return audio::kNoSound;
        auto sound = std::make_shared<audio::Sound>();
        std::vector<uint8_t> data;
        if (_files.read(name, data) || audio::decodeWav(data, *sound))
            return audio::kNoSound;
        return _mixer->play(sound, type, audio::kMaxVolume, 0, loop);
    }
    void stopSound(audio::SoundHandle &handle) {
        if (_mixer && handle != audio::kNoSound)
            _mixer->stop(handle);
        handle = audio::kNoSound;
    }
    void stopAll() {
        stopSound(_music);
        stopSound(_introSound);
        if (_mixer)
            _sys.stopAudio();
        _mixer = nullptr;
    }

    static constexpr const char *kPreferences = "ARMADA.PRF";
    // The display driver the graphics settings name.
    static constexpr const char *kDriver = "Reluva";
    static constexpr int kWidth = 640;
    static constexpr int kHeight = 480;
    // About the game's own pace in its menu (59 frames a second).
    static constexpr uint32_t kFrameMs = 16;
    System &_sys;
    GameTarget _target;
    GameFiles _files;
    audio::Mixer *_mixer = nullptr;
    audio::SoundHandle _music = audio::kNoSound;
    audio::SoundHandle _introSound = audio::kNoSound;
    FullScreenMovie _intro;
    Preferences _preferences = Preferences::defaults(kDriver);
    ShellProgress _progress;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a Storm3dEngine to run one.
 */
class Storm3dMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "storm3d"; }
    const char *name() const override { return "Storm3D"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        return found.empty() ? detectFromTable(id(), kFallback, dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<Storm3dEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::storm3d

namespace reluva {

const MetaEngine &storm3dMetaEngine() {
    static const storm3d::Storm3dMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
