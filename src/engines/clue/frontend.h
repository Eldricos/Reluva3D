// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game as it starts: the intro, the menus and their screens (all from
// dialogInfo.res), setting up the players, and the mansion with the
// game's bar, before the rules are played (see docs/engines/clue.md).

#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/system.h"
#include "common/video/avi.h"
#include "engines/clue/game_data.h"
#include "engines/clue/level.h"
#include "engines/clue/mansion.h"
#include "engines/clue/resi.h"
#include "engines/clue/ui.h"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::clue {

// The six suspects, in the game's order (Miss Scarlet plays first).
constexpr int kSuspects = 6;

// The preferences screen's settings, saved between runs.
struct Preferences {
    enum Switch { SuggestMovies, Lighting, Translucency, AutoNotes, ButlerVoice, WalkAnimations, MansionWalls, kSwitches };
    std::array<bool, kSwitches> on{true, true, true, true, true, true, true};
    int musicVolume = 80, masterVolume = 100; // 0-100

    void load(const std::filesystem::path& file);
    void save(const std::filesystem::path& file) const;
};

class FrontEnd {
public:
    FrontEnd(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer,
             std::filesystem::path prefsFile);
    ~FrontEnd();
    Error open();

    void handleEvent(const Event& ev);
    void update(uint64_t nowMs);
    bool quit() const { return quit_; } // the player chose Exit Game, Yes
    void leave();                        // stops the sound when the engine switches away
    void resume();                       // and starts it again

private:
    enum class Screen {
        Intro, MainMenu, StartChoice, Connection, NetGame, LoadGame, RulesMenu, Rules, Credits, CreditsText,
        Exit, Picker, Setup, Variants, Prefs, Loading, Deal, Game, GameMenu
    };
    struct Player {
        enum Kind { None, Human, Computer } kind = None;
        std::string name;
        int level = 1; // 0 novice, 1 normal, 2 expert
    };

    void go(Screen s);
    void onClick(int id);
    void showDialog(int id, int x = 0, int y = 0);
    void refreshPicker();
    void refreshSetup();
    void refreshPrefs();
    void scrollText(int textId, int knobId, int lines);
    std::string readText(const std::string& file) const;
    void startMovie(const std::string& file, Screen after);
    void stopMovie();
    void playMusic(bool inGame);
    void playTrack(int track);
    void stopMusic();
    void startGame();
    void drawGame();
    void drawBar();
    void drawCursor();
    void drawStorm();
    void applyVolumes();

    System& sys_;
    Surface& screen_;
    const GameData& data_;
    audio::Mixer* mixer_;
    std::filesystem::path prefsFile_;
    Resi resi_;
    std::unique_ptr<Fonts> fonts_;
    std::unique_ptr<DialogView> dialog_;
    Screen at_ = Screen::Intro;
    Screen prefsReturn_ = Screen::MainMenu, rulesReturn_ = Screen::MainMenu;
    bool quit_ = false;
    uint64_t now_ = 0;
    int mouseX_ = 320, mouseY_ = 240;

    std::array<Player, kSuspects> players_;
    int setupSuspect_ = 0;
    Player setupDraft_;
    bool noDieRoll_ = false;
    int rulesTopic_ = 0;
    Preferences prefs_;

    // A video playing full screen.
    std::unique_ptr<video::VideoPlayer> movie_;
    audio::SoundHandle movieSound_ = audio::kNoSound;
    uint64_t movieStart_ = 0;
    Screen afterMovie_ = Screen::MainMenu;

    // The CD's music, if ripped.
    audio::SoundHandle music_ = audio::kNoSound;
    std::map<int, std::shared_ptr<audio::Sound>> tracks_;
    int musicTrack_ = 0;
    bool musicInGame_ = false;

    // The game.
    DataDrive levelFile_;
    Level level_;
    std::unique_ptr<Mansion> mansion_;
    int camX_ = 0, camY_ = 0;
    int turn_ = 0;                     // the suspect whose turn it is
    std::array<std::string, kSuspects> turnSentence_;
    std::unique_ptr<DialogView> gameMenu_;
};

} // namespace reluva::clue
