// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Where things go and what they say is the original's, as recordings show
// it (docs/engines/clue.md); what follows what is the game's own texts'
// (Data/<lang>/Text); the pictures, sounds and dialogs are its files'.

#include "engines/clue/play.h"

#include "engines/clue/viewers.h"

#include "common/audio/decode.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>

namespace reluva::clue {

namespace {

constexpr int kW = 640;
constexpr int kBarTop = 413, kTickerTop = 462;
// The columns over the view: the cards and the notepad on the left, the
// choices, disproving and videos on the right.
constexpr int kLeftX = 5, kLeftY = 5, kRightX = 434, kRightY = 4;
// The card shown to a human, wider: from further left.
constexpr int kShownX = 398;
// The drawer's three places, over the logo: the die (then the number
// rolled, then the end of the turn), the bulb (suggest), the handcuffs.
constexpr int kDrawerX = 511, kSlotY = 420;
constexpr int kSlotX[3] = {535, 574, 606}, kSlotW[3] = {33, 27, 32}, kSlotH = 42;
// How fast suspects walk, in world units a second: a square in about 0.6 s,
// as measured on a recording.
constexpr int kWalkSpeed = 110;
// How long each "can / cannot disprove" box shows (about 3 s in recordings).
constexpr int kToldMs = 2800;
// The ticker: a sentence stays centred 8 s, then scrolls left at 3.5 pixels
// a second, coming round again from the right (as in recordings), between
// the ticker's ends.
constexpr int kTickerStillMs = 8000, kTickerSpeed = 35, kTickerLeft = 48, kTickerRight = 598;

// dialogInfo.res's in-game dialogs.
enum { kChooseDialog = 30, kToldDialog = 31, kPickDialog = 32, kShownDialog = 33, kSolutionDialog = 34,
       kLoserDialog = 40 };
constexpr int kReturn = 10000, kOk = 10001;

// TickerStrings.txt: 663 lines a suspect, in the suspects' order.
constexpr int kTickerBlock = 663;
enum TickerLine {
    kYourTurn = 0, kRoll = 1, kRollPassage = 2, kRollSuggest = 3, kRollPassageSuggest = 4, kChooseObject = 5,
    kMakeMove = 6, kMaySuggest = 7, kAccuseOrEnd = 8, kEndTurnLine = 9, kTrapped = 10, kCanDisprove = 11,
    kCannotDisprove = 12, kAccusedRight = 13, kAccusedWrong = 14, kSuggests = 15, kAccuses = 339,
};

// The suspects in resource names, in sound and video names, and in the
// level.
const char* const kKey[kSuspectCount] = {"sca", "mus", "whi", "gre", "pea", "plu"};
const char* const kVoice[kSuspectCount] = {"MS", "CM", "MW", "MG", "MP", "PP"};
const char* const kCharacter[kSuspectCount] = {"character:Oscarlet", "character:Omustard", "character:Owhite",
                                               "character:Ogreen",   "character:Opeacock", "character:Oplum"};
const bool kFemale[kSuspectCount] = {true, false, true, false, true, false};
// The cards in the small cards' names (UI:A<key>card), in card order.
const char* const kCardKey[kCardCount] = {"sca", "mus", "whi", "gre", "pea", "plu", "kni",
                                          "can", "rev", "rop", "pip", "wre", "hal", "lou",
                                          "din", "kit", "bal", "con", "bil", "lib", "stu"};
// The weapons in the videos' names (CM_rope.avi).
const char* const kVideoWeapon[kWeaponCount] = {"dagger", "candle", "gun", "rope", "pipe", "wrench"};
// The prompts' spoken lines.
const char* promptVoice(int line) {
    switch (line) {
    case kRoll: return "roll die.WAV";
    case kRollPassage: return "roll die or use passage.WAV";
    case kRollSuggest: return "roll die or suggest.WAV";
    case kRollPassageSuggest: return "roll die passage or suggest.WAV";
    case kMakeMove: return "please move.WAV";
    case kChooseObject: return "choose object.WAV";
    default: return nullptr;
    }
}

// Where cards go in the cards window (from the bottom right), and the
// holes in the choice window's summary for the suspect, room and weapon.
const int kHandX[6] = {111, 18, 111, 18, 111, 18}, kHandY[6] = {246, 246, 129, 129, 12, 12};
const UiRect kSummarySuspect{8, 40, 65, 173}, kSummaryRoom{89, 104, 98, 76}, kSummaryWeapon{90, 213, 73, 86};
// The notepad: each kind's first row (rows 15 apart), the columns for
// who holds a card and for the marks.
// Measured on a recording at 640x480: rows 15.2 apart from each kind's
// first; the columns of the colour, the two marks and the note's triangle.
const int kNoteRowY[3] = {20, 129, 239}, kNoteHeadY[3] = {4, 113, 223};
constexpr int kNoteTextX = 16, kNoteColourX = 139, kNoteMarkX[2] = {154, 170}, kNoteTriangleX = 184,
              kNoteRowH = 15;
int noteRowY(int kind, int row) { return kNoteRowY[kind] + row * 76 / 5; }
// The marks a click goes round, after none.
const char* const kMark[5] = {"UI:Anoteschk", "UI:Anotesx", "UI:Anoteso", "UI:Anoteschko", "UI:Anotesxo"};

// The board from above (UI:Aoverhead): where it goes, and its squares,
// 13 pixels each, column 9 and row 9 at (8, 15) in it.
constexpr int kMapX = 208, kMapY = 5, kMapW = 425, kMapH = 407;
constexpr int kMapSquare = 13, kMapCol0X = 8, kMapRow0Y = 15, kMapFirst = 9;

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<std::string> readLines(const GameData& data, const char* file) {
    std::vector<std::string> lines;
    if (const std::filesystem::path* p = data.find(file)) {
        const auto bytes = readFile(*p);
        std::istringstream in(std::string(bytes.begin(), bytes.end()));
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }
    return lines;
}

// The facing (0 n ... 7 nw; n is -y, e +x) of a step.
int facingOf(int dx, int dy) {
    const int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
    static const int kFacing[3][3] = {{7, 6, 5}, {0, 2, 4}, {1, 2, 3}}; // [sx + 1][sy + 1]
    if (sx == 0 && sy == 0)
        return -1;
    if (sx == 0)
        return sy < 0 ? 0 : 4;
    return kFacing[sx + 1][sy + 1] == 2 && sy != 0 ? (sy < 0 ? 1 : 3) : kFacing[sx + 1][sy + 1];
}

} // namespace

Play::Play(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer, const Resi& resi,
           const Fonts& fonts, Mansion& mansion, const Level& level)
    : sys_(sys), screen_(screen), data_(data), mixer_(mixer), resi_(resi), fonts_(fonts), mansion_(mansion),
      level_(level) {}

Play::~Play() { stopSounds(); }

Error Play::start(const std::array<Player, kSuspectCount>& players, bool noDieRoll, uint32_t seed) {
    if (Error err = board_.build(level_))
        return err;
    players_ = players;
    seed_ = seed;
    std::array<bool, kSuspectCount> playing{};
    for (int s = 0; s < kSuspectCount; ++s)
        playing[size_t(s)] = players[size_t(s)].kind != Player::None;
    if (Error err = game_.start(board_, playing, noDieRoll, seed))
        return err;
    // Every seat has a computer's mind: for the computer players, and for
    // humans out of the game who let it disprove for them.
    for (int s = 0; s < kSuspectCount; ++s)
        if (playing[size_t(s)])
            ai_[size_t(s)].reset(game_, s, seed * 7919u + uint32_t(s),
                                 players[size_t(s)].kind == Player::Computer ? players[size_t(s)].level
                                                                             : ComputerPlayer::Expert);
    for (int s = 0; s < kSuspectCount; ++s) {
        Board::standingPoint(board_.start(s), walkers_[size_t(s)].x, walkers_[size_t(s)].y);
        walkers_[size_t(s)].facing = 4;
    }
    for (auto& m : shownBy_)
        m.fill(-1);
    // Each player's notepad starts with their own cards in their colour,
    // ticked twice (as the original's does).
    for (int k = 0; k < kSuspectCount; ++k) {
        for (NoteRow& row : notes_[size_t(k)])
            row = {};
        for (int c : game_.seat(k).hand)
            notes_[size_t(k)][size_t(c)] = {k, {1, 1}, {}};
    }
    tickerLines_ = readLines(data_, "TickerStrings.txt");
    // The notepad's names: Notepad.txt's lines but its comments and the
    // kinds' headings.
    // Notepad.txt: each kind's heading, then its cards.
    cardNames_.clear();
    kindNames_ = {"Suspects", "Weapons", "Rooms"};
    size_t kind = 0;
    bool heading = true;
    for (const std::string& l : readLines(data_, "Notepad.txt")) {
        if (l.empty()) {
            heading = true;
            continue;
        }
        if (l.rfind("//", 0) == 0)
            continue;
        if (heading && kind < kindNames_.size())
            kindNames_[kind++] = l;
        else
            cardNames_.push_back(l);
        heading = false;
    }
    cardNames_.resize(kCardCount);
    beginTurn();
    return Error::success();
}

// ---- The sequence of events ------------------------------------------------

void Play::pause(int ms) {
    then([this, ms] { waitUntil_ = now_ + uint64_t(ms); });
}

bool Play::busy() const {
    if (now_ < waitUntil_ || video_)
        return true;
    if (window_ == Window::Pick || window_ == Window::Shown || window_ == Window::Solution ||
        window_ == Window::Loser)
        return true;
    return std::any_of(walkers_.begin(), walkers_.end(), [](const Walker& w) { return w.walking; });
}

void Play::runSteps() {
    while (!busy() && !steps_.empty()) {
        auto step = std::move(steps_.front());
        steps_.pop_front();
        step();
    }
}

bool Play::humanTurn() const { return players_[size_t(game_.turn())].kind == Player::Human; }

int Play::viewer() const {
    // The human whose turn it is; in a computer's turn, the human who
    // played last before it.
    for (int k = 0; k < kSuspectCount; ++k) {
        const int s = (game_.turn() - k + kSuspectCount) % kSuspectCount;
        if (players_[size_t(s)].kind == Player::Human)
            return s;
    }
    return -1;
}

void Play::beginTurn() {
    const int s = game_.turn();
    plan_.clear();
    // Hot seat: one human's cards aren't left open for the next.
    if (panel_ != Panel::None && viewer() != panelOwner_)
        panel_ = Panel::None;
    then([this, s] {
        say(line(s, kYourTurn));
        sound(std::string("turn_") + kVoice[s] + ".WAV", true);
        centreOn(s);
    });
    pause(1200);
    if (humanTurn())
        then([this] { promptHuman(); });
}

int Play::promptLine() const {
    switch (game_.phase()) {
    case Game::Phase::Start:
        if (game_.trapped())
            return kTrapped;
        if (game_.noDieRoll())
            return game_.canSuggest() ? kMaySuggest : kMakeMove;
        if (game_.canTakePassage())
            return game_.canSuggest() ? kRollPassageSuggest : kRollPassage;
        return game_.canSuggest() ? kRollSuggest : kRoll;
    case Game::Phase::Moving: return kMakeMove;
    case Game::Phase::Moved: return game_.canSuggest() ? kMaySuggest : kAccuseOrEnd;
    default: return kAccuseOrEnd;
    }
}

void Play::promptHuman() {
    // Spoken only: the ticker keeps "..., it is your turn." through a
    // human's turn, as recorded (the prompts' sentences are in
    // TickerStrings.txt all the same).
    if (const char* v = promptVoice(promptLine()))
        sound(v, true);
}

void Play::afterAction() {
    if (game_.phase() != Game::Phase::Over && humanTurn())
        promptHuman();
}

void Play::computerTurn() {
    const int s = game_.turn();
    ComputerPlayer& me = ai_[size_t(s)];
    switch (me.decide(game_)) {
    case ComputerPlayer::Action::Roll:
        game_.rollDie();
        rollStart_ = now_;
        sound("dice shake and roll.WAV");
        pause(900);
        break;
    case ComputerPlayer::Action::Walk: {
        const auto path = me.chooseWalk(game_);
        if (game_.walk(path))
            game_.endTurn(); // can't be: the path is one the rules gave
        startWalk(path);
        pause(400);
        break;
    }
    case ComputerPlayer::Action::Passage:
        game_.takePassage();
        placeWalkers(false);
        centreOn(s);
        pause(800);
        break;
    case ComputerPlayer::Action::Suggest: {
        const Accusation a = me.chooseSuggestion(game_);
        game_.suggest(a.suspect, a.weapon);
        suggestionMade();
        break;
    }
    case ComputerPlayer::Action::Accuse: accusationMade(me.chooseAccusation()); break;
    case ComputerPlayer::Action::EndTurn: endTurn(); break;
    }
    pause(500);
}

void Play::endTurn() {
    if (game_.endTurn())
        return;
    beginTurn();
}

// Where a suspect stands in a room: its first place no one else stands on.
std::pair<int, int> Play::roomSpot(int suspect, int room) const {
    for (const Square& sq : board_.standing(room)) {
        int x, y;
        Board::standingPoint(sq, x, y);
        bool taken = false;
        for (int k = 0; k < kSuspectCount; ++k) {
            if (k == suspect)
                continue;
            const Walker& w = walkers_[size_t(k)];
            const int ex = w.route.empty() ? w.x : w.route.back().first;
            const int ey = w.route.empty() ? w.y : w.route.back().second;
            taken = taken || (ex == x && ey == y);
        }
        if (!taken)
            return {x, y};
    }
    int x = 0, y = 0;
    if (!board_.standing(room).empty())
        Board::standingPoint(board_.standing(room).front(), x, y);
    return {x, y};
}

void Play::startWalk(const std::vector<Place>& path) {
    // The game has the suspect there already; this walks the picture.
    const int s = game_.turn();
    Walker& w = walkers_[size_t(s)];
    w.route.clear();
    const auto point = [](const Square& sq) {
        int x, y;
        Board::standingPoint(sq, x, y);
        return std::pair<int, int>{x, y};
    };
    // Out of a room: to the inside of the door taken first.
    if (!path.empty() && !path.front().inRoom())
        for (const Board::Door& d : board_.doors())
            if (d.outside == path.front().square && board_.roomAt(board_.squareAt(w.x, w.y)) == d.room) {
                w.route.push_back(point(d.inside));
                break;
            }
    for (size_t i = 0; i < path.size(); ++i) {
        const Place& p = path[i];
        if (!p.inRoom()) {
            w.route.push_back(point(p.square));
            continue;
        }
        // Into a room: through its door, to a free place in it.
        for (const Board::Door& d : board_.doors())
            if (d.room == p.room && i > 0 && d.outside == path[i - 1].square) {
                w.route.push_back(point(d.inside));
                break;
            }
        w.route.push_back(roomSpot(s, p.room));
    }
    if (!look_.walkAnimations || w.route.empty()) {
        if (!w.route.empty()) {
            w.x = w.route.back().first;
            w.y = w.route.back().second;
        }
        w.route.clear();
        centreOn(s);
        return;
    }
    w.walking = true;
    w.fx = w.x;
    w.fy = w.y;
    follow_ = s;
}

void Play::placeWalkers(bool walk) {
    (void)walk;
    // Each suspect to where the game has them, at once: after a passage,
    // after being brought in by a suggestion, after being moved off a door.
    for (int s = 0; s < kSuspectCount; ++s) {
        Walker& w = walkers_[size_t(s)];
        if (w.walking)
            continue;
        const Place& p = game_.place(s);
        if (p.inRoom()) {
            if (board_.roomAt(board_.squareAt(w.x, w.y)) != p.room) {
                const auto [x, y] = roomSpot(s, p.room);
                w.x = x;
                w.y = y;
            }
        } else
            Board::standingPoint(p.square, w.x, w.y);
    }
}

void Play::suggestionMade() {
    const int s = game_.turn();
    const Accusation a = game_.suggestion();
    say(line(s, kSuggests + a.suspect * kWeaponCount * kRoomCount + a.weapon * kRoomCount + a.room));
    placeWalkers(false); // the suspect named, brought in
    pause(1800);
    if (look_.suggestMovies)
        then([this, a] { startVideo(std::string(kVoice[a.suspect]) + "_" + kVideoWeapon[a.weapon] + ".avi"); });
    // Those who can't disprove it, in turn, and the one who can.
    for (int p : game_.passed()) {
        then([this, p] {
            showTold(p, false);
            say(line(p, kCannotDisprove));
            sound(std::string("cannot disprove ") + kVoice[p] + ".WAV", true);
        });
        pause(kToldMs);
    }
    const int d = game_.disprover();
    if (d >= 0) {
        then([this, d] {
            showTold(d, true);
            say(line(d, kCanDisprove));
            sound(std::string("can disprove ") + kVoice[d] + ".WAV", true);
        });
        pause(kToldMs);
        then([this, d, s] {
            if (players_[size_t(d)].kind == Player::Human && !autoDisprove_[size_t(d)])
                openPick();
            else
                game_.show(ai_[size_t(d)].chooseShow(game_, s));
        });
        then([this, s] {
            told_.reset();
            if (players_[size_t(s)].kind == Player::Human)
                openShown();
        });
    }
    then([this, s, a, d] {
        told_.reset();
        for (int k = 0; k < kSuspectCount; ++k)
            if (players_[size_t(k)].kind != Player::None)
                ai_[size_t(k)].observe(game_);
        if (game_.shown() >= 0) {
            shownBy_[size_t(s)][size_t(game_.shown())] = d;
            // Auto Notes: the card crossed off in the colour of who showed it.
            NoteRow& row = notes_[size_t(s)][size_t(game_.shown())];
            if (look_.autoNotes && row.colour < 0 && row.marks[0] == 0 && row.marks[1] == 0)
                row = {d, {2, 2}, row.text};
        }
        afterAction();
    });
}

void Play::accusationMade(const Accusation& a) {
    const int s = game_.turn();
    say(line(s, kAccuses + a.suspect * kWeaponCount * kRoomCount + a.weapon * kRoomCount + a.room));
    pause(2000);
    then([this, s, a] {
        game_.accuse(a);
        if (game_.winner() == s) {
            // Right: the solution, then (Continue) the murderer in prison,
            // then the main menu (as recorded).
            say(line(s, kAccusedRight));
            sound(std::string("accused correct ") + kVoice[s] + ".WAV", true);
            pause(1500);
            then([this] { showSolution(3); });
            if (look_.suggestMovies) {
                const int murderer = game_.solution().suspect;
                then([this, murderer] { startVideo(std::string(kVoice[murderer]) + "_prison.avi"); });
            }
            then([this] { finished_ = true; });
            return;
        }
        for (int k = 0; k < kSuspectCount; ++k)
            if (players_[size_t(k)].kind != Player::None)
                ai_[size_t(k)].observeAccusation(a);
        say(line(s, kAccusedWrong));
        sound(std::string("accuse incorrect ") + kVoice[s] + ".WAV", true);
        placeWalkers(false); // off a doorway
        pause(1500);
        // With no human left in the game, it is over: the solution, then
        // the main menu. Else a human out is asked how to go on disproving.
        bool humansIn = false;
        for (int k = 0; k < kSuspectCount; ++k)
            humansIn = humansIn || (players_[size_t(k)].kind == Player::Human && !game_.seat(k).out);
        if (game_.phase() == Game::Phase::Over || !humansIn) {
            const bool everyone = game_.phase() == Game::Phase::Over;
            then([this, everyone] { showSolution(everyone ? 5 : 6); });
            then([this] { finished_ = true; });
            return;
        }
        if (players_[size_t(s)].kind == Player::Human)
            then([this, s] {
                viewerOut_ = s;
                openLoser();
            });
        beginTurn(); // the rules have gone on to the next player
    });
}

// ---- The windows ----------------------------------------------------------------

void Play::showTold(int suspect, bool can) {
    // Dialog 31, with the suspect's own portrait: the box at the top right.
    const ResiDialog* d = resi_.dialog(kToldDialog);
    if (!d) {
        told_.reset();
        return;
    }
    toldInfo_ = *d;
    toldInfo_.background = std::string("dialog:A") + kKey[suspect] + "Dis1Bg";
    told_ = std::make_unique<DialogView>(data_, fonts_, toldInfo_, kRightX, kRightY);
    told_->setGroupVisible(1, false);
    told_->setText(0, line(suspect, can ? kCanDisprove : kCannotDisprove));
}

void Play::openChoose(bool accuse) {
    const ResiDialog* d = resi_.dialog(kChooseDialog);
    if (!d)
        return;
    dialog_ = std::make_unique<DialogView>(data_, fonts_, *d, kRightX, kRightY);
    window_ = Window::Choose;
    chooseAccuse_ = accuse;
    choosePage_ = 0;
    chosenSet_ = {false, false, !accuse};
    chosen_ = {0, 0, accuse ? 0 : game_.place(game_.turn()).room};
    refreshChoose();
}

void Play::refreshChoose() {
    // The window's pages are its controls' groups of ids: the suspects
    // (0-7), the weapons (8-15), the rooms (16-26), the summary (27-35).
    dialog_->setGroupVisible(1, false);
    dialog_->setVisible(0, choosePage_ == 0);
    const auto show = [&](int from, int to) {
        for (int id = from; id <= to; ++id)
            dialog_->setVisible(id, true);
    };
    switch (choosePage_) {
    case 0: show(1, 7); break;
    case 1: show(8, 15); break;
    case 2: show(16, 26); break;
    default:
        for (int id : {29, 30, 31, 32, 35})
            dialog_->setVisible(id, true);
        dialog_->setVisible(chooseAccuse_ ? 33 : 27, true); // Accuse or Suggest
        dialog_->setVisible(chooseAccuse_ ? 34 : 28, true); // "You have accused..." or "suggested..."
        break;
    }
}

void Play::clickChoose(int id) {
    const auto next = [&]() {
        if (!chosenSet_[0])
            return 0;
        if (!chosenSet_[1])
            return 1;
        if (!chosenSet_[2])
            return 2;
        return 3;
    };
    if (id >= 2 && id <= 7) {
        chosen_.suspect = id - 2;
        chosenSet_[0] = true;
    } else if (id >= 10 && id <= 15) {
        chosen_.weapon = id - 10;
        chosenSet_[1] = true;
    } else if (id >= 18 && id <= 26) {
        chosen_.room = id - 18;
        chosenSet_[2] = true;
    } else if (id == 35) {
        window_ = Window::None;
        dialog_.reset();
        return;
    } else if (id == 27 || id == 33) {
        window_ = Window::None;
        dialog_.reset();
        if (id == 27 && !game_.suggest(chosen_.suspect, chosen_.weapon))
            suggestionMade();
        else if (id == 33)
            accusationMade(chosen_);
        return;
    } else
        return;
    sound("click.wav");
    choosePage_ = next();
    refreshChoose();
}

void Play::openPick() {
    const ResiDialog* d = resi_.dialog(kPickDialog);
    if (!d)
        return;
    const int who = game_.disprover();
    dialog_ = std::make_unique<DialogView>(data_, fonts_, *d, kRightX, kRightY);
    window_ = Window::Pick;
    pickCards_ = game_.disproving();
    pickChosen_ = -1;
    dialog_->setGroupVisible(1, false);
    dialog_->setVisible(who, true);       // the disprover's background
    // "..., Please click on the card ...", then, where it was, Done.
    dialog_->setVisible(17 + who, true);
    dialog_->setVisible(kOk, false);
    dialog_->setText(16, "");
    // One card in the middle; two or three in the window's other places.
    static const int kOne[] = {12}, kMore[] = {13, 14, 15};
    const int* slots = pickCards_.size() == 1 ? kOne : kMore;
    for (size_t i = 0; i < pickCards_.size() && i < 3; ++i)
        dialog_->setVisible(slots[i], true);
}

int Play::pickSlot(size_t i) const {
    static const int kOne[] = {12}, kMore[] = {13, 14, 15};
    return pickCards_.size() == 1 ? kOne[0] : kMore[std::min<size_t>(i, 2)];
}

void Play::clickPick(int id) {
    for (size_t i = 0; i < pickCards_.size(); ++i)
        if (id == pickSlot(i)) {
            pickChosen_ = int(i);
            for (size_t j = 0; j < pickCards_.size(); ++j)
                dialog_->setChosen(pickSlot(j), j == i);
            dialog_->setVisible(17 + game_.disprover(), false);
            dialog_->setVisible(kOk, true);
            sound("click.wav");
            return;
        }
    if (id == kOk && pickChosen_ >= 0) {
        game_.show(pickCards_[size_t(pickChosen_)]);
        window_ = Window::None;
        dialog_.reset();
    }
}

void Play::openShown() {
    const ResiDialog* d = resi_.dialog(kShownDialog);
    if (!d || game_.shown() < 0)
        return;
    dialog_ = std::make_unique<DialogView>(data_, fonts_, *d, kShownX, kRightY);
    window_ = Window::Shown;
    sound("SDcard.WAV");
}

void Play::showSolution(int text) {
    const ResiDialog* d = resi_.dialog(kSolutionDialog);
    if (!d)
        return;
    dialog_ = std::make_unique<DialogView>(data_, fonts_, *d, 0, 0);
    window_ = Window::Solution;
    solutionText_ = text;
    for (int id : {3, 4, 5, 6})
        dialog_->setVisible(id, id == text);
    if (text == 3 && game_.winner() >= 0)
        dialog_->setText(3, line(game_.winner(), kAccusedRight));
}

void Play::openLoser() {
    const ResiDialog* d = resi_.dialog(kLoserDialog);
    if (!d)
        return;
    dialog_ = std::make_unique<DialogView>(data_, fonts_, *d, (kW - 231) / 2, (kBarTop - 395) / 2);
    window_ = Window::Loser;
}

// ---- Clicks -------------------------------------------------------------------

void Play::handleEvent(const Event& ev) {
    if (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseUp) {
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
    }
    if (ev.type == EventType::KeyDown) {
        const int step = (ev.mods & keymod::Shift) ? 128 : 32;
        if (ev.key == key::Left)
            camX_ -= step;
        else if (ev.key == key::Right)
            camX_ += step;
        else if (ev.key == key::Up)
            camY_ -= step;
        else if (ev.key == key::Down)
            camY_ += step;
    }
    // Typing a note.
    if (editing_ >= 0 && panelOwner_ >= 0) {
        std::string& text = notes_[size_t(panelOwner_)][size_t(editing_)].text;
        if (ev.type == EventType::TextInput) {
            text += typedText(ev.text);
            if (text.size() > 40)
                text.resize(40);
            return;
        }
        if (ev.type == EventType::KeyDown && ev.key == key::Backspace) {
            if (!text.empty())
                text.pop_back();
            return;
        }
        if (ev.type == EventType::KeyDown && (ev.key == key::Return || ev.key == key::Escape)) {
            editNote(-1);
            return;
        }
        if (ev.type == EventType::MouseUp &&
            !(ev.mouseX >= kLeftX && ev.mouseX < kLeftX + 202 && ev.mouseY >= kLeftY && ev.mouseY < kLeftY + 408))
            editNote(-1);
    }
    // A window takes the clicks while it is open.
    if (window_ != Window::None && dialog_) {
        if (window_ == Window::Choose && choosePage_ == 3 && ev.type == EventType::MouseUp) {
            // The summary's pictures: click one to choose it again.
            const int x = ev.mouseX - kRightX, y = ev.mouseY - kRightY;
            const int page = kSummarySuspect.contains(x, y)                 ? 0
                             : kSummaryWeapon.contains(x, y)                ? 1
                             : kSummaryRoom.contains(x, y) && chooseAccuse_ ? 2
                                                                            : -1;
            if (page >= 0) {
                choosePage_ = page;
                refreshChoose();
                return;
            }
        }
        const int id = dialog_->handleEvent(ev);
        if (id < 0)
            return;
        switch (window_) {
        case Window::Choose: clickChoose(id); break;
        case Window::Pick: clickPick(id); break;
        case Window::Shown:
        case Window::Solution:
            if (id == kOk) {
                window_ = Window::None;
                dialog_.reset();
            }
            break;
        case Window::Loser:
            if (id == kOk || id == kReturn) {
                autoDisprove_[size_t(viewerOut_)] = id == kReturn;
                window_ = Window::None;
                dialog_.reset();
            }
            break;
        default: break;
        }
        // A click in the window is the window's alone; elsewhere, the view's.
        if (window_ == Window::Choose)
            return;
        if (window_ != Window::None)
            return;
    }
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        pressedAction_ = actionAt(ev.mouseX, ev.mouseY);
        return;
    }
    if (ev.type != EventType::MouseUp || ev.button != MouseButton::Left)
        return;
    const int pressed = pressedAction_;
    pressedAction_ = -1;
    const int x = ev.mouseX, y = ev.mouseY;
    if (y >= kBarTop) {
        // The bar: the notes and cards, the actions.
        if (x >= 39 && x < 84) {
            panel_ = panel_ == Panel::None ? Panel::Cards : Panel::None;
            panelOwner_ = viewer();
            sound("click.wav");
        } else if (x >= 84 && x < 139) {
            map_ = !map_;
            sound("click.wav");
        }
        const int slot = actionAt(x, y);
        if (slot >= 0 && slot == pressed)
            clickAction(slot);
        return;
    }
    if (panel_ != Panel::None && x >= kLeftX && x < kLeftX + 202 && y >= kLeftY && y < kLeftY + 408) {
        clickPanel(x - kLeftX, y - kLeftY);
        return;
    }
    if (busy() || !steps_.empty() || !humanTurn())
        return;
    if (map_ && x >= kMapX && x < kMapX + kMapW && y >= kMapY && y < kMapY + kMapH)
        clickMap(x - kMapX, y - kMapY);
    else
        clickView(x, y);
}

int Play::actionAt(int x, int y) const {
    if (!humanTurn() || game_.phase() == Game::Phase::Over)
        return -1;
    for (int k = 0; k < 3; ++k)
        if (x >= kSlotX[k] && x < kSlotX[k] + kSlotW[k] && y >= kSlotY && y < kSlotY + kSlotH)
            return k;
    return -1;
}

void Play::clickAction(int slot) {
    if (busy() || !steps_.empty() || window_ != Window::None)
        return;
    if (slot == 0) {
        if (game_.canRoll()) {
            game_.rollDie();
            rollStart_ = now_;
            plan_.clear();
            sound("dice shake and roll.WAV");
            pause(900);
            then([this] { afterAction(); });
        } else if (game_.canEndTurn()) {
            sound("click.wav");
            endTurn();
        } else if (game_.noDieRoll() && game_.canMove() && !plan_.empty() && game_.canWalk(plan_))
            walkPlan();
        else
            sound("nogo.wav");
    } else if (slot == 1) {
        if (game_.canSuggest())
            openChoose(false);
        else
            sound("nogo.wav");
    } else if (slot == 2) {
        if (game_.canAccuse())
            openChoose(true);
        else
            sound("nogo.wav");
    }
}

void Play::walkPlan() {
    const std::vector<Place> path = plan_;
    plan_.clear();
    if (game_.walk(path)) {
        sound("nogo.wav");
        return;
    }
    startWalk(path);
    then([this] { afterAction(); });
}

void Play::clickView(int x, int y) {
    const int s = game_.turn();
    // A secret passage, clicked in a room that has one.
    if (game_.canTakePassage())
        for (const Mansion::Item& item : mansion_.items())
            if (item.object->type == ObjectDef::SecretPassage && item.rect.contains(x + camX_, y + camY_) &&
                board_.roomAt(Board::squareAt(item.box.x, item.box.y)) == game_.place(s).room) {
                game_.takePassage();
                plan_.clear();
                sound("door open.wav");
                placeWalkers(false);
                centreOn(s);
                pause(500);
                then([this] { afterAction(); });
                return;
            }
    if (!game_.canMove())
        return;
    // The floor's square under the pointer: x - y and (x + y) / 2 undone.
    const int sx = x + camX_, sy = y + camY_;
    const Square sq = Board::squareAt(sy + floorDiv(sx, 2), sy - floorDiv(sx, 2));
    if (board_.isCorridor(sq))
        clickSquare({sq, kNoRoom});
    else if (const int room = board_.roomAt(sq); room != kNoRoom)
        clickSquare({{}, room});
    else
        sound("nogo.wav");
}

void Play::clickMap(int x, int y) {
    const Square sq{kMapFirst + floorDiv(x - kMapCol0X, kMapSquare), kMapFirst + floorDiv(y - kMapRow0Y, kMapSquare)};
    const int room = board_.roomAt(sq);
    // From a corner room, the room in the opposite corner: the passage.
    if (room != kNoRoom && game_.canTakePassage() && board_.passage(game_.place(game_.turn()).room) == room) {
        game_.takePassage();
        plan_.clear();
        sound("door open.wav");
        placeWalkers(false);
        centreOn(game_.turn());
        pause(500);
        then([this] { afterAction(); });
        return;
    }
    if (!game_.canMove())
        return;
    if (board_.isCorridor(sq))
        clickSquare({sq, kNoRoom});
    else if (room != kNoRoom)
        clickSquare({{}, room});
    else
        sound("nogo.wav");
}

void Play::clickSquare(const Place& p) {
    const int s = game_.turn();
    // A square of the plan: back to it (the last one again: go).
    const auto it = std::find(plan_.begin(), plan_.end(), p);
    if (it != plan_.end()) {
        if (it + 1 == plan_.end() && game_.canWalk(plan_))
            walkPlan();
        else
            plan_.erase(it + 1, plan_.end());
        return;
    }
    if (p == game_.place(s)) {
        plan_.clear();
        return;
    }
    const auto next = game_.nextSteps(plan_);
    if (std::find(next.begin(), next.end(), p) == next.end()) {
        sound("nogo.wav");
        return;
    }
    plan_.push_back(p);
    // The die's number reached, or a room: the walk begins.
    const int limit = game_.noDieRoll() ? game_.moves() : game_.roll();
    if (p.inRoom() || int(plan_.size()) == limit)
        walkPlan();
}

void Play::clickPanel(int x, int y) {
    if (panel_ == Panel::Cards) {
        if (x < 50 && y >= 352)
            panel_ = Panel::Notepad;
        return;
    }
    if (x < 45 && y >= 372) {
        panel_ = Panel::Cards;
        return;
    }
    // A row of the notepad: its colour goes round the suspects', each mark
    // round the marks; its triangle opens the row's note.
    const int who = panelOwner_;
    if (who < 0)
        return;
    static const int kFirst[3] = {suspectCard(0), weaponCard(0), roomCard(0)};
    static const int kCount[3] = {kSuspectCount, kWeaponCount, kRoomCount};
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < kCount[k]; ++r) {
            const int top = noteRowY(k, r);
            if (y < top || y >= top + kNoteRowH)
                continue;
            const int card = kFirst[k] + r;
            NoteRow& row = notes_[size_t(who)][size_t(card)];
            if (x >= kNoteColourX && x < kNoteMarkX[0])
                row.colour = row.colour + 1 < kSuspectCount ? row.colour + 1 : -1;
            else if (x >= kNoteMarkX[0] && x < kNoteMarkX[1])
                row.marks[0] = (row.marks[0] + 1) % 6;
            else if (x >= kNoteMarkX[1] && x < kNoteTriangleX)
                row.marks[1] = (row.marks[1] + 1) % 6;
            else if (x >= kNoteTriangleX)
                editNote(editing_ == card ? -1 : card);
            else
                return;
            sound("click.wav");
            return;
        }
}

void Play::editNote(int card) {
    editing_ = card;
    sys_.setTextInput(card >= 0);
}

// ---- Time --------------------------------------------------------------------

void Play::update(uint64_t nowMs) {
    const uint64_t dt = now_ == 0 ? 0 : std::min<uint64_t>(nowMs - now_, 100);
    now_ = nowMs;
    // The walks.
    for (int s = 0; s < kSuspectCount; ++s) {
        Walker& w = walkers_[size_t(s)];
        if (!w.walking)
            continue;
        double left = double(kWalkSpeed) * double(dt) / 1000.0;
        while (left > 0 && !w.route.empty()) {
            const auto [tx, ty] = w.route.front();
            const double dx = tx - w.fx, dy = ty - w.fy, d = std::hypot(dx, dy);
            if (const int f = facingOf(tx - int(std::lround(w.fx)), ty - int(std::lround(w.fy))); f >= 0)
                w.facing = f;
            if (d <= left) {
                w.fx = tx;
                w.fy = ty;
                left -= d;
                w.route.pop_front();
                char step[32];
                std::snprintf(step, sizeof step, "%s_step%d.wav", kFemale[s] ? "female" : "male",
                              1 + int(now_ / 97 % 7));
                sound(step);
            } else {
                w.fx += dx / d * left;
                w.fy += dy / d * left;
                left = 0;
            }
        }
        w.x = int(std::lround(w.fx));
        w.y = int(std::lround(w.fy));
        if (w.route.empty())
            w.walking = false;
        if (s == follow_)
            centreOn(s);
    }
    // The video, in the right column.
    if (video_) {
        const double t = double(now_ - videoStart_) / 1000.0;
        const bool soundOn = mixer_ && videoSound_ != audio::kNoSound && mixer_->isPlaying(videoSound_);
        if (video_->seek(t) || (video_->finished(t) && !soundOn))
            stopVideo();
    }
    runSteps();
    if (!busy() && steps_.empty() && window_ == Window::None && game_.phase() != Game::Phase::Over &&
        !humanTurn())
        computerTurn();
    runSteps();
}

// ---- Sounds, words, videos -----------------------------------------------------

std::string Play::line(int suspect, int index) const {
    const size_t n = size_t(suspect) * kTickerBlock + size_t(index);
    return n < tickerLines_.size() ? tickerLines_[n] : std::string(); // Windows-1252, as the fonts
}

void Play::sound(const std::string& file, bool voice) {
    if (!mixer_ || (voice && !look_.butlerVoice))
        return;
    auto it = sounds_.find(file);
    if (it == sounds_.end()) {
        std::shared_ptr<audio::Sound> s;
        if (const std::filesystem::path* p = data_.find(file)) {
            auto decoded = std::make_shared<audio::Sound>();
            if (!audio::decodeSound(readFile(*p), *decoded))
                s = decoded;
        }
        it = sounds_.emplace(file, s).first;
    }
    if (!it->second)
        return;
    if (voice) {
        if (voice_ != audio::kNoSound)
            mixer_->stop(voice_);
        voice_ = mixer_->play(it->second, audio::SoundType::Speech);
    } else
        mixer_->play(it->second, audio::SoundType::Effects);
}

void Play::startVideo(const std::string& file) {
    stopVideo();
    const std::filesystem::path* p = data_.find(file);
    auto player = std::make_unique<video::VideoPlayer>();
    if (!p || player->open(readFile(*p)))
        return;
    if (mixer_ && player->file().hasAudio()) {
        auto s = std::make_shared<audio::Sound>();
        if (!player->file().audio(*s))
            videoSound_ = mixer_->play(s, audio::SoundType::Speech);
    }
    video_ = std::move(player);
    videoStart_ = now_;
}

void Play::stopVideo() {
    if (mixer_ && videoSound_ != audio::kNoSound)
        mixer_->stop(videoSound_);
    videoSound_ = audio::kNoSound;
    video_.reset();
}

void Play::stopSounds() {
    stopVideo();
    if (mixer_ && voice_ != audio::kNoSound)
        mixer_->stop(voice_);
    voice_ = audio::kNoSound;
}

void Play::centreOn(int suspect) {
    const Walker& w = walkers_[size_t(suspect)];
    camX_ = (w.x - w.y) - kW / 2;
    camY_ = floorDiv(w.x + w.y, 2) - 60 - kBarTop / 2;
}

// ---- Drawing -------------------------------------------------------------------

void Play::drawAnimation(const std::string& name, int x, int y, int frame) {
    const Animation* a = data_.animation(name);
    if (!a || a->layers.empty() || a->frameCount() == 0)
        return;
    const size_t f = size_t(std::clamp(frame, 0, int(a->frameCount()) - 1));
    for (const auto& layer : a->layers) {
        const Animation::Part& part = layer[f];
        if (part.image >= a->images.size())
            continue;
        if (const Image* img = data_.image(a->images[part.image]))
            image::blit(screen_, img->pixels, x + part.x + img->offsetX, y + part.y + img->offsetY);
    }
}

std::vector<Mansion::Actor> Play::actors() {
    std::vector<Mansion::Actor> out;
    // The suspects: standing (states 1-8) or walking (9-16), by facing.
    for (int s = 0; s < kSuspectCount; ++s) {
        const Walker& w = walkers_[size_t(s)];
        const ObjectDef* o = data_.object(kCharacter[s]);
        const ObjectDef::State* st = o ? o->state(uint16_t((w.walking ? 9 : 1) + w.facing)) : nullptr;
        const Animation* a = st ? data_.animation(st->animation) : nullptr;
        if (!a || a->frameCount() == 0)
            continue;
        const auto* frames = mansion_.frames(data_, *a);
        Mansion::Actor actor;
        const size_t f = size_t((now_ + uint64_t(s) * 337) * uint64_t(std::max(1, a->framesPerSecond)) / 1000) %
                         frames->size();
        actor.frame = &(*frames)[f];
        actor.box = {w.x, w.y, a->frames[f].sizeX, a->frames[f].sizeY, a->frames[f].height};
        actor.reveal = true;
        out.push_back(actor);
    }
    // A move being planned: its squares numbered, and the next squares it
    // can take numbered faintly.
    if (humanTurn() && game_.canMove() && !busy()) {
        const auto number = [&](const Square& sq, int n, int opacity) {
            if (n < 1 || n > 9)
                return;
            const Animation* a = data_.animation(std::string("tilenum:At_") + char('a' + n - 1));
            if (!a || a->frameCount() == 0)
                return;
            Mansion::Actor actor;
            actor.frame = &(*mansion_.frames(data_, *a))[0];
            actor.box = {Board::kGridOrigin + sq.x * Board::kSquareSize,
                         Board::kGridOrigin + sq.y * Board::kSquareSize, Board::kSquareSize, Board::kSquareSize, 1};
            actor.flat = true;
            actor.opacity = opacity;
            actor.reveal = opacity == 255;
            out.push_back(actor);
        };
        for (size_t i = 0; i < plan_.size(); ++i)
            if (!plan_[i].inRoom())
                number(plan_[i].square, int(i) + 1, 255);
        for (const Place& p : game_.nextSteps(plan_))
            if (!p.inRoom())
                number(p.square, int(plan_.size()) + 1, 110);
    }
    return out;
}

void Play::draw() {
    const ScreenRect& b = mansion_.bounds();
    camX_ = std::clamp(camX_, b.x - kW / 2, b.x + b.w - kW / 2);
    camY_ = std::clamp(camY_, b.y - kBarTop / 2, b.y + b.h - kBarTop / 2);
    fillRect(screen_, 0, 0, screen_.w, screen_.h, {0, 0, 0});
    MansionLook look = look_.mansion;
    look.characters = false;
    look.lights.clear();
    for (const Walker& w : walkers_)
        look.lights.push_back({w.x, w.y});
    mansion_.draw(screen_, camX_, camY_, look, now_, actors());
    drawMap();
    drawPanel();
    // The solution's window is the whole screen; the others leave the bar.
    if (window_ == Window::Solution) {
        drawBar();
        drawWindows();
        return;
    }
    drawWindows();
    drawBar();
}

void Play::drawBar() {
    drawAnimation("UI:Atopframe", 0, 0);
    drawAnimation("UI:Aleftframe", 0, 4);
    drawAnimation("UI:Artframe", 635, 4);
    drawAnimation("UI:Aleftorn", 0, kBarTop);
    drawAnimation("UI:Anotecardbtn", 39, kBarTop);
    drawAnimation("UI:Agamebdbtn", 84, kBarTop);
    for (int k = 0; k < kSuspectCount; ++k) {
        // Frames: 0 as it is, 1 whose turn it is, 2 not playing (or out).
        const bool away = players_[size_t(k)].kind == Player::None || game_.seat(k).out;
        const int frame = k == game_.turn() && game_.phase() != Game::Phase::Over ? 1 : away ? 2 : 0;
        drawAnimation(std::string("UI:A") + kKey[k] + "bottom", 141 + 60 * k, kBarTop, frame);
    }
    drawAnimation("UI:Alogobtn", 501, kBarTop);
    drawAnimation("UI:Arightorn", 594, kBarTop);
    if (humanTurn() && game_.phase() != Game::Phase::Over)
        drawActions();
    drawAnimation("UI:Aticker", 0, kTickerTop);
    drawAnimation("UI:Artbtmornnorm", 599, kTickerTop + 1);
    drawTicker();
}

void Play::say(const std::string& sentence) {
    if (sentence != ticker_)
        tickerSince_ = now_;
    ticker_ = sentence;
}

void Play::drawTicker() {
    const BitmapFont* f = fonts_.get(2);
    if (!f || ticker_.empty())
        return;
    // Drawn into a strip the ticker's width, so as to come round.
    const int w = kTickerRight - kTickerLeft, h = 18;
    if (tickerStrip_.w != w)
        tickerStrip_.create(w, h, PixelFormat::RGBA32);
    std::fill(tickerStrip_.pixels.begin(), tickerStrip_.pixels.end(), 0);
    const int textW = f->width(ticker_);
    const uint64_t moving = now_ - tickerSince_ > uint64_t(kTickerStillMs) ? now_ - tickerSince_ - kTickerStillMs : 0;
    const int start = (kW - textW) / 2 - kTickerLeft;
    int x = start - int(moving * kTickerSpeed / 10000);
    x = ((x % w) + w) % w;
    const int lineY = (h - f->lineHeight()) / 2;
    f->draw(tickerStrip_, x, lineY, ticker_);
    f->draw(tickerStrip_, x - w, lineY, ticker_);
    image::Image strip{w, h, tickerStrip_.pixels};
    image::blit(screen_, strip, kTickerLeft, kTickerTop);
}

void Play::drawActions() {
    drawAnimation("UI:Aicondrawer", kDrawerX, kBarTop);
    const int hover = actionAt(mouseX_, mouseY_);
    const auto frameOf = [&](int slot, bool enabled) {
        if (!enabled)
            return 3;
        if (slot == pressedAction_ && hover == slot)
            return 2;
        return hover == slot ? 1 : 0;
    };
    const int s = game_.turn();
    const std::string end = std::string("UI:A") + kKey[s] + "end";
    // The first place: the die, rolling, its number; without the die the
    // moves left; once moved, the suspect's pawn to end the turn.
    if (now_ - rollStart_ < 800 && rollStart_ != 0)
        drawAnimation("UI:Arolled", kSlotX[0], kSlotY, int(now_ / 70 % 6));
    else if (game_.canEndTurn())
        drawAnimation(end, kSlotX[0], kSlotY, hover == 0 ? 1 : 0);
    else if (game_.noDieRoll())
        drawAnimation("UI:Anoroll", kSlotX[0], kSlotY, game_.moves());
    else if (game_.phase() == Game::Phase::Moving)
        drawAnimation("UI:Arolled", kSlotX[0], kSlotY, game_.roll() - 1);
    else
        drawAnimation("UI:Arolldiebtn", kSlotX[0], kSlotY, frameOf(0, game_.canRoll()));
    drawAnimation("UI:Asuggestbtn", kSlotX[1], kSlotY, frameOf(1, game_.canSuggest()));
    drawAnimation("UI:Aaccusebtn", kSlotX[2], kSlotY, frameOf(2, game_.canAccuse()));
}

void Play::drawPanel() {
    if (panel_ == Panel::None || panelOwner_ < 0)
        return;
    const int who = panelOwner_;
    const Game::Seat& seat = game_.seat(who);
    if (panel_ == Panel::Cards) {
        drawAnimation(std::string("UI:A") + kKey[who] + "cardwin", kLeftX, kLeftY);
        for (size_t i = 0; i < seat.hand.size() && i < 6; ++i)
            drawAnimation(std::string("UI:A") + kCardKey[seat.hand[i]] + "card", kLeftX + kHandX[i],
                          kLeftY + kHandY[i]);
        drawAnimation("UI:Anotesbtn", kLeftX + 3, kLeftY + 352);
        return;
    }
    drawAnimation(std::string("UI:A") + kKey[who] + "notepad", kLeftX, kLeftY);
    const BitmapFont* font = fonts_.get(1);
    static const int kFirst[3] = {suspectCard(0), weaponCard(0), roomCard(0)};
    static const int kCount[3] = {kSuspectCount, kWeaponCount, kRoomCount};
    for (int k = 0; k < 3; ++k) {
        if (font)
            drawText(screen_, *font, kindNames_[size_t(k)], kLeftX + 10, kLeftY + kNoteHeadY[k], 150, kNoteRowH, false,
                     true);
        // The ruling: light grey lines under each row and between the columns.
        const Color rule{190, 190, 190};
        const int top = kLeftY + noteRowY(k, 0), bottom = kLeftY + noteRowY(k, kCount[k]);
        for (int r = 1; r <= kCount[k]; ++r)
            fillRect(screen_, kLeftX + 8, kLeftY + noteRowY(k, r) - 1, 194, 1, rule);
        for (int x : {kNoteColourX, kNoteMarkX[0], kNoteMarkX[1], kNoteTriangleX})
            fillRect(screen_, kLeftX + x, top, 1, bottom - top, rule);
        for (int r = 0; r < kCount[k]; ++r) {
            const int card = kFirst[k] + r;
            const int y = kLeftY + noteRowY(k, r);
            if (font)
                drawText(screen_, *font, cardNames_[size_t(card)], kLeftX + kNoteTextX, y, 115, kNoteRowH, false, true);
            const NoteRow& row = notes_[size_t(who)][size_t(card)];
            if (row.colour >= 0)
                drawAnimation(std::string("UI:A") + kKey[row.colour] + "noteclr", kLeftX + kNoteColourX + 1, y + 1);
            for (int m = 0; m < 2; ++m)
                if (row.marks[size_t(m)] > 0)
                    drawAnimation(kMark[row.marks[size_t(m)] - 1], kLeftX + kNoteMarkX[m] + 1, y + 1);
            // The triangle: pointing right while its note is open; marked
            // when there is one.
            const char* tri = editing_ == card ? "notesrt" : row.text.empty() ? "notesup" : "notemark";
            drawAnimation(std::string("UI:A") + kKey[who] + tri, kLeftX + kNoteTriangleX, y + 1);
            // The note, beside the notepad: while it is open, or the
            // pointer is on its triangle.
            const bool hover = mouseX_ >= kLeftX + kNoteTriangleX && mouseX_ < kLeftX + 205 && mouseY_ >= y &&
                               mouseY_ < y + kNoteRowH;
            if (const BitmapFont* f = fonts_.get(2); f && (editing_ == card || (hover && !row.text.empty())))
                f->draw(screen_, kLeftX + 212, y + (kNoteRowH - f->lineHeight()) / 2,
                        row.text + (editing_ == card && now_ / 500 % 2 ? "|" : ""));
        }
    }
    drawAnimation("UI:Acardsbtn", kLeftX + 3, kLeftY + 375);
}

void Play::drawMap() {
    if (!map_)
        return;
    drawAnimation("UI:Aoverhead", kMapX, kMapY);
    const auto at = [&](double col, double row, int& x, int& y) {
        x = kMapX + kMapCol0X + int(std::lround((col - kMapFirst) * kMapSquare));
        y = kMapY + kMapRow0Y + int(std::lround((row - kMapFirst) * kMapSquare));
    };
    // A move being planned: its squares numbered in the map's small digits.
    if (humanTurn() && game_.canMove() && !busy())
        for (size_t i = 0; i < plan_.size() && i < 9; ++i)
            if (!plan_[i].inRoom()) {
                int x, y;
                at(plan_[i].square.x, plan_[i].square.y, x, y);
                drawAnimation(std::string("tilenum:Aoverhead_") + char('a' + i), x + 1, y + 1);
            }
    // The suspects' pawns (their state 19), where they are, walking or not:
    // a square's middle is 40 units on from where they stand on it.
    for (int s = 0; s < kSuspectCount; ++s) {
        const ObjectDef* o = data_.object(kCharacter[s]);
        const ObjectDef::State* st = o ? o->state(19) : nullptr;
        if (!st)
            continue;
        const Walker& w = walkers_[size_t(s)];
        int x, y;
        at((w.x - 40) / 64.0, (w.y - 40) / 64.0, x, y);
        drawAnimation(st->animation, x, y);
    }
}

void Play::drawWindows() {
    if (told_)
        told_->draw(screen_);
    if (video_) {
        const image::Image& pic = video_->picture();
        image::blit(screen_, pic, kRightX + (201 - pic.w) / 2, kRightY + (409 - pic.h) / 2);
    }
    if (!dialog_ || window_ == Window::None)
        return;
    dialog_->drawBackground(screen_);
    if (window_ == Window::Choose && choosePage_ == 3) {
        // The summary: what has been chosen, under the frame with its holes.
        const auto put = [&](int card, const UiRect& r) {
            const std::string name = std::string("dialog:A") + kCardKey[card] + "SugAcs";
            if (const Animation* a = data_.animation(name); a && a->frameCount() > 0)
                drawAnimation(name, kRightX + r.x + (r.w - a->width) / 2, kRightY + r.y + (r.h - a->height) / 2);
        };
        put(suspectCard(chosen_.suspect), kSummarySuspect);
        put(weaponCard(chosen_.weapon), kSummaryWeapon);
        put(roomCard(chosen_.room), kSummaryRoom);
    }
    dialog_->drawControls(screen_);
    switch (window_) {
    case Window::Pick:
        for (size_t i = 0; i < pickCards_.size() && i < 3; ++i)
            if (const ResiControl* c = dialog_->info().control(pickSlot(i)))
                drawAnimation(std::string("UI:A") + kCardKey[pickCards_[i]] + "card", kRightX + c->left,
                              kRightY + c->top);
        if (pickChosen_ >= 0)
            if (const ResiControl* c = dialog_->info().control(pickSlot(size_t(pickChosen_))))
                fillRect(screen_, kRightX + c->left, kRightY + c->top + 112, 80, 3, {230, 200, 90});
        break;
    case Window::Shown:
        drawAnimation("UI:Algcard", kShownX + 23, kRightY + 2, game_.shown());
        break;
    case Window::Solution: {
        const Accusation& s = game_.solution();
        const int cards[3] = {suspectCard(s.suspect), weaponCard(s.weapon), roomCard(s.room)};
        for (int k = 0; k < 3; ++k)
            if (const ResiControl* c = dialog_->info().control(k))
                drawAnimation("UI:Algcard", c->left, c->top, cards[k]);
        break;
    }
    default: break;
    }
}

} // namespace reluva::clue
