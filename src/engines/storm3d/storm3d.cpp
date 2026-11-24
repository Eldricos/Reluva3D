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
#include "engines/storm3d/shell/credits_screen.h"
#include "engines/storm3d/player_files.h"
#include "engines/storm3d/shell/full_screen_movie.h"
#include "engines/storm3d/shell/load_game_screen.h"
#include "engines/storm3d/shell/loading_screen.h"
#include "engines/storm3d/shell/main_menu.h"
#include "engines/storm3d/shell/multiplayer_screen.h"
#include "engines/storm3d/shell/options_screen.h"
#include "engines/storm3d/shell/single_player.h"

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
        // Back from the loading screen to the single-player screen, in 2D.
        auto endLoading = [&]() -> Error {
            loading.stop();
            loadingOn = false;
            return _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32);
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
                } else if (loadingOn) {
                    // The mission can't run yet: Esc gives up on it.
                    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                        if (Error err = endLoading()) {
                            stopAll();
                            return err;
                        }
                    }
                } else if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                    // Out a screen, as its Previous Menu does; out of the game from the menu.
                    if (settingsOn)
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
                        : loadGameOn   ? loadGame.pointerAt(ev.mouseX, ev.mouseY)
                        : settingsOn   ? settings.pointerAt(ev.mouseX, ev.mouseY)
                        : connectionOn ? connection.pointerAt(ev.mouseX, ev.mouseY)
                        : optionsOn    ? options.pointerAt(ev.mouseX, ev.mouseY)
                                       : menu.pointerAt(ev.mouseX, ev.mouseY, now))
                        play("sounds/effects/cornron.wav", audio::SoundType::kEffects, false);
                } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
                    if (loadGameOn)
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
                        gfx::Renderer3D *renderer = nullptr;
                        if (Error err = _sys.init3D(kWidth, kHeight, renderer)) {
                            stopAll();
                            return err;
                        }
                        if (Error err = loading.start(renderer, single.mission())) {
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
                    if (choice == ConnectionChoice::kPreviousMenu)
                        connectionOn = leaveConnection();
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
            if (loadingOn) {
                // The files read, the screen drawn in 3D; the mission
                // itself doesn't run yet, so it ends back at the races.
                const bool more = loading.step();
                loading.draw(now);
                if (!more) {
                    if (Error err = endLoading()) {
                        stopAll();
                        return err;
                    }
                }
                _sys.delayMillis(kFrameMs);
                continue;
            }
            if (intro) {
                if (_intro.update(now))
                    _intro.draw(screen);
                else
                    intro = stopIntro();
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
