// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game being played: the mansion with the suspects walking in it, the
// bar with its ticker and the actions' icons, the cards and the notepad,
// suggestions and accusations (dialogInfo.res's in-game dialogs), the
// suggestions' videos, and the computer players' turns. The rules are
// rules.h's; where the pictures go, recordings of the original's
// (docs/engines/clue/systems/screen.md).

#include "audio/mixer.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/clue/game/ai.h"
#include "engines/clue/game/board.h"
#include "engines/clue/game_data.h"
#include "engines/clue/game/mansion.h"
#include "engines/clue/ui/movie.h"
#include "engines/clue/game/netplay.h"
#include "engines/clue/formats/resi.h"
#include "engines/clue/game/rules.h"
#include "engines/clue/ui/ui.h"

#include <array>
#include <deque>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <set>
#include <random>
#include <string>
#include <vector>

namespace reluva::clue {

// Clue.exe plays its sounds through Miles at the file's rate times (x + 1)
// / 64, x what most calls pass as 0x40 (0x4330C0, 0x4331F0): 65/64, a
// little faster and higher than recorded. A recording of the original has
// its weather, voices, steps and menu sounds so (the CD's music, the videos
// and the loading's loop not).
inline void playAsClueExe(audio::Sound &s) {
    s.rate = s.rate * 65 / 64;
}
// And at volume 100 of Miles' 127 (0x42EB80, 0x42ECB0, 0x42EBC0 pass 0x64),
// under the master volume.
constexpr int kClueSoundVolume = audio::kMaxVolume * 100 / 127;

// Who plays a suspect, as the set-up screens choose.
struct Player {
    enum Kind { kNone, kHuman, kComputer } kind = kNone;
    std::string name;
    int level = 1; // 0 novice, 1 normal, 2 expert
};

// The preferences the game looks at while it plays.
struct PlayLook {
    bool suggestMovies = true;
    bool autoNotes = true;
    bool butlerVoice = true;
    bool walkAnimations = true;
    MansionLook mansion;
};

class Play {
public:
    Play(System &sys, Surface &screen, const GameData &data, audio::Mixer *mixer, const Resi &resi, const Fonts &fonts,
         Mansion &mansion, const Level &level);
    ~Play();

    Error start(const std::array<Player, kSuspectCount> &players, bool noDieRoll, uint32_t seed);
    // A game over a network: the session's lobby and deal; this machine
    // plays its own suspect, the others' actions come through the session.
    Error startNetwork(netplay::Session &session);
    // Saved games: when it's a human's turn and nothing is going on.
    bool canSave() const;
    // What the music is to do, once: -1 stop, 2-4 that track; 0 nothing.
    int takeMusicCue() {
        const int cue = _musicCue;
        _musicCue = 0;
        return cue;
    }
    void save(std::ostream &out) const;
    Error load(std::istream &in); // in place of start()
    void setLook(const PlayLook &look) { _look = look; }

    void handleEvent(const Event &ev);
    void update(uint64_t nowMs);
    void draw();
    void stopSounds();

    bool finished() const { return _finished; } // the end's Continue clicked
    int turn() const { return _game.turn(); }
    const Game &game() const { return _game; }

private:
    enum class Panel { kNone, kCards, kNotepad };
    // A window of the game's over the view, waiting on a human.
    enum class Window {
        kNone,
        kChoose,   // dialog 30: suggesting or accusing
        kPick,     // dialog 32: a human disproving picks a card
        kShown,    // dialog 33: the card shown to a human
        kSolution, // dialog 34: the case file's three cards
        kLoser,    // dialog 40: out; disproving by hand or not
    };
    struct Walker {
        // in the world
        int x = 0;
        int y = 0;
        // while walking
        double fx = 0;
        double fy = 0;
        int facing = 2;                        // 0 n, 1 ne, 2 e, ... 7 nw
        std::deque<std::pair<int, int>> route; // world points still to walk to
        bool walking = false;
        int stepFrame = -1; // the walk's frame last stepped on
    };

    // The queue of what happens next, one step at a time, each when the
    // one before is over (a walk, a video, a window, a pause).
    void then(std::function<void()> step) { _steps.push_back(std::move(step)); }
    void pause(int ms);
    bool busy() const;
    void runSteps();

    void readTexts();
    void beginTurn();
    void computerTurn();
    void afterAction();
    void startWalk(const std::vector<Place> &path);
    void walkPlan();
    void placeWalkers(bool walk);
    // Where a suspect stands in a room, looked for from a point (Clue.exe's
    // 0x41C480): the walk grid's free cells around it, in the room.
    std::pair<int, int> roomSpot(int suspect, int room, int fromX, int fromY) const;
    void buildWalkGrid();
    // The way the suspect whose turn it is walks about its room to a point;
    // empty if it can't.
    std::deque<std::pair<int, int>> roomRoute(int worldX, int worldY) const;
    // Clue.exe's way-finder (0x41A0C0): a suspect's way in a room from one
    // point to another, round what stands there and, with aroundOthers,
    // round the other suspects too; the cells' middles, the first left out.
    // Empty if there's none.
    std::deque<std::pair<int, int>> wayFind(int suspect, int room, int fromX, int fromY, int toX, int toY,
                                            bool aroundOthers) const;
    // The cells the other suspects stand on, or are walking to.
    std::set<std::pair<int, int>> othersCells(int suspect) const;
    void suggestionMade();
    void accusationMade(const Accusation &a);
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
    bool reactAt(int x, int y, bool hover); // a hot spot's reaction, if one is there
    void updateAmbience();
    void clickView(int x, int y);
    void clickSquare(const Place &p);
    void clickMap(int x, int y);
    void clickPanel(int x, int y);
    int viewer() const; // whose cards and notes the panels show
    bool humanTurn() const;
    bool local(int seat) const; // a human playing at this machine
    bool localTurn() const;
    // A human's action: played out now, or over a network sent to the host
    // and played out when it comes back in order.
    void perform(const netplay::Command &c);
    void apply(const netplay::Command &c);

    std::string line(int suspect, int index) const; // TickerStrings.txt
    void say(const std::string &sentence);
    void drawTicker();
    void editNote(int card);
    std::shared_ptr<audio::Sound> loadSound(const std::string &file);
    void sound(const std::string &file, bool voice = false);
    int promptLine() const;
    void promptHuman();
    void startVideo(const std::string &file);
    void stopVideo();
    void centreOn(int suspect);

    void drawBar();
    void drawActions();
    void drawPanel();
    void drawMap();
    void drawWindows();
    void drawAnimation(const std::string &name, int x, int y, int frame = 0);
    std::vector<Mansion::Actor> actors();

    System &_sys;
    Surface &_screen;
    const GameData &_data;
    audio::Mixer *_mixer;
    const Resi &_resi;
    const Fonts &_fonts;
    Mansion &_mansion;
    const Level &_level;
    PlayLook _look;

    Board _board;
    Game _game;
    std::array<Player, kSuspectCount> _players;
    std::array<ComputerPlayer, kSuspectCount> _ai;
    std::array<bool, kSuspectCount> _autoDisprove{}; // humans out who chose Automatically
    // Over a network: the session, each human's machine, the actions to
    // play out, and whether this machine's is on its way.
    netplay::Session *_net = nullptr;
    std::array<int, kSuspectCount> _owner{};
    std::deque<netplay::Command> _incoming;
    bool _pending = false;
    bool _awaitingShow = false; // a human's card to show, waited for
    uint32_t _seed = 0;

    std::deque<std::function<void()>> _steps;
    uint64_t _now = 0;
    uint64_t _waitUntil = 0;
    bool _finished = false;

    // The view.
    int _camX = 0;
    int _camY = 0;
    int _mouseX = 320;
    int _mouseY = 200;
    int _follow = -1; // the suspect the camera follows while walking
    std::array<Walker, kSuspectCount> _walkers;
    // A computer player's next step, decided, and the waits made for it.
    bool _cpuPending = false;
    ComputerPlayer::Action _pendingAction = ComputerPlayer::Action::kEndTurn;
    int _waits = 0;
    // Clue.exe's walk grid: 32-unit cells, those under furniture, hot
    // spots, passages and walls blocked; and the secret passages (their
    // room, place and the way they face, 0 north ... 6 west).
    std::set<std::pair<int, int>> _blocked;
    struct Passage {
        int room = kNoRoom;
        int x = 0;
        int y = 0;
        int facing = 0;
    };
    std::vector<Passage> _passages;
    std::vector<Place> _plan; // a human's move being planned
    uint64_t _rollStart = 0;  // the die rolling, for its animation

    // The bar, the panels and the windows.
    std::string _ticker;
    uint64_t _tickerSince = 0;
    Surface _tickerStrip;
    std::unique_ptr<BitmapFont> _tickerFont;
    std::vector<std::string> _tickerLines;
    std::vector<std::string> _cardNames;   // Notepad.txt's, in card order
    std::array<std::string, 3> _kindNames; // its headings
    Panel _panel = Panel::kNone;
    bool _map = false; // the board seen from above, over the view
    int _panelOwner = -1;
    // Each player's notepad: a row's colour (a suspect's, or -1), its two
    // marks (0 none, then tick, X, O, tick in O, X in O) and its note.
    struct NoteRow {
        int colour = -1;
        std::array<int, 2> marks{};
        std::string text;
    };
    std::array<std::array<NoteRow, kCardCount>, kSuspectCount> _notes;
    int _editing = -1; // the card whose note is being typed
    // What each human was shown, and by whom (for the notepad).
    std::array<std::array<int, kCardCount>, kSuspectCount> _shownBy{};
    Window _window = Window::kNone;
    std::unique_ptr<DialogView> _dialog;
    std::unique_ptr<DialogView> _told; // dialog 31: who can or can't disprove
    ResiDialog _toldInfo;              // its copy, with the suspect's background
    bool _chooseAccuse = false;
    int _choosePage = 0; // 0 suspect, 1 weapon, 2 room, 3 the summary
    Accusation _chosen;
    std::array<bool, 3> _chosenSet{};
    std::vector<int> _pickCards;
    int _viewerOut = 0; // the human out, whom the loser's window asks
    int _solutionText = 3;
    int _pressedAction = -1;

    // A suggestion's video, in the right column.
    std::unique_ptr<Movie> _video;
    audio::SoundHandle _videoSound = audio::kNoSound;
    uint64_t _videoStart = 0;

    std::map<std::string, std::shared_ptr<audio::Sound>> _sounds;
    // The weather and the sounds of things in the house.
    int _musicCue = 0; // for the front end: -1 stop the music, 2-4 play that track
    std::mt19937 _ambientRng{12345};
    // The weather's cycle (Clue.exe's 0x41C690): where in it, its step, the
    // ticks not yet run, the gust looping.
    uint64_t _lastAmbience = 0;
    uint64_t _weatherTickMs = 0;
    int _weatherMs = 0;
    int _weatherStep = 0;
    audio::SoundHandle _gust = audio::kNoSound;
    std::array<audio::SoundHandle, 2> _ambient{audio::kNoSound, audio::kNoSound};
    audio::SoundHandle _voice = audio::kNoSound;
};

} // namespace reluva::clue
