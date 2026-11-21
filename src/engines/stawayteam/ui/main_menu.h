// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/ui/briefing.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/credits.h"
#include "engines/stawayteam/ui/difficulty.h"
#include "engines/stawayteam/ui/settings.h"
#include "engines/stawayteam/ui/key_config.h"
#include "engines/stawayteam/ui/sound_options.h"
#include "engines/stawayteam/ui/team_selection.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/ui/font.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The confirm dialog ("Are you sure you want to exit the game?"): a
 * question and Yes and No, laid out by Rules.txt's `Confirm Dialog`, in
 * the middle of the screen.
 */
class ConfirmDialog {
public:
    /** The dialog asking `question`, on a screen of that size; `data` must outlive it. */
    ConfirmDialog(const Archive &data, const TextObject &rules, std::string question, int screenW, int screenH);

    /** What `ev` does to it: true or false once it's answered, until then nothing. */
    std::optional<bool> handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen) const;

private:
    image::Image _background;
    Font _questionFont;
    std::array<Font, 2> _buttonFonts;
    ButtonGroup _buttons; // Yes, then No
    std::string _question;
    int _questionY = 0;
    // Its top left: in the middle of the screen (inferred).
    int _x;
    int _y;
};

/**
 * The main menu: what Rules.txt's `Main Menu` puts on its screen (its
 * "nernies": movies, looping, and pictures, each at its position), the
 * startup song, and its six buttons. Start New Game asks the difficulty,
 * Settings shows the settings, Credits the credits, Quit asks to be sure;
 * loading and network games aren't there yet.
 */
class MainMenu {
public:
    /**
     * The menu as `rules` describes it, its files from `data` and the
     * install's movies, the settings window changing `options`; `sys`,
     * `screen`, `data`, `options` and `mixer` (nullptr: no sound) must
     * outlive it. A new game starts at mission `firstMission` (1 to 18).
     */
    MainMenu(System &sys, Surface &screen, const Archive &data, const std::filesystem::path &install,
             const TextObject &rules, GameOptions &options, audio::Mixer *mixer, int firstMission = 1);
    ~MainMenu();

    /** Shown until the player quits (then true), or the window is closed (then false). */
    bool run();

    /**
     * What it found to show: "movie <name> at x,y", "picture <name> at
     * x,y", "button <label> at x,y".
     */
    const std::vector<std::string> &contents() const { return _contents; }

private:
    struct Part;

    // Draws the screen, its windows over it if there are any.
    void draw();
    // The options' brightness over the screen as it's drawn.
    void brighten();
    // A new game: the first mission's briefing and team selection, those
    // it has.
    void startGame();
    void showTeamSelection();

    System &_sys;
    Surface &_screen;
    const Archive &_data;
    std::filesystem::path _install;
    const TextObject &_rules;
    GameOptions &_options;
    audio::Mixer *_mixer;
    std::vector<std::unique_ptr<Part>> _parts;
    std::vector<std::string> _contents;
    std::shared_ptr<audio::Sound> _song;
    Font _font;
    ButtonGroup _buttons;
    std::unique_ptr<ConfirmDialog> _dialog;
    std::unique_ptr<CreditsWindow> _credits;
    std::unique_ptr<DifficultyWindow> _difficultyWindow;
    std::unique_ptr<SettingsWindow> _settings;
    std::unique_ptr<SoundOptionsWindow> _soundOptions;
    std::unique_ptr<KeyConfigScreen> _keyConfig;
    std::unique_ptr<TeamSelectionScreen> _teamSelection;
    std::unique_ptr<BriefingScreen> _briefing; // over the team selection, when it's from there
    Mission _mission;
    Difficulty _difficulty = Difficulty::kNormal;
    int _firstMission;
};

} // namespace reluva::stawayteam
