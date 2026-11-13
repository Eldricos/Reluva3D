// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game as it starts: the intro, the menus and their screens (all from
// dialogInfo.res), setting up the players, then the game itself (play.h)
// with its Esc menu (see docs/engines/clue/systems/playing.md).

#include "audio/mixer.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/clue/game_data.h"
#include "engines/clue/formats/level.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/ui/movie.h"
#include "engines/clue/game/netplay.h"
#include "engines/clue/game/play.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/ui/ui.h"

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::clue {

// The six suspects, in the game's order (Miss Scarlet plays first).
constexpr int kSuspects = 6;

// Where the windows over the game go (the Esc menu and what it opens,
// dialogs 35-39 and 44; 231 x 395): Clue.exe 0x435C7F places its exit
// window at 320 less half its width across and in the middle between the
// top frame (5) and the bar (413) down.
constexpr int kOverGameX = 205, kOverGameY = 11;

// The preferences screen's settings, saved between runs.
struct Preferences {
    enum Switch {
        kSuggestMovies,
        kLighting,
        kTranslucency,
        kAutoNotes,
        kButlerVoice,
        kWalkAnimations,
        kMansionWalls,
        kSwitches
    };
    // The original's defaults: all on but Auto Notes, both volumes half.
    std::array<bool, kSwitches> on{true, true, true, false, true, true, true};
    int musicVolume = 50, masterVolume = 50; // 0-100

    void load(const std::filesystem::path &file);
    void save(const std::filesystem::path &file) const;
    // The original's Prefs.ini (16 bytes: the seven switches, a byte, the
    // music's and the master volume 0-10 as 32-bit numbers); false if it
    // isn't one.
    bool loadOriginal(const std::vector<uint8_t> &bytes);
};

class FrontEnd {
public:
    FrontEnd(System &sys, Surface &screen, const GameData &data, audio::Mixer *mixer, std::filesystem::path prefsFile);
    ~FrontEnd();
    Error open();

    void handleEvent(const Event &ev);
    void update(uint64_t nowMs);
    bool quit() const { return _quit; } // the player chose Exit Game, Yes
    void leave();                       // stops the sound when the engine switches away
    void resume();                      // and starts it again

private:
    enum class Screen {
        kIntro,
        kMainMenu,
        kStartChoice,
        kConnection,
        kNetGame,
        kLoadGame,
        kRulesMenu,
        kRules,
        kCredits,
        kCreditsText,
        kExit,
        kPicker,
        kSetup,
        kVariants,
        kPrefs,
        kLoading,
        kDeal,
        kGame,
        kGameMenu,
        kSaveGame,
        kExitConfirm,
        kGamePrefs,
        kGameRulesMenu,
        kGameRules, // the Esc menu's Preferences and How to Play
        // Network play (frontend_net.cpp).
        kNetHost,
        kNetJoin,
        kNetAddress,
        kNetLobby,
        kNetSetup,
        kNetMessage
    };

    void go(Screen s);
    // A window of the Esc menu's, over the game.
    bool overGame() const {
        return _at == Screen::kGameMenu || _at == Screen::kSaveGame || _at == Screen::kExitConfirm ||
               _at == Screen::kGamePrefs || _at == Screen::kGameRulesMenu || _at == Screen::kGameRules;
    }
    void onClick(int id);
    void showDialog(int id, int x = 0, int y = 0);
    void refreshPicker();
    void refreshSetup();
    void refreshPrefs(DialogView &view, int first, int musicId);
    void scrollText(DialogView &view, int textId, int knobId, int lines);
    std::string readText(const std::string &file) const;
    void startMovie(const std::string &file, Screen after);
    void stopMovie();
    void playMusic(bool inGame);
    void playTrack(int track);
    void stopMusic();
    void startGame();
    PlayLook playLook() const;
    // Saved games: eight slots, in the saves folder beside the preferences.
    static constexpr int kSaveSlots = 8;
    std::filesystem::path savesFolder() const;
    std::filesystem::path saveFile(int slot) const;
    std::string savedName(int slot) const; // empty for an empty slot
    Error saveGame(int slot, const std::string &name);
    Error loadGame(int slot);
    void drawCursor();
    void drawStorm();
    void menuThunder();
    void applyVolumes();

    // Network play (frontend_net.cpp).
    void netShow(Screen s);
    void netClick(int id);
    void netUpdate();
    void refreshLobby();
    void refreshNetSetup();
    void netLeave();
    void netLost(std::string message);
    std::unique_ptr<net::UdpSocket> netSocket(uint16_t port, std::string &error);

    System &_sys;
    Surface &_screen;
    const GameData &_data;
    audio::Mixer *_mixer;
    std::filesystem::path _prefsFile;
    Resi _resi;
    std::unique_ptr<Fonts> _fonts;
    std::unique_ptr<DialogView> _dialog;
    Screen _at = Screen::kIntro;
    Screen _prefsReturn = Screen::kMainMenu, _rulesReturn = Screen::kMainMenu;
    bool _quit = false;
    uint64_t _now = 0;
    int _mouseX = 320, _mouseY = 240;

    std::array<Player, kSuspects> _players;
    int _setupSuspect = 0;
    Player _setupDraft;
    bool _noDieRoll = false;
    int _rulesTopic = 0;
    Preferences _prefs;

    // A video playing full screen.
    std::unique_ptr<Movie> _movie;
    audio::SoundHandle _movieSound = audio::kNoSound;
    uint64_t _movieStart = 0;
    Screen _afterMovie = Screen::kMainMenu;

    // The CD's music, if ripped.
    audio::SoundHandle _music = audio::kNoSound;
    std::map<int, std::shared_ptr<audio::Sound>> _tracks;
    int _musicTrack = 0;
    bool _musicInGame = false;

    // The game.
    DataDrive _levelFile;
    Level _level;
    std::unique_ptr<Mansion> _mansion;
    std::unique_ptr<Play> _play;
    std::unique_ptr<DialogView> _gameMenu; // the Esc menu, or its Save window
    int _saveSlot = -1, _loadSlot = -1;

    // Network play: over the internet (by address) or the local network;
    // the session; the host's seat being set up and its draft; this
    // machine's name; a line to show on the message screen.
    bool _netInternet = false;
    std::unique_ptr<netplay::Session> _net;
    int _netSeat = 0;
    netplay::Seat _netDraft;
    int _netMaxHumans = 6;
    std::string _netName = "Player", _netMessage;
    std::vector<std::string> _netChat;
    // The menus' storm (Clue.exe 0x40C350/0x40C4A0): mnThndr1 looping under
    // them; a click on a storm screen plays mnThndr2 and shows the lightning
    // for 1 s before it does what it does.
    bool stormScreen() const;
    void startFlash(int id);
    audio::SoundHandle _stormSound = audio::kNoSound;
    uint64_t _flashUntil = 0;
    int _flashClick = -1;
    audio::SoundHandle _loadingSound = audio::kNoSound;
    std::vector<Event> _heldEvents;
};

} // namespace reluva::clue
