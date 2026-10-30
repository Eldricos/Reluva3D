// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game being played: the mansion with the suspects walking in it, the
// bar with its ticker and the actions' icons, the cards and the notepad,
// suggestions and accusations (dialogInfo.res's in-game dialogs), the
// suggestions' videos, and the computer players' turns. The rules are
// rules.h's; where the pictures go, recordings of the original's
// (docs/engines/clue.md).

#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/system.h"
#include "common/video/avi.h"
#include "engines/clue/ai.h"
#include "engines/clue/board.h"
#include "engines/clue/game_data.h"
#include "engines/clue/mansion.h"
#include "engines/clue/resi.h"
#include "engines/clue/rules.h"
#include "engines/clue/ui.h"

#include <array>
#include <deque>
#include <functional>
#include <iosfwd>
#include <map>
#include <random>
#include <memory>
#include <string>
#include <vector>

namespace reluva::clue {

// Who plays a suspect, as the set-up screens choose.
struct Player {
    enum Kind { None, Human, Computer } kind = None;
    std::string name;
    int level = 1; // 0 novice, 1 normal, 2 expert
};

// The preferences the game looks at while it plays.
struct PlayLook {
    bool suggestMovies = true, autoNotes = true, butlerVoice = true, walkAnimations = true;
    MansionLook mansion;
};

class Play {
public:
    Play(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer, const Resi& resi,
         const Fonts& fonts, Mansion& mansion, const Level& level);
    ~Play();

    Error start(const std::array<Player, kSuspectCount>& players, bool noDieRoll, uint32_t seed);
    // Saved games: when it's a human's turn and nothing is going on.
    bool canSave() const;
    void save(std::ostream& out) const;
    Error load(std::istream& in); // in place of start()
    void setLook(const PlayLook& look) { look_ = look; }

    void handleEvent(const Event& ev);
    void update(uint64_t nowMs);
    void draw();
    void stopSounds();

    bool finished() const { return finished_; } // the end's Continue clicked
    int turn() const { return game_.turn(); }
    const Game& game() const { return game_; }

private:
    enum class Panel { None, Cards, Notepad };
    // A window of the game's over the view, waiting on a human.
    enum class Window {
        None,
        Choose,   // dialog 30: suggesting or accusing
        Pick,     // dialog 32: a human disproving picks a card
        Shown,    // dialog 33: the card shown to a human
        Solution, // dialog 34: the case file's three cards
        Loser,    // dialog 40: out; disproving by hand or not
    };
    struct Walker {
        int x = 0, y = 0;       // in the world
        double fx = 0, fy = 0;  // while walking
        int facing = 2;         // 0 n, 1 ne, 2 e, ... 7 nw
        std::deque<std::pair<int, int>> route; // world points still to walk to
        bool walking = false;
    };

    // The queue of what happens next, one step at a time, each when the
    // one before is over (a walk, a video, a window, a pause).
    void then(std::function<void()> step) { steps_.push_back(std::move(step)); }
    void pause(int ms);
    bool busy() const;
    void runSteps();

    void readTexts();
    void beginTurn();
    void computerTurn();
    void afterAction();
    void startWalk(const std::vector<Place>& path);
    void walkPlan();
    void placeWalkers(bool walk);
    std::pair<int, int> roomSpot(int suspect, int room) const;
    void suggestionMade();
    void accusationMade(const Accusation& a);
    void endTurn();
    void showTold(int suspect, bool can);
    void showSolution(int text);
    void openChoose(bool accuse);
    void refreshChoose();
    void openPick();
    int pickSlot(size_t i) const;
    void openShown();
    void openLoser();
    void clickChoose(int id);
    void clickPick(int id);
    int actionAt(int x, int y) const;
    void clickAction(int slot);
    bool clickHotSpot(int x, int y);
    void updateAmbience();
    void clickView(int x, int y);
    void clickSquare(const Place& p);
    void clickMap(int x, int y);
    void clickPanel(int x, int y);
    int viewer() const; // whose cards and notes the panels show
    bool humanTurn() const;

    std::string line(int suspect, int index) const; // TickerStrings.txt
    void say(const std::string& sentence);
    void drawTicker();
    void editNote(int card);
    std::shared_ptr<audio::Sound> loadSound(const std::string& file);
    void sound(const std::string& file, bool voice = false);
    int promptLine() const;
    void promptHuman();
    void startVideo(const std::string& file);
    void stopVideo();
    void centreOn(int suspect);

    void drawBar();
    void drawActions();
    void drawPanel();
    void drawMap();
    void drawWindows();
    void drawAnimation(const std::string& name, int x, int y, int frame = 0);
    std::vector<Mansion::Actor> actors();

    System& sys_;
    Surface& screen_;
    const GameData& data_;
    audio::Mixer* mixer_;
    const Resi& resi_;
    const Fonts& fonts_;
    Mansion& mansion_;
    const Level& level_;
    PlayLook look_;

    Board board_;
    Game game_;
    std::array<Player, kSuspectCount> players_;
    std::array<ComputerPlayer, kSuspectCount> ai_;
    std::array<bool, kSuspectCount> autoDisprove_{}; // humans out who chose Automatically
    uint32_t seed_ = 0;

    std::deque<std::function<void()>> steps_;
    uint64_t now_ = 0, waitUntil_ = 0;
    bool finished_ = false;

    // The view.
    int camX_ = 0, camY_ = 0;
    int mouseX_ = 320, mouseY_ = 200;
    int follow_ = -1; // the suspect the camera follows while walking
    std::array<Walker, kSuspectCount> walkers_;
    std::vector<Place> plan_; // a human's move being planned
    uint64_t rollStart_ = 0;  // the die rolling, for its animation

    // The bar, the panels and the windows.
    std::string ticker_;
    uint64_t tickerSince_ = 0;
    Surface tickerStrip_;
    std::vector<std::string> tickerLines_;
    std::vector<std::string> cardNames_; // Notepad.txt's, in card order
    std::array<std::string, 3> kindNames_; // its headings
    Panel panel_ = Panel::None;
    bool map_ = false; // the board seen from above, over the view
    int panelOwner_ = -1;
    // Each player's notepad: a row's colour (a suspect's, or -1), its two
    // marks (0 none, then tick, X, O, tick in O, X in O) and its note.
    struct NoteRow {
        int colour = -1;
        std::array<int, 2> marks{};
        std::string text;
    };
    std::array<std::array<NoteRow, kCardCount>, kSuspectCount> notes_;
    int editing_ = -1; // the card whose note is being typed
    // What each human was shown, and by whom (for the notepad).
    std::array<std::array<int, kCardCount>, kSuspectCount> shownBy_{};
    Window window_ = Window::None;
    std::unique_ptr<DialogView> dialog_;
    std::unique_ptr<DialogView> told_; // dialog 31: who can or can't disprove
    ResiDialog toldInfo_;              // its copy, with the suspect's background
    bool chooseAccuse_ = false;
    int choosePage_ = 0; // 0 suspect, 1 weapon, 2 room, 3 the summary
    Accusation chosen_;
    std::array<bool, 3> chosenSet_{};
    std::vector<int> pickCards_;
    int pickChosen_ = -1;
    int viewerOut_ = 0; // the human out, whom the loser's window asks
    int solutionText_ = 3;
    int pressedAction_ = -1;

    // A suggestion's video, in the right column.
    std::unique_ptr<video::VideoPlayer> video_;
    audio::SoundHandle videoSound_ = audio::kNoSound;
    uint64_t videoStart_ = 0;

    std::map<std::string, std::shared_ptr<audio::Sound>> sounds_;
    // The weather and the sounds of things in the house.
    std::mt19937 ambientRng_{12345};
    uint64_t nextThunder_ = 0, nextGust_ = 0;
    std::array<audio::SoundHandle, 2> ambient_{audio::kNoSound, audio::kNoSound};
    audio::SoundHandle voice_ = audio::kNoSound;
};

} // namespace reluva::clue
