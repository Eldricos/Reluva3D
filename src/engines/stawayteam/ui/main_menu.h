// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/campaign.h"
#include "engines/stawayteam/ui/briefing.h"
#include "engines/stawayteam/ui/debriefing.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/credits.h"
#include "engines/stawayteam/ui/difficulty.h"
#include "engines/stawayteam/ui/settings.h"
#include "engines/stawayteam/ui/key_config.h"
#include "engines/stawayteam/ui/map_screen.h"
#include "engines/stawayteam/ui/save_menu.h"
#include "engines/stawayteam/ui/sound_options.h"
#include "engines/stawayteam/ui/team_selection.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/ui/font.h"

#include <array>
#include <filesystem>
#include <map>
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
     * Games are saved in `saves` (none without it).
     */
    MainMenu(System &sys, Surface &screen, const Archive &data, const std::filesystem::path &install,
             const TextObject &rules, GameOptions &options, audio::Mixer *mixer, int firstMission = 1,
             std::filesystem::path saves = {});
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
    // `p` as the mouse pointer, if it isn't already.
    void showCursor(const Animation::Picture *p);
    // The options' brightness over the screen as it's drawn.
    void brighten();
    // A new game: the first mission's briefing and team selection, those
    // it has.
    void startGame();
    // Mission `number`, from its briefing, its team selection or its map,
    // the first of those it has.
    void beginMission(int number);
    void showTeamSelection();
    // The mission's first map, `team` on it.
    void startMission(std::vector<TeamMember> team);
    // One of the mission's maps (of Levels/), loaded the first time; the
    // team's on it unless they're `arriving` later.
    MapScreen *openMap(const std::string &name, bool arriving);
    void showMap(MapScreen *map);
    void endMaps();
    // Officers sent between the maps, and the map shown for one chosen.
    void moveOfficers();
    // The settings' windows, over the main menu or a map.
    void drawWindows(uint64_t now);
    // The mission's music on its maps.
    void playMissionMusic();
    std::vector<TeamMember> startingTeam() const;

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
    // The mission's maps, by their names, and the one shown.
    std::map<std::string, std::unique_ptr<MapScreen>> _maps;
    MapScreen *_mapScreen = nullptr;
    Animation _pointer;                               // Interface/Cursors' Pointer
    const Animation::Picture *_cursorShown = nullptr; // the pointer as it was last set
    std::unique_ptr<DebriefingScreen> _debriefing;
    std::shared_ptr<audio::Sound> _missionMusic;
    audio::SoundHandle _missionSong = audio::kNoSound;
    int _songsPlayed = 0;
    unsigned _songRandom = 1;
    int _nextMission = 0; // the mission after the debriefing; 0 for the main menu
    Mission _mission;
    std::vector<TeamMember> _missionTeam; // the team it started with, for a restart
    Difficulty _difficulty = Difficulty::kNormal;
    int _firstMission;
    // The game's score and items found, and as they were as the mission
    // began: what a save keeps.
    Campaign _campaign;
    Campaign _missionStart;
    std::filesystem::path _saves;
    std::unique_ptr<SaveGameScreen> _saveScreen;
    // The save or load screen opened, saving the mission's start.
    void openSaveScreen(SaveGameScreen::Mode mode);
    // A saved game begun: its mission, from its briefing.
    void loadGame(const SaveGame &save);
};

} // namespace reluva::stawayteam
